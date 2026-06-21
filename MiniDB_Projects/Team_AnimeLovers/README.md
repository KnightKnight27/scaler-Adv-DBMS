# MiniDB — Team AnimeLovers

A relational database engine built from scratch in C++17 for the Advanced DBMS
Capstone Project. Implements storage, indexing, query processing, transactions,
WAL recovery, and MVCC as the extension track.

---

## Team Information

**Team Name:** AnimeLovers

| Name | Scaler Email | Roll Number |
|------|-------------|-------------|
| Navjeet Singh | amrinder.singh@scalerailabs.com | SCALER_NJ_01 |
| Yash (heavycoderyash) | yashisyash13112004@gmail.com | SCALER_YS_02 |
| Aadi (ARRY7686) | aadi.24bcs10167@sst.scaler.com | SCALER_AD_03 |
| Kavya Dhyani | kavyadhyani10@gmail.com | SCALER_KD_04 |

---

## 1. Project Overview

### Problem Statement

Build a miniature relational database that demonstrates all core concepts from
the Advanced DBMS course: page-level storage, B+ tree indexing, SQL execution,
cost-based optimization, two-phase locking, write-ahead logging, crash recovery,
and multi-version concurrency control.

### Goals

- Implement each component from scratch with clarity prioritised over
  production-grade performance.
- Make every design decision explainable in a 10-minute viva discussion.
- Deliver a single-binary system that can execute a SQL demo end to end.

### Extension Track: Track B — Concurrency (MVCC)

We implement Multi-Version Concurrency Control as a drop-in replacement for
strict 2PL. The `--mvcc` flag switches the TransactionManager to MVCC mode.
A benchmark in `benchmarks/` compares read throughput between the two modes.

---

## 2. System Architecture

```
┌─────────────────────────────────────────┐
│           Client (REPL / .sql)           │
└─────────────────┬───────────────────────┘
                  │ SQL string
                  ▼
┌─────────────────────────────────────────┐
│         TransactionManager              │
│  ┌────────────┐  ┌────────────────────┐ │
│  │LockManager │  │    MvccStore       │ │
│  │  (2PL)     │  │(version chains)    │ │
│  └────────────┘  └────────────────────┘ │
│              WAL (write-ahead log)       │
└─────────────────┬───────────────────────┘
                  ▼
┌─────────────────────────────────────────┐
│           Parser → Executor             │
│              Optimizer                  │
└────────────┬────────────────────────────┘
             │
   ┌─────────┴─────────┐
   ▼                   ▼
HeapTable          BPlusTree
(slotted pages)    (in-memory index)
   │
BufferPool (LRU, 64 frames)
   │
DiskManager (one .db file per table)
```

### Major Modules

| File | Module | Responsibility |
|------|--------|---------------|
| `value.h` | Types | `Value` (INT/VARCHAR/NULL), `RID` |
| `storage.h/cpp` | Storage | `Page`, `DiskManager`, `BufferPool` |
| `bplustree.h/cpp` | Indexing | In-memory B+ Tree (order 4) |
| `parser.h/cpp` | Parser | Tokenizer + recursive-descent SQL parser |
| `engine.h/cpp` | Engine | `Catalog`, `HeapTable`, `Optimizer`, `Executor`, `Database` |
| `transaction.h/cpp` | Transactions | `WAL`, `LockManager`, `MvccStore`, `TransactionManager` |
| `main.cpp` | CLI | REPL + batch mode, crash recovery on startup |

---

## 3. Storage Layer

### Page Format

Each page is 4096 bytes with a slotted layout:

```
[0..1]  num_slots   : number of slot entries (uint16)
[2..3]  free_end    : byte offset where free space ends (uint16)
[4..]   SlotEntry[] : {offset:uint16, length:uint16} per slot, growing →
        ...free space...
[...PAGE_SIZE-1] record bytes, growing ←
```

A deleted slot has `offset==0, length==0` (tombstone). Space is not reclaimed
(no compaction needed for this scope).

### Heap Files

Each table is stored in a file `<db_dir>/<table_name>.db`. The `DiskManager`
maps page IDs to fixed-size 4096-byte regions in the file. New pages are
appended by extending the file.

### Buffer Pool

