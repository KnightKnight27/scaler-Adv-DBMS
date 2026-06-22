# MiniDB — A Relational Database Engine with LSM-tree Storage

**Team:** Goose  
**Track:** C — Modern Storage (LSM-tree)  
**Language:** C++17  
**Course:** Advanced Database Management Systems (Capstone Project)

---

## 1. Project Overview

### Problem Statement

Build a working relational database engine from foundational components — storage, indexing, query processing, transaction management, and crash recovery — and extend it with a modern storage architecture.

### Goals

- Implement a correct, end-to-end SQL execution pipeline
- Integrate all core database components into a coherent system
- Replace the traditional heap-file storage with an **LSM-tree** (Log-Structured Merge-tree)
- Benchmark and analyze write throughput, read latency, and storage amplification

### Chosen Extension Track: **Track C — Modern Storage (LSM-tree)**

We replaced the heap-file storage engine with an LSM-tree design. The LSM-tree buffers writes in an in-memory `MemTable` (backed by `std::map`), flushes sorted immutable `SSTables` to disk, and periodically runs background compaction to merge SSTable files and reclaim space.

This design is the foundation of modern storage engines like RocksDB, LevelDB, and Apache Cassandra.

---

## 2. System Architecture

```
┌─────────────────────────────────────────────────┐
│                    CLI / REPL                     │
├─────────────────────────────────────────────────┤
│                  SQL Parser                       │
│           (Recursive-descent parser)              │
├─────────────────────────────────────────────────┤
│              Query Executor                       │
│    ┌──────────┬──────────┬──────────────┐        │
│    │Table Scan│Index Scan│NestedLoopJoin│        │
│    └──────────┴──────────┴──────────────┘        │
├─────────────────────────────────────────────────┤
│          Cost-Based Optimizer                     │
│   (Selectivity estimation + Join ordering)        │
├─────────────────────────────────────────────────┤
│    B+ Tree Index        Transaction Manager      │
│  (Primary key index)      (2PL + Deadlock)        │
├─────────────────────────────────────────────────┤
│              LSM Storage Engine                   │
│  ┌──────────┐  ┌─────────┐  ┌──────────────┐    │
│  │ MemTable │  │ SSTable │  │  Compaction   │    │
│  │(in-mem)  │→ │(on disk)│→ │ (k-way merge) │    │
│  └──────────┘  └─────────┘  └──────────────┘    │
├─────────────────────────────────────────────────┤
│             Write-Ahead Log (WAL)                 │
│           Crash Recovery Manager                  │
└─────────────────────────────────────────────────┘
```

### Major Modules

| Module | File(s) | Responsibility |
|--------|---------|---------------|
| **Common Types** | `src/common/types.h` | Value, Record, Page serialization |
| **Storage Engine** | `src/storage/lsm_engine.h`, `memtable.h`, `sstable.h`, `compaction.h` | LSM-tree: writes → MemTable → SSTable → Compaction |
| **Buffer Pool** | `src/storage/buffer_pool.h` | LRU page cache |
| **B+ Tree Index** | `src/indexing/bplustree.h` | Primary-key index with insert/search/range-scan |
| **SQL Parser** | `src/query/parser.h` | Tokenizer + recursive-descent parser |
| **Query Executor** | `src/query/executor.h` | Table scan, index scan, nested-loop join |
| **Optimizer** | `src/query/optimizer.h` | Selectivity estimation, join order, access path |
| **Lock Manager** | `src/transaction/lock_manager.h` | Shared/Exclusive locks, deadlock detection |
| **Transaction Mgr** | `src/transaction/transaction.h` | 2PL lifecycle: BEGIN → ACTIVE → COMMIT/ABORT |
| **WAL** | `src/recovery/wal.h` | Sequential binary log with checksums |
| **Recovery** | `src/recovery/wal.h` | Analysis → Redo → Undo |

### Data Flow

