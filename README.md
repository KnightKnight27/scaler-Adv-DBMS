# MiniDB — Educational Relational Database Engine

## Team Information

| Field | Details |
|-------|---------|
| **Team Name** | Solo Rider |
| **Member** | Harsha Shetty |
| **Email** | harsha.24bcs10208@sst.scaler.com |
| **Roll Number** | 10208 |

---

## 1. Project Overview

MiniDB is a from-scratch educational relational database engine built in C++17 as a university capstone project. The goal is not to build a production database, but to deeply understand and implement the fundamental components that make relational databases work — from pages on disk all the way up to SQL query execution.

**Problem Statement:** Build a minimal relational database engine that demonstrates understanding of storage management, indexing, query processing, concurrency control, and crash recovery.

**Goals:**
- Implement all core database components from scratch (no SQLite, Boost, or existing DB libraries)
- Keep the code simple and explainable — every line should be defensible in a viva
- Make principled design trade-offs and document them
- Benchmark and compare storage backends (heap file vs LSM-tree)

**Chosen Extension Track:** Track C — Modern Storage (LSM-Tree). We implement an LSM-tree storage backend as an alternative to the heap-file + buffer-pool path, and benchmark the two against each other.

---

## 2. System Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    REPL / CLI Interface                  │
├─────────────────────────────────────────────────────────┤
│                     SQL Parser                          │
│              (Lexer → Parser → AST)                     │
├─────────────────────────────────────────────────────────┤
│              Query Planner & Optimizer                  │
│    (Logical Plan → Cost Estimation → Physical Plan)     │
├─────────────────────────────────────────────────────────┤
│                  Execution Engine                       │
│         (Volcano Iterator: Open/Next/Close)             │
│   SeqScan │ IndexScan │ Filter │ Join │ Projection      │
├─────────────────────────────────────────────────────────┤
│         Transaction Manager         │   Recovery (WAL)  │
│    (Strict 2PL, Deadlock Detection) │  (ARIES-style)    │
├─────────────────────────────────────────────────────────┤
│  B+ Tree Index  │     Catalog       │   Buffer Pool     │
│  (PK lookups)   │  (Table schemas)  │   (LRU-K)        │
├─────────────────────────────────────────────────────────┤
│              Storage Layer                              │
│     Heap Files (slotted pages)  │  LSM-Tree (extension) │
│         Page Manager            │  MemTable + SSTables  │
└─────────────────────────────────────────────────────────┘
                        │
                   [ Disk Files ]