`BufferPool` keeps up to 64 pages in memory using LRU eviction. Pages are
pinned during reads/writes (pin count > 0 prevents eviction). Dirty pages are
written back on eviction or `flush_all()`. On startup, `HeapTable` scans all
existing pages to rebuild the in-memory B+ Tree index.

---

## 4. Indexing

### B+ Tree Design

- **Order 4**: internal nodes hold up to 3 separator keys and 4 children;
  leaf nodes hold up to 3 (key, RID) pairs.
- **In-memory**: the tree is rebuilt from the heap scan on every startup. A
  production system would serialise nodes to buffer-pool pages.
- **Leaf linked list**: leaves are doubly linked for efficient range scans
  without traversing the tree.

### Node Structure

```
Internal: [key₀, key₁, ..., key_{n-1}]  →  children[0..n]
Leaf:      [key₀, key₁, ..., key_{n-1}]  →  [rid₀, rid₁, ..., rid_{n-1}]
                                             → next_leaf
```

### Search Path

To find key K:
1. At each internal node, binary-search for the first key ≥ K.
2. Follow the appropriate child pointer.
3. At the leaf, binary-search for K and return the paired RID.

Time complexity: O(log_t N) where t = ORDER/2 = 2 (minimum degree).

---

## 5. Query Execution

### Parser

Hand-written tokenizer + recursive-descent parser covering:
- `SELECT [*|cols] FROM t [JOIN t2 ON cond] [WHERE cond]`
- `INSERT INTO t VALUES (...)`
- `DELETE FROM t [WHERE cond]`
- `CREATE TABLE t (col type [PRIMARY KEY], ...)`
- `DROP TABLE t`
- `BEGIN`, `COMMIT`, `ROLLBACK`

Conditions support: `col OP literal` and `col OP col` (for JOIN ON).

### Query Plan Generation

`Optimizer::plan()` inspects the WHERE condition and picks a scan strategy:

| Condition | Plan |
|-----------|------|
| No WHERE | `TABLE_SCAN` |
| `pk_col = value` | `INDEX_POINT` |
| `pk_col < / > value` | `INDEX_RANGE` |
| Other column | `TABLE_SCAN` |

### Operator Execution

- **TABLE_SCAN**: `HeapTable::scan()` walks all pages in the buffer pool.
- **INDEX_POINT**: `BPlusTree::search(key)` → single page fetch.
- **INDEX_RANGE**: `BPlusTree::range_scan(lo, hi)` → leaf-list walk.
- **JOIN**: nested-loop join; outer rows collected first, inner table scanned once.

---

## 6. Optimizer

### Cost Estimation

Cost is measured in estimated page reads:

```
TABLE_SCAN   → page_count(table)
INDEX_POINT  → 1        (log₂ n tree traversal + 1 page read)
INDEX_RANGE  → selectivity × page_count
```

### Selectivity Estimation

Heuristic table (no statistics collected):

| Condition | Estimated selectivity |
|-----------|-----------------------|
| `pk = v`  | 0.001 (essentially 1 row) |
| `col = v` | 0.10 |
| range     | 0.25 |
| `≠`       | 0.90 |

### Join Ordering

For a two-table nested-loop join, the optimizer estimates each input's
cardinality (`base rows × selectivity`, via `Optimizer::plan_join` +
`HeapTable::approx_row_count`) and **materialises the smaller relation as the
inner side while streaming the larger one** (`JoinPlan::from_is_outer`). This
minimises both the materialised set and the number of outer iterations that
re-scan the inner. The output column order (FROM columns then JOIN columns) is
preserved regardless of which relation is physically streamed. A production
optimizer would extend this to N tables with dynamic programming (the Selinger
algorithm) over all join orderings.

---

## 7. Transactions & Concurrency

### Locking Strategy (2PL mode)

- **Strict Two-Phase Locking**: all locks acquired during execution, released
  only at commit or abort.
- **Lock modes**: `SHARED` (read) and `EXCLUSIVE` (write).
- **Compatibility**: S+S are compatible; any pair involving X is incompatible.
- **Lock granularity: table-level** — readers take a `SHARED` lock on the table,
  writers an `EXCLUSIVE` lock on the table. This is the coarsest (and simplest)
  granularity; it is deliberately chosen so the 2PL-vs-MVCC benchmark shows real
  reader/writer contention. A production system would use row-level locks.
