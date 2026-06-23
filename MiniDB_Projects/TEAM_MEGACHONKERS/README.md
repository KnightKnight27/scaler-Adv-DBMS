# MiniDB: A C++ Relational Database Engine

MiniDB is a fully functional, ACID-compliant relational database engine built from scratch in modern C++17. This project implements the Track C: Modern Storage extension, replacing traditional heap files with a Log-Structured Merge (LSM) Tree architecture optimized for high-throughput write workloads.

## Architecture Overview

The system is strictly layered to ensure separation of concerns:

1. **Storage Layer (LSM-Tree):** Replaces traditional page-based heap files. Utilizes an in-memory MemTable (buffered writes) that flushes to permanent, read-only SSTables on disk.

2. **Recovery Layer:** Implements a Write-Ahead Log (WAL) with Group Commit to guarantee durability and crash recovery while maximizing sequential I/O throughput.

3. **Query Frontend (Lexer → Parser → Planner):** A hand-written lexer tokenizes SQL once; a recursive-descent parser builds a typed Abstract Syntax Tree (including rich, nested boolean expression trees for `WHERE`/`ON`); and the Planner lowers the AST into a physical executor pipeline. The cost-based Optimizer makes the plan **index-aware** — an equality predicate on an indexed column is routed to an `IndexScan` (O(log N)) instead of a `SeqScan + Filter` (O(N)).

4. **Execution Layer:** Implements the Volcano Iterator (Pipeline) Model. SQL operations are executed via chained operators (SeqScan, IndexScan, Filter, Projection, Insert, Delete, NestedLoopJoin).

5. **Concurrency Layer:** Implements Strict Two-Phase Locking (2PL) via a thread-safe Lock Manager to guarantee serializable transaction isolation. `BEGIN` / `COMMIT` / `ROLLBACK` expose explicit transaction control in the REPL.

## Query Processing Pipeline

Every line entered at the REPL flows through a clean, layered frontend before it touches storage:

```
raw SQL ─▶ Lexer ─▶ Parser ─▶ AST ─▶ Planner ─▶ Optimizer ─▶ Executor tree ─▶ Volcano execution
          (tokens)  (recursive (typed   (physical  (index-aware  (SeqScan/IndexScan/
                     descent)   nodes)   plan)      cost model)    Filter/Projection/Join)
```

* **Lexer** (`src/parser/lexer.*`): single-pass tokenizer; keywords, identifiers, integer/string literals, comparison operators (`= != <> < <= > >=`), and punctuation.
* **Expression trees** (`src/parser/expression.h`): `WHERE`/`ON` predicates are full trees of `Comparison`, `Logical (AND/OR)`, `ColumnRef`, and `Constant` nodes — supporting arbitrarily nested logic and parenthesized grouping, with numeric-aware comparison so `age > 9` correctly accepts `10`.
* **Parser** (`src/parser/parser.*`): recursive descent producing a typed `Statement` AST. Supports `CREATE TABLE`, `CREATE INDEX`, multi-row `INSERT`, `SELECT` (projection lists, `JOIN ... ON`, `WHERE`), `DELETE ... WHERE`, and `BEGIN`/`COMMIT`/`ROLLBACK`. Syntax errors are returned as an `InvalidStatement` (never thrown).
* **Planner** (`src/planner/planner.*`): binds column references to physical indices and emits the executor pipeline. Single-column equality predicates are sent through the **Optimizer**, which consults the table's B+Tree indexes and picks the cheaper of `IndexScan` vs `SeqScan + Filter`.

## Design Decisions

* LSM over B+ Tree (Track C): Chose an append-only LSM architecture. Deletes are treated as writes (Tombstones). This avoids the severe Random I/O disk thrashing penalties associated with updating B+ Tree pages in-place.

* MemTable Implementation: Wraps std::map in a std::shared_mutex. This provides thread-safe, concurrent reads while maintaining sorted keys, bypassing the need for complex, lock-free SkipList pointers for prototyping speed.

* Tuple Serialization: Rows are serialized into flat byte arrays (std::string) immediately upon insertion. This allows the storage layer to write memory directly to disk without CPU overhead for re-serialization.

## Track C: Benchmark Report & Analysis

