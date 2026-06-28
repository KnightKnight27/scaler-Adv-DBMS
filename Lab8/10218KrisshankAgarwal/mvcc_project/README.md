# MVCC Version Chains Manager in C++
### Different Page Layouts for SQL Queries in Database Management Systems

---

## Overview

This project implements a full **Multi-Version Concurrency Control (MVCC)** engine in modern C++17, complete with three physical **page layout models** used in real database systems. It demonstrates how SQL-level operations (SELECT, INSERT, UPDATE, DELETE, aggregates) interact with MVCC versioning and how the physical storage layout affects query performance.

---

## Architecture

```
┌────────────────────────────────────────────────────────┐
│                    SQL Executor                         │
│         execInsert / execSelect / execUpdate            │
│         execDelete / execAggSum                         │
└────────────┬─────────────┬──────────────┬──────────────┘
             │             │              │
     ┌───────▼──┐   ┌──────▼──┐   ┌──────▼──┐
     │ NSMPage  │   │ DSMPage │   │ PAXPage │
     │ (row     │   │ (col    │   │(hybrid) │
     │  store)  │   │  store) │   │         │
     └───────┬──┘   └──────┬──┘   └──────┬──┘
             └──────────────┴──────────────┘
                            │
             ┌──────────────▼──────────────┐
             │     VersionChainIndex        │
             │  key → VersionChain          │
             │  (linked list of versions)   │
             └──────────────┬──────────────┘
                            │
             ┌──────────────▼──────────────┐
             │     TransactionManager       │
             │  begin / commit / abort      │
             │  timestamp oracle + GC       │
             └─────────────────────────────┘
```

---

## Page Layout Models

### NSM — N-ary Storage Model (Row Store)
```
Page: [ Slot0: [hdr][col0][col1][col2] | Slot1: [hdr][col0][col1][col2] | ... ]
```
- **Best for:** OLTP — point lookups by primary key, full-row fetches
- **Strength:** entire row in one cache line for INSERT/UPDATE
- **Weakness:** reads ALL columns even for a 1-column projection

### DSM — Decomposition Storage Model (Column Store)
```
Page: [ col0: [v0][v1][v2]... | col1: [v0][v1][v2]... | col2: [v0][v1][v2]... ]
```
- **Best for:** OLAP — aggregations, column scans, projections
- **Strength:** extremely cache-friendly for column-wide scans (SUM, AVG, COUNT)
- **Weakness:** row reconstruction requires one access per column

### PAX — Partition Attributes Across (Hybrid)
```
Page: [ RowMeta[] | minipage(col0): [v0][v1]... | minipage(col1): [v0][v1]... ]
```
- **Best for:** HTAP — mixed workloads needing both row access and column projection
- **Strength:** column-local scans within a page, row access via meta-directory
- **Weakness:** more complex buffer management

### Layout Suitability Summary

| Query Pattern           | NSM | DSM | PAX |
|-------------------------|-----|-----|-----|
| PK Lookup (by ID)       | ★★★ | ★   | ★★  |
| Full Row Fetch          | ★★★ | ★   | ★★  |
| Column Scan (AGG/OLAP)  | ★   | ★★★ | ★★  |
| Projection (few cols)   | ★   | ★★★ | ★★★ |
| Write-heavy OLTP        | ★★★ | ★   | ★★  |
| Mixed HTAP workload     | ★★  | ★★  | ★★★ |
| Cache locality (cols)   | ★   | ★★★ | ★★★ |
| Update (full row)       | ★★★ | ★   | ★★  |

---

## MVCC Concepts Demonstrated

### Version Chains
Each logical row has a **version chain** — a singly-linked list of `VersionRecord` nodes, head = newest. Each version carries:
- `creatorTxn` — which transaction created it
- `[beginTS, endTS)` — the timestamp interval during which this version is visible
- `data` — the actual tuple payload
- `prev` — pointer to the prior (older) version

### Visibility Rule
A version `V` is visible to a transaction reading at timestamp `readTS` iff:
```
V.status == COMMITTED  AND  V.beginTS <= readTS  AND  readTS < V.endTS  AND  V.data.size() > 0
```
The last condition filters tombstone versions created by DELETE.

### Isolation Levels
| Level            | Read Timestamp Used       |
|------------------|---------------------------|
| READ_COMMITTED   | Latest committed TS        |
| SNAPSHOT         | Transaction's `beginTS`    |
| SERIALIZABLE     | `beginTS` + read-set check |

### Conflict Detection
On commit, the write set is validated: if any other transaction wrote the same key after `beginTS`, a `TxnConflictError` is thrown and the transaction is aborted.

### Garbage Collection
`VersionChain::gc(horizonTS)` purges versions whose `endTS < horizonTS`. The horizon is the minimum `beginTS` of all active transactions, maintained by `TransactionManager`.

---

## File Structure

```
mvcc_project/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── mvcc_types.h          # Core types: Value, Tuple, Schema, RID, TxnID, ...
│   ├── version_chain.h       # VersionRecord, VersionChain, VersionChainIndex
│   ├── transaction_manager.h # Transaction, TransactionManager (begin/commit/abort)
│   ├── page_layouts.h        # NSMPage, DSMPage, PAXPage
│   └── sql_executor.h        # SQLExecutor<PageT>, Predicate, QueryResult
├── src/
│   └── main.cpp              # 6 comprehensive demos
└── tests/
    └── test_mvcc.cpp         # 59 unit tests (all passing)
```

---

## Build & Run

### Prerequisites
- C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+)
- CMake 3.14+

### Build
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Run Demo
```bash
./mvcc_demo
```

### Run Tests
```bash
./mvcc_tests
# or via CTest:
ctest --output-on-failure
```

---

## Demo Scenarios

| Demo | Topic |
|------|-------|
| 1    | Full CRUD (INSERT / SELECT / UPDATE / DELETE / AGG) on NSM, DSM, PAX |
| 2    | Snapshot Isolation — concurrent txn visibility |
| 3    | Write-write conflict detection and automatic rollback |
| 4    | Version chain garbage collection |
| 5    | Physical page layout comparison (dumps + suitability table) |
| 6    | Read Committed vs Snapshot Isolation side-by-side |

---

## Key Design Decisions

1. **Append-only writes** — UPDATE and DELETE never overwrite physical slots; they append new versions and seal old ones with an `endTS`.

2. **Version chains as the authoritative source** — SELECT and aggregate queries resolve the correct tuple version through the `VersionChainIndex`, not directly from raw page slots (which may contain stale versions).

3. **Template-based executor** — `SQLExecutor<PageT>` is parameterized over the page type. All three layouts share the same SQL interface; the page type determines physical I/O behavior.

4. **Shared-mutex concurrency** — `VersionChain` and `VersionChainIndex` use `std::shared_mutex` for concurrent reads and exclusive writes.

5. **Monotonic timestamp oracle** — `TransactionManager` uses `std::atomic<Timestamp>` for a lock-free, strictly-monotonic timestamp source.

---

## References

- Larson et al. — *High-Performance Concurrency Control Mechanisms for Main-Memory Databases* (VLDB 2011)
- Neumann, Mühlbauer, Kemper — *Fast Serializable Multi-Version Concurrency Control for Main-Memory Database Systems* (SIGMOD 2015)
- Ailamaki et al. — *Weaving Relations for Cache Performance* — PAX layout (VLDB 2001)
- Stonebraker et al. — *C-Store: A Column-Oriented DBMS* (VLDB 2005)