- **Writer preference**: while a writer waits for its `EXCLUSIVE` lock, new
  `SHARED` requests yield to it, preventing a continuous reader stream from
  starving the writer.

The transaction manager keeps the global bookkeeping mutex (`mu_`) and the
storage mutex (`storage_mu_`) strictly separate — they are never held at the
same time — and 2PL table locks are always acquired *before* `storage_mu_`, so
there is no lock-ordering cycle. MVCC reads take none of these: they read a
snapshot version through the index with no locking at all.

### Deadlock Handling

A waits-for graph is maintained: when a transaction T₁ blocks on a lock held
by T₂, edge T₁→T₂ is added. Edge registration and the DFS cycle check happen
**atomically under one lock** (`register_and_detect`), which guarantees that two
mutually-deadlocked transactions cannot both detect the cycle — exactly one is
chosen as the victim and thrown a `DeadlockException` (no timeout needed).

This is demonstrated by `./build/deadlock_demo`: two threads lock resources
A and B in opposite order; the program shows exactly one transaction aborted as
the victim while the other commits, on every run.

### Isolation Guarantees

- **2PL mode**: serializable isolation — the schedule is equivalent to some
  serial execution order.
- **MVCC mode**: snapshot isolation — each transaction reads the committed
  state at its start timestamp.

---

## 8. Recovery

### WAL Design

A binary append-only log file (`minidb.wal`) records every state change before
it reaches the heap:

```
Record format:
  [1B type][8B txn_id][2B table_len][N bytes table]
  [2B col_count][per-column: type + is_null + data]
  [4B page_id][2B slot_id]
```

Log types: `BEGIN`, `INSERT`, `DELETE`, `COMMIT`, `ABORT`.

### Log Records

- `BEGIN`: marks transaction start (no payload beyond txn_id).
- `INSERT`: stores the full row and the RID for REDO.
- `DELETE`: stores the row and RID for UNDO.
- `COMMIT`/`ABORT`: durability fence — flush is called after writing.

### Crash Recovery Procedure

On startup, `recover()` in `transaction.cpp`:

1. **Analysis pass**: classify each txn as committed, aborted, or in-flight.
2. **REDO pass**: replay `INSERT` records for committed transactions
   (idempotent — duplicates caught by the PK constraint).
3. **In-flight txns**: are reported as rolled back. A full ARIES implementation
   would replay `DELETE` (undo) records in reverse for each in-flight INSERT.

---

## 9. Extension Track: MVCC

### Motivation

Under 2PL, a long-running writer holds an exclusive lock that forces all
concurrent readers to block. This severely limits read throughput on workloads
with mixed reads and writes — a common pattern in real databases.

### Design

Each row in `MvccStore` has a version chain:

```
RID → [ {data, begin_ts=5, end_ts=∞},      ← current
         {data, begin_ts=2, end_ts=5},      ← previous
         ... ]
```

A transaction with `snapshot_ts = T` reads the version where:
`begin_ts ≤ T AND T < end_ts`.

On INSERT: a new version is appended with `begin_ts = commit_ts, end_ts = ∞`.
On DELETE: the current version's `end_ts` is sealed to `current_ts`.
On ABORT: `end_ts = 0` (before any valid snapshot → never visible).

Writers do not need to acquire a read lock; readers do not need to acquire any
lock for MVCC reads. This eliminates the reader–writer blocking seen in 2PL.

### Results