1. **Write path**: SQL → Parser → Executor → LSM Engine → MemTable → (flush) → SSTable
2. **Read path**: SQL → Parser → Executor → LSM Engine → MemTable → SSTables (newest first) → Result
3. **Transaction path**: BEGIN → acquire locks (2PL) → execute DML → WAL log → COMMIT (release locks)
4. **Recovery path**: Read WAL → Redo committed → Undo uncommitted

---

## 3. Storage Layer (Track C — LSM-tree)

### MemTable

- In-memory sorted write buffer backed by `std::map<Key, Record>` (red-black tree)
- All writes land here first for low-latency insertion
- When the MemTable reaches the size threshold (default: 256 entries), it is frozen and flushed to disk
- Reads check: active MemTable → frozen MemTables → SSTables (in reverse chronological order)

### SSTable (Sorted String Table)

- Immutable, sorted, on-disk file
- **Format**: Data block (key-value pairs) → Index block (key → offset) → Footer (metadata + magic number)
- Binary search via the cached index for O(log N) point lookups
- Each SSTable is written once and never modified — new versions are created during compaction

### Compaction

- **Strategy**: Size-tiered — when SSTable count exceeds the threshold (default: 4), all SSTables are merged
- **Algorithm**: k-way merge using a min-heap, producing a single sorted output SSTable
- **Tombstone cleanup**: Deleted records (empty Record markers) are dropped during compaction
- **Deduplication**: Newer entries for the same key override older ones

### Comparison: LSM-tree vs B+tree Storage

| Metric | LSM-tree (Track C) | B+tree (Traditional) |
|--------|-------------------|---------------------|
| Write throughput | **High** — sequential I/O | Low — random I/O |
| Point read latency | Moderate — check multiple levels | **Low** — single path |
| Range scan | Good — sorted files | **Good** — leaf chain |
| Storage amplification | Moderate — compaction needed | Low — in-place updates |
| Write amplification | Higher — compaction rewrites | Low — direct page writes |

---

## 4. Indexing — B+ Tree

- **Order**: 4 (configurable via template parameter `FANOUT`)
- Internal nodes store separator keys and child pointers
- Leaf nodes store key-record pairs with sibling pointers for range scans
- **Operations**: `insert()` with node splitting, `search()` with binary traversal, `remove()`, `range_scan()`

### Design Decisions

- The B+ tree lives in **memory** (not paged to disk) for fast access. In production, internal/leaf nodes would be stored as pages in the buffer pool.
- **Order 4** keeps the tree shallow and easy to debug while demonstrating all B+ tree concepts (split, merge, rebalance).

---

## 5. Query Execution

### Parser

- Recursive-descent parser supporting:
  - `CREATE TABLE name (col1 TYPE, ..., PRIMARY KEY (col))`
  - `INSERT INTO name VALUES (v1, v2, ...)`
  - `SELECT [cols] FROM name [WHERE col op val] [JOIN name2 ON colA = colB]`
  - `DELETE FROM name [WHERE col op val]`
- Supported column types: `INT`, `FLOAT`, `STRING`
- Supported operators: `=`, `!=`, `<`, `>`, `<=`, `>=`

### Executor Operators

| Operator | Description |
|----------|-------------|
| **Table Scan** | Full scan of all records via LSM `full_scan()` |
| **Index Scan** | Point lookup via primary key → LSM `get()` |
| **Filter** | Evaluate WHERE clause against each record |
| **Project** | Select subset of columns from each record |
| **Nested-Loop Join** | For each left row, scan all right rows; apply ON condition |
| **Insert** | Serialize record, write to LSM engine |
| **Delete** | Logical delete (tombstone) in LSM engine |

### Cost-Based Optimizer

1. **Selectivity Estimation**: `1/distinct_count` for equality predicates, 33% heuristic for range predicates
2. **Access Path Selection**: Uses index scan when selectivity < 5% (common heuristic)
3. **Join Order Selection**: Greedy algorithm — smallest tables first to minimize intermediate results

---

## 6. Transactions & Concurrency

### Two-Phase Locking (2PL)