To empirically validate the architectural benefits of the LSM-Tree, a benchmark was constructed to pit the LSM engine against a custom-built, disk-backed B+ Tree (simulating a 100-page Buffer Pool).

### Benchmark Environment

* Workload: 100,000 Records

* Operations: 100% Writes followed by 100% Point-Lookup Reads.

* LSM Engine: MemTable + WAL (Group Commit) + SSTable

* B+ Tree Baseline: Order 50, Disk-Backed with LRU-simulated page eviction (100 page memory limit).

## Benchmark Results

| Metric           | LSM-Tree (Sequential I/O) | B+ Tree (Random I/O)  |
| ---------------- | ------------------------- | --------------------- |
| Write Throughput | ~256,316 ops/sec          | ~1,807 ops/sec        |
| Read Throughput  | ~1.03 Million ops/sec     | ~1.21 Million ops/sec |

### 1. Analysis: Write Throughput

**Observation:** The LSM-Tree achieved over 140x higher write throughput compared to the disk-backed B+ Tree.

**Architectural Analysis:** This vividly demonstrates the catastrophic impact of Random I/O on storage media. Because the benchmark pushed 100,000 records into a B+ tree constrained by a 100-page buffer pool, the B+ Tree was forced into heavy "disk thrashing"—constantly evicting and flushing dirty 4KB pages to arbitrary offsets on the disk just to make room for new node splits.

Conversely, the LSM-Tree entirely bypasses Random I/O. It writes exclusively to memory (MemTable), and durability is guaranteed via a Group-Committed Write-Ahead Log, resulting in pure, highly efficient Sequential I/O.

### 2. Analysis: Read Latency

**Observation:** The B+ Tree maintained a ~15% lead in read throughput.

**Architectural Analysis:** This aligns with foundational relational database theory: B+ Trees are fundamentally Read-Optimized. Resolving a point-lookup in the B+ Tree requires a highly predictable $O(\log N)$ traversal straight to the leaf node.

The LSM-Tree is Write-Optimized, which introduces read penalties. To resolve a point lookup, the LSM engine must execute a multi-stage search across the MemTable (checking for both valid rows and tombstones) before falling back to scanning SSTables sequentially on disk.

## Engineering Trade-offs (For Future Optimization)

1. **Deadlock Management:** The current Row-Level Lock Manager relies on std::condition_variable blocking. It is susceptible to deadlocks if cycles occur. A production system would implement a Wait-For Graph (Deadlock Detection) or Wound-Wait protocols.

2. **Nested Loop Join Complexity:** The NestedLoopJoinExecutor runs at $O(N \times M)$ time complexity, requiring the engine to repeatedly re-scan the right table. For larger datasets, implementing an In-Memory Hash Join would drastically reduce latency.

3. **In-Memory Major Compaction:** The background compactor currently loads active SSTables into a memory map to merge them. Production systems (e.g., RocksDB) utilize a $K$-way External Merge Sort to keep RAM usage strictly bounded during compaction.

## Setup Instructions

### Prerequisites

* CMake 3.14+

* A C++17 compatible compiler (GCC / Clang)

### Build and Test

```bash
# 1. Clone and enter the directory
mkdir build && cd build

# 2. Configure and build the project
cmake ..
make -j4

# 3. Run the isolated test suites
ctest --output-on-failure

# 4. Run the live execution demonstration
./minidb

# 5. Run the performance benchmark
./lsm_benchmark
```

### Building natively on Windows (MSYS2 / MinGW)

```bash
# Configure with the MinGW Makefiles generator
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build -j4
```

Make sure the MSYS2 runtime DLLs are on `PATH` (e.g. `C:\msys64\ucrt64\bin`)
before building or running `ctest`. CMake's `gtest_discover_tests` launches each
test executable at build time to enumerate cases, and those executables need
`libstdc++` / `libgcc` / `libwinpthread` to be resolvable, otherwise the build's
test-discovery step fails even though compilation succeeded.

## Team Details 

### Team Name:
TEAM_MEGACHONKERS

### Team Members: 
1. Minesh Shaw (minesh.24bcs10029@sst.scaler.com)
2. Sneha Raj (sneha.24bcs10295@sst.scaler.com)
3. Nirbhay (nirbhay.24bcs10030@sst.scaler.com)
4. Adam (adam.24bcs10017@sst.scaler.com)