```

**Data Flow:**
1. User types SQL into the REPL
2. Lexer tokenizes → Parser builds AST
3. Planner converts AST → logical plan → optimized physical plan
4. Executor runs the physical plan using Volcano iterators
5. Operators access data via Buffer Pool → Heap Files (or LSM path)
6. Transactions protected by Strict 2PL with deadlock detection
7. All changes logged via WAL for crash recovery

---

## 3. Storage Layer

### Page Format

MiniDB uses a **slotted-page layout** (as taught in lecture) with 4KB fixed-size pages:

```
┌──────────────────────────────────────────┐
│ HEADER (12 bytes)                        │
│  page_id (4B) │ slot_count (2B)          │
│  free_space_offset (2B) │ next_page (4B) │
├──────────────────────────────────────────┤
│ SLOT DIRECTORY (grows downward →)        │
│  [offset₀, len₀] [offset₁, len₁] ...   │
├──────────────────────────────────────────┤
│              FREE SPACE                  │
├──────────────────────────────────────────┤
│ TUPLE DATA (grows upward ←)             │
│  ... tuple₁_bytes ... tuple₀_bytes ...   │
└──────────────────────────────────────────┘
```

- **Slot directory** grows forward from the header; each entry is 4 bytes (2B offset + 2B length).
- **Tuple data** grows backward from the end of the page.
- Deletion uses **tombstoning**: set slot length to 0. Reclaimed later via compaction.

### Tuple Encoding

Variable-length tuples use a simple encoding:
- For each field: 1-byte null flag, then type-specific data
  - INT: 4 bytes (little-endian)
  - FLOAT: 8 bytes (IEEE 754 double via memcpy)
  - VARCHAR: 2-byte length prefix + character data
  - BOOL: 1 byte

### Heap Files

A heap file is a sequence of pages stored in a single file:
- File header: 4 bytes (page count)
- Pages stored sequentially at offset `4 + page_id × 4096`
- A **free-space map** (in-memory vector) tracks available space per page for fast insert routing

### Buffer Pool

**Design choice: LRU-K (K=2) instead of plain LRU.** Plain LRU is vulnerable to sequential flooding — a single full-table scan can evict all frequently-used pages. LRU-K tracks the last K access timestamps per page and evicts the page whose K-th most recent access is oldest, which naturally favors pages with repeated access patterns.

- Fixed-size frame array (configurable pool size)
- Page table maps page_id → frame_id
- Pin counts prevent eviction of in-use pages
- Dirty flags track which pages need flushing

---

## 4. Indexing (B+ Tree)

### Design

- **In-memory B+ tree** mapping integer keys to RecordIds
- Configurable order (fanout); default order = 4
- Leaf nodes linked for efficient range scans

**Trade-off:** Storing the index in memory (not on disk pages) is simpler to implement and explain. The downside is that the index must be rebuilt on restart. For an educational DB with small datasets, this is acceptable.

### Node Structure

- **Internal nodes**: sorted keys + child pointers. Keys act as separators.
- **Leaf nodes**: sorted keys + RecordId values + next-leaf pointer.

### Operations

- **Search**: Navigate root → leaf using binary search at each level. O(log n).
- **Insert**: Find leaf, insert in sorted order. If overflow, split and push middle key up.
- **Delete**: Find leaf, remove. If underflow, redistribute from sibling or merge.
- **Range scan**: Find start leaf, follow next pointers.

---

## 5. Query Execution

### Parser

A **recursive-descent parser** converts SQL text into an AST:
1. **Lexer** tokenizes input (keywords, identifiers, literals, operators)
2. **Parser** uses recursive descent with proper precedence for expressions

Supported SQL:
- `SELECT [columns|*] FROM table [alias] [JOIN table [alias] ON cond] [WHERE cond]`
- `INSERT INTO table [(columns)] VALUES (values)`
- `DELETE FROM table [WHERE cond]`
- `CREATE TABLE name (column_defs, PRIMARY KEY (col))`

### Plan Generation

AST → Logical Plan (relational algebra tree) → Physical Plan (with operator implementations):
- Scan → Filter → Projection (simple queries)
- Scan → Join → Filter → Projection (join queries)

### Volcano Iterator Model

Every operator implements the same interface:
```cpp
void Open();           // Initialize the operator
bool Next(Tuple& out); // Produce the next tuple, return false if done
void Close();          // Clean up resources
```

**Operators:**
- **SeqScan**: Full table scan via heap file
- **IndexScan**: Point/range lookup via B+ tree
- **Filter**: Evaluate WHERE predicates, pass matching tuples
- **Projection**: Select specific columns from tuples
- **NestedLoopJoin**: For each left tuple, scan all right tuples checking the join condition

---

## 6. Optimizer

### Cost Estimation

- **Statistics**: Row count and distinct-value count per column (stored in catalog)
- **Selectivity**: For equality predicates `col = val`, selectivity = 1/distinct_count. For range predicates, assume 1/3 selectivity (simple heuristic).

### Join Ordering

- **Heuristic**: Smaller table on the outer side of nested-loop join (fewer iterations of the inner loop)
- Uses estimated result sizes based on selectivity

### Scan Choice

- **Rule**: If a WHERE clause has an equality predicate on an indexed column, choose IndexScan over SeqScan
- Otherwise, use SeqScan

This is a simple but principled optimizer. A production system would use dynamic programming for join ordering and histogram-based selectivity — those are out of scope here.

---

## 7. Transactions & Concurrency

### Locking Strategy

**Strict Two-Phase Locking (S2PL)** at **page granularity**:
- **Why page granularity over tuple granularity**: Simpler to implement (fewer locks to manage), and with 4KB pages holding multiple tuples, the granularity is fine enough for educational purposes. The trade-off is slightly more lock contention compared to tuple-level locking.
- **Shared (S) locks** for reads, **Exclusive (X) locks** for writes
- Locks held until transaction commit/abort (strict — prevents cascading aborts)

### Isolation Level

**Serializable** — the strongest isolation level. All transactions appear to execute sequentially.

### Deadlock Handling

**Wait-for graph** with cycle detection:
- Build a directed graph where edge (T1 → T2) means "T1 is waiting for a lock held by T2"
- Run DFS to detect cycles
- When a cycle is found, abort the youngest transaction (lowest txn_id as a simple tie-breaker)

**Why wait-for graph over timeout?** It's more precise (no false positives) and more demonstrable for a viva — you can show the actual graph and explain why a specific transaction was chosen as the victim.

---

## 8. Recovery

### WAL Design

**Write-Ahead Logging** with before/after images:
- All changes written to the log *before* being applied to data pages
- Log records: BEGIN, UPDATE (with before & after images), COMMIT, ABORT, CHECKPOINT

### Log Record Format

| Field | Size | Description |
|-------|------|-------------|
| LSN | 8B | Log sequence number |
| TxnId | 4B | Transaction ID |
| Type | 1B | BEGIN/UPDATE/COMMIT/ABORT/CHECKPOINT |
| PageId | 4B | (UPDATE only) Affected page |
| Offset | 2B | (UPDATE only) Offset within page |
| Length | 2B | (UPDATE only) Data length |
| Before | var | (UPDATE only) Old data |
| After | var | (UPDATE only) New data |

### Crash Recovery (Simplified ARIES)

Three phases:
1. **Analysis**: Scan log from last checkpoint, identify committed and active transactions at crash time
2. **Redo**: Re-apply all logged changes from checkpoint forward (idempotent — safe to redo already-applied changes)
3. **Undo**: Roll back changes from uncommitted transactions using before-images

---

## 9. Extension Track — LSM-Tree Storage

### Motivation

Heap files with a buffer pool are optimized for read-heavy workloads with random access. LSM-trees (Log-Structured Merge Trees) offer a different trade-off: much faster writes (sequential I/O only) at the cost of more complex reads and background maintenance (compaction).

### Design

- **MemTable**: In-memory sorted `std::map<int, std::string>` buffering writes. When it reaches a size threshold, it's flushed to disk as an SSTable.
- **SSTable**: Immutable sorted file on disk. Includes a sparse in-memory index (every Nth key → file offset) for fast lookups without full scans.
- **Compaction**: Size-tiered strategy — when too many SSTables of similar size exist, merge them into a larger one. This bounds read amplification.

### Read Path

1. Check MemTable (most recent writes)
2. Check SSTables from newest to oldest
3. Use sparse index to skip irrelevant portions of each SSTable

### Trade-offs vs Heap File

| Aspect | Heap File + Buffer Pool | LSM-Tree |
|--------|------------------------|----------|
| Write speed | Random I/O (slower) | Sequential I/O (faster) |
| Read speed | Direct page access (fast with index) | Multiple levels to check (slower) |
| Space | Minimal overhead | Write amplification from compaction |
| Complexity | Simpler | More complex (compaction, merge logic) |

---

## 10. Benchmarks

*(Results will be added after benchmarking is complete)*

### Setup
- Machine: Linux x86_64
- Compiler: g++ with C++17
- Page size: 4096 bytes
- Buffer pool: 100 frames

### Tests
1. **Write throughput**: Sequential inserts (1K, 10K, 100K rows)
2. **Read latency**: Point lookups after data load
3. **Storage amplification**: On-disk size vs raw data size

### Results

*(To be filled with actual benchmark numbers)*

---

## 11. Limitations

- **No UPDATE statement**: Only SELECT, INSERT, DELETE are supported
- **Integer-only indexes**: B+ tree only supports INT keys
- **In-memory index**: B+ tree does not persist to disk; must rebuild on restart
- **No query pipelining**: Single-threaded query execution
- **Limited SQL**: No aggregations (GROUP BY, COUNT, SUM), no subqueries, no ORDER BY
- **No multi-table transactions**: Transaction support is per-operation
- **Fixed page size**: 4KB, not configurable at runtime
- **No network protocol**: CLI-only, no client-server architecture

### Future Work
- Persistent B+ tree index (stored on disk pages)
- Hash join and sort-merge join operators
- Aggregation operators (GROUP BY, HAVING)
- Multi-version concurrency control (MVCC) as an alternative to 2PL
- Histogram-based selectivity estimation

---

## 12. How to Run

### Prerequisites
- C++17 compatible compiler (g++ 7+ or clang 5+)
- CMake 3.16+
- Git (for Catch2 fetch)

### Build

```bash
cd MiniDB_Projects/Team_SoloRider
cmake -B build
cmake --build build
```

### Run the REPL

```bash
./build/minidb
```

### Example Session

```sql
minidb> CREATE TABLE students (id INT, name VARCHAR(50), age INT, PRIMARY KEY (id));
Table 'students' created.

minidb> INSERT INTO students VALUES (1, 'Alice', 20);
Inserted 1 row.

minidb> INSERT INTO students VALUES (2, 'Bob', 22);
Inserted 1 row.

minidb> SELECT * FROM students WHERE age > 18;
| id | name  | age |
|----|-------|-----|
| 1  | Alice | 20  |
| 2  | Bob   | 22  |
(2 rows)

minidb> DELETE FROM students WHERE id = 1;
Deleted 1 row.
```

### Run Tests

```bash
cd build
ctest --output-on-failure
```

### Run Benchmarks

```bash
./build/minidb_benchmark
```