- **Growing phase**: Acquire locks as needed (shared for reads, exclusive for writes)
- **Shrinking phase**: Release ALL locks at commit/abort (strict 2PL)
- **Lock modes**: Shared (S) — multiple readers; Exclusive (X) — single writer

### Isolation Guarantee

Serializable isolation via strict 2PL: all locks held until transaction completion prevents dirty reads, non-repeatable reads, and phantom reads.

### Deadlock Handling

- **Detection**: Wait-for graph constructed from lock table, DFS cycle detection
- **Victim selection**: Youngest transaction (highest TxnID) is aborted
- Simplified non-blocking approach: lock requests that cannot be granted immediately return `false`, and the caller retries or aborts

---

## 7. Recovery

### Write-Ahead Log (WAL)

- Sequential binary log file (`wal.log`)
- All modifications are logged BEFORE being applied to storage
- **Log record types**: BEGIN, UPDATE, COMMIT, ABORT
- Each UPDATE record contains: before-image + after-image for undo/redo
- Records include a simple XOR checksum

### Crash Recovery Procedure

1. **Analysis Phase**: Scan WAL, identify committed transactions (have COMMIT record) and active transactions (no COMMIT/ABORT)
2. **Redo Phase**: Replay all UPDATE operations from committed transactions (apply after-image)
3. **Undo Phase**: In reverse order, restore before-images for uncommitted transactions

---

## 8. Extension Track — LSM-tree Storage

### Motivation

Traditional B+tree storage engines suffer from **write amplification** due to random disk I/O during in-place updates. Modern write-heavy workloads (logging, time-series, IoT) benefit from sequential write patterns.

The LSM-tree design addresses this by:
- Buffering writes in memory (MemTable) for low latency
- Writing sorted, immutable files (SSTables) sequentially — no random I/O
- Background compaction to merge files and reclaim space

### Design

Our LSM-tree implementation consists of three components:

1. **MemTable** (`src/storage/memtable.h`): `std::map`-backed sorted buffer with configurable size threshold.
2. **SSTable** (`src/storage/sstable.h`): Immutable sorted file with binary-searchable index.
3. **Compaction** (`src/storage/compaction.h`): Min-heap k-way merge producing a single sorted output.

### Results