See [Benchmarks](#10-benchmarks) below.

---

## 10. Benchmarks

### Experimental Setup

- **Table**: `bench (id INT PRIMARY KEY, val VARCHAR(32))`, 1000 initial rows.
- **Workload**: N reader threads doing random `SELECT WHERE id = k` (an index
  point lookup in both modes) + 1 writer thread doing interleaved `INSERT` /
  `DELETE`.
- **Duration**: 3 seconds per mode.
- **Machine**: MacBook Pro (Apple Silicon), macOS. Run `./build/bench [readers] [seconds]`.

### Measured Results

| Readers | Mode | Reads/sec | Writes/sec | Avg Read Latency (µs) | Speedup |
|---------|------|-----------|------------|----------------------|---------|
| 4 | 2PL  | ~4 000    | ~640 | ~985 | — |
| 4 | MVCC | ~356 000  | ~450 | ~11  | **~87×** |
| 8 | 2PL  | ~6 400    | ~620 | ~1250 | — |
| 8 | MVCC | ~178 000  | ~460 | ~44   | **~28×** |

Numbers are stable across repeated runs and vary with hardware — reproduce with
`./build/bench 4 3` and `./build/bench 8 3`.

### Analysis

In 2PL mode, each reader acquires a `SHARED` lock on the table and holds it
until commit; the writer's `INSERT`/`DELETE` takes an `EXCLUSIVE` table lock.
While the writer holds (or is waiting for) that lock, all readers block — so
read throughput collapses and read latency spikes (~1 ms). We use
writer-preference so the writer is not starved by the reader stream, which is
exactly what creates the contention.

In MVCC mode, each reader takes a snapshot at `BEGIN` and reads the version
visible to that snapshot via the B+ index — **no locks at all**. Readers never
block on the writer (or each other), so throughput scales and latency stays in
the tens of microseconds.

**Why the gap is so large (honest caveat):** our 2PL uses **table-level**
locking, the coarsest granularity — *any* write blocks *every* read. A
real system with row-level locking would let readers and the writer touch
different rows concurrently, shrinking the gap substantially. The benchmark
therefore shows the *maximum* benefit MVCC can provide over coarse 2PL, not a
like-for-like row-level comparison. The qualitative result — MVCC readers don't
block behind writers — holds regardless of granularity.

---

## 11. Limitations

| Area | Current limitation | Future improvement |
|------|-------------------|--------------------|
| MVCC GC | Old versions never reclaimed | Background vacuum thread |
| 2PL lock granularity | Table-level (S for reads, X for writes) | Row-level / predicate locks |
| Storage compaction | Deleted slots never reclaimed | Page compaction / vacuum |
| Index persistence | B+ Tree rebuilt from heap on start | Serialize nodes to pages |
| SQL coverage | No UPDATE, no aggregates (SUM/AVG), no subqueries | Extend grammar |
| JOIN columns | Same-named join columns must be table-qualified (`a.x = b.x`) | Full name resolution |
| Crash recovery | REDO of committed txns only; no LSN/checkpoint | Full ARIES with LSNs |
| Join algorithm | Nested-loop only | Hash join / sort-merge join |
| Statistics | Hardcoded selectivity heuristics | Column histograms |

---

## 12. How to Run

### Dependencies

- C++17 compiler (GCC 10+ or Clang 12+ or Apple Clang 14+)
- CMake ≥ 3.14
- Standard POSIX filesystem support

### Build

```bash
cd MiniDB_Projects/Team_AnimeLovers
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Run the REPL (2PL mode)

```bash
./minidb
```

### Run the REPL (MVCC mode)

```bash
./minidb --mvcc
```

### Run the demo script

```bash
./minidb --batch ../docs/demo.sql
```

### Run the benchmark

```bash
./bench             # 4 readers, 5 seconds each
./bench 8 10        # 8 readers, 10 seconds each
```

### Run the deadlock demonstration

```bash
./deadlock_demo     # two threads deadlock; one is chosen as victim and aborts
```

### Example REPL session

```
minidb> CREATE TABLE t (id INT PRIMARY KEY, name VARCHAR(32));
OK
minidb> INSERT INTO t VALUES (1, 'Alice');
OK
minidb> INSERT INTO t VALUES (2, 'Bob');
OK
minidb> SELECT * FROM t WHERE id = 1;
+----+-------+
| id | name  |
+----+-------+
|  1 | Alice |
+----+-------+
1 row(s)
minidb> BEGIN;
BEGIN txn=1
minidb*> DELETE FROM t WHERE id = 2;
OK
minidb*> ROLLBACK;
ROLLBACK txn=1
minidb> SELECT * FROM t;
+----+-------+
| id | name  |
+----+-------+
|  1 | Alice |
|  2 | Bob   |
+----+-------+
2 row(s)
```