See [Benchmarks](#10-benchmarks) for detailed measurements.

---

## 9. Limitations

- **B+ Tree is in-memory only**: Not paged to disk; large indexes may exceed available RAM
- **No secondary indexes**: Only primary-key B+ tree index implemented
- **Single-threaded execution**: No concurrent query execution within a single process
- **Simplify deadlock handling**: Non-blocking lock acquisition with retry instead of wait-die/wound-wait
- **Limited SQL support**: No `UPDATE`, `GROUP BY`, `ORDER BY`, subqueries, or aggregation functions
- **No authentication/authorization**: Single-user system
- **No network protocol**: CLI-only interface

---

## 10. Benchmarks

### Experimental Setup

- **Hardware**: Apple Silicon (M-series), 16 GB RAM
- **Dataset**: 10,000 user records, 100 order records
- **Compiler**: Clang 15 with `-O2` optimization

### Results

| Benchmark | Operations | Time | Throughput |
|-----------|-----------|------|------------|
| Bulk Insert | 10,000 rows | ~X ms | ~Y ops/sec |
| Point Read | 1,000 lookups | ~X μs avg | ~Y ops/sec |
| Range Scan | 500 scans × 20 rows | ~X ms | ~Y scans/sec |
| Transactions | 100 txns × 10 inserts | ~X ms | ~Y txn/sec |
| Nested-Loop Join | 50 joins (100×50 rows) | ~X ms | — |

*(Run `./benchmark` after building to populate exact values.)*

### Analysis

- **Write throughput**: LSM-tree achieves high write throughput due to sequential SSTable writes vs. the random I/O of B+tree heap files
- **Read latency**: Point reads require checking MemTable + frozen MemTables + SSTables; acceptable for most workloads
- **Storage amplification**: Compaction reduces SSTable count from N to 1, eliminating redundant entries and tombstones

---

## 11. How to Run

### Dependencies

- **C++17** compiler (GCC 9+, Clang 10+, or Apple Clang)
- **CMake 3.16+**
- **libreadline** (optional — for interactive CLI with history)
  - macOS: pre-installed
  - Ubuntu/Debian: `sudo apt install libreadline-dev`

### Build

```bash
cd MiniDB_Projects/TEAM-goose
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### Run the CLI

```bash
./minidb_cli
```

### Example Session

```sql
minidb> CREATE TABLE students (id INT, name STRING, age INT, PRIMARY KEY (id));

minidb> INSERT INTO students VALUES (1, 'Alice', 20);
OK. 1 row(s) affected.

minidb> INSERT INTO students VALUES (2, 'Bob', 22);
OK. 1 row(s) affected.

minidb> SELECT * FROM students;
id | name | age
------------------
1 | Alice | 20
2 | Bob | 22
2 row(s) returned.

minidb> SELECT name, age FROM students WHERE id = 1;
name | age
----------
Alice | 20
1 row(s) returned.

minidb> BEGIN;
Transaction 1 started.

minidb[TXN1]> INSERT INTO students VALUES (3, 'Charlie', 25);

minidb[TXN1]> COMMIT;
Transaction 1 committed.

minidb> DELETE FROM students WHERE id = 2;
OK. 1 row(s) affected.

minidb> SELECT * FROM students;
id | name | age
------------------
1 | Alice | 20
3 | Charlie | 25
2 row(s) returned.

minidb> STATS
--- Storage Statistics ---
MemTable size:   0 entries
SSTable count:   1
Tables:          students (2 rows)

minidb> EXIT
Bye!
```

### Run Benchmarks

```bash
./benchmark
```

---

## 12. Team Information

**Team Name**: Goose

| Full Name | Roll Number | Scaler Email |
|-----------|-------------|--------------|
| Siddham Jain | 23BCS10103 | siddham.23bcs10103@sst.scaler.com |
| Bhavya Jain | 23BCS10088 | bhavya.23bcs10088@sst.scaler.com |
| Vatsal Omar | 23BCS10101 | vatsal.23bcs10101@sst.scaler.com |

---

## 13. Project Structure

```
TEAM-goose/
├── README.md                    ← This file
├── CMakeLists.txt               ← Build configuration
├── src/
│   ├── common/
│   │   ├── types.h              ← Value, Record, serialisation
│   │   └── types.cpp
│   ├── storage/
│   │   ├── page.h               ← 4 KB page format
│   │   ├── buffer_pool.h        ← LRU buffer pool
│   │   ├── memtable.h           ← In-memory write buffer
│   │   ├── sstable.h            ← Immutable sorted file
│   │   ├── compaction.h         ← K-way merge compaction
│   │   └── lsm_engine.h         ← LSM-tree storage engine
│   ├── indexing/
│   │   └── bplustree.h          ← B+ Tree index
│   ├── query/
│   │   ├── parser.h             ← SQL parser
│   │   ├── executor.h           ← Query executor + Catalog
│   │   └── optimizer.h          ← Cost-based optimizer
│   ├── transaction/
│   │   ├── lock_manager.h       ← 2PL lock manager
│   │   └── transaction.h        ← Transaction manager
│   ├── recovery/
│   │   └── wal.h                ← WAL + Recovery manager
│   ├── database.h               ← Main DB interface
│   └── main.cpp                 ← CLI entry point
├── benchmarks/
│   └── benchmark.cpp            ← Benchmark suite
├── docs/
│   └── architecture.md          ← Detailed architecture notes
└── tests/
    └── (test files)
```

---

## References

1. O'Neil, P., et al. "The Log-Structured Merge-tree (LSM-tree)." *Acta Informatica*, 1996.
2. Google LevelDB — Reference LSM-tree implementation
3. Facebook RocksDB — Production LSM-tree engine
4. Gray, J., Reuter, A. "Transaction Processing: Concepts and Techniques." 1993.
5. Silberschatz, A., et al. "Database System Concepts." 7th Edition.
