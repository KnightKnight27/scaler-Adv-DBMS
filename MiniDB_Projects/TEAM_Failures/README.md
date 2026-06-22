# MiniDB — A Minimal Relational Database Engine (Advanced DBMS Capstone)

MiniDB is a small but complete relational database engine written in C++17. It
implements, from scratch, the layers found in a real database: a page-based
storage engine with a buffer pool, a B+ Tree index, a SQL parser, a Volcano-style
execution engine, a cost-based optimizer, transactions with Strict Two-Phase
Locking and deadlock detection, Write-Ahead Logging with crash recovery, and —
as the chosen extension — an **LSM-tree storage engine** (Track C).

The guiding principle of this project is **clarity over cleverness**: every file
is heavily commented, the design is deliberately simple, and we can explain every
line.

---

## Team Information

> **Team Name:** Failures
>
> | Full Name | Roll Number | Email |
> |-----------|-------------|-------|
> | Shaurya Verma | 24BCS10151 | shaurya.24bcs10151@sst.scaler.com |
> | Arjun Aggarwal | 24BCS10109 | arjun.24bcs10109@sst.scaler.com |
> | Ashutosh Kumar | 24BCS10111 | ashutosh.24bcs10111@sst.scaler.com |

---

## Table of Contents
1. [Project Overview](#1-project-overview)
2. [System Architecture](#2-system-architecture)
3. [Storage Layer](#3-storage-layer)
4. [Indexing](#4-indexing)
5. [Query Execution](#5-query-execution)
6. [Optimizer](#6-optimizer)
7. [Transactions & Concurrency](#7-transactions--concurrency)
8. [Recovery](#8-recovery)
9. [Extension Track — LSM Storage](#9-extension-track--lsm-storage-track-c)
10. [Benchmarks](#10-benchmarks)
11. [Limitations](#11-limitations)
12. [How to Run](#12-how-to-run)

---

## 1. Project Overview

**Problem statement.** Build a working relational database engine that
integrates all the components studied in the course (storage, indexing, query
processing, optimization, transactions, recovery) into one coherent system that
we fully understand and can defend.

**Goals.**
- Correctness first: queries return the right answers; committed data survives
  crashes; concurrent transactions are isolated.
- A clean, layered architecture where each module has one job.
- Code simple enough to read top-to-bottom and explain in a viva.

**Chosen extension track: Track C — Modern Storage (LSM-tree).** We added an
LSM-tree key-value engine (MemTable + SSTables + compaction) and benchmarked it
against the B+ Tree to study the write-throughput vs read-latency vs
space-amplification trade-offs.

**What MiniDB supports (SQL):**
```sql
CREATE TABLE t (col TYPE [PRIMARY KEY], ...);   -- TYPE = INT | VARCHAR
CREATE INDEX name ON t (col);
INSERT INTO t VALUES (...);
SELECT col,... | *  FROM t  [JOIN t2 ON t.a = t2.b]  [WHERE pred AND ...];
DELETE FROM t [WHERE pred AND ...];
BEGIN;  COMMIT;  ROLLBACK;
```

---

## 2. System Architecture

MiniDB is a stack of layers. A SQL string travels **down** the stack to become
disk reads/writes, and rows travel back **up**.

```
                ┌─────────────────────────────────────────────┐
   SQL string   │                  REPL (main.cpp)            │
       │        └─────────────────────────────────────────────┘
       ▼
 ┌───────────┐   ┌──────────────┐   ┌──────────────────────────┐
 │  Lexer    │──▶│   Parser     │──▶│   AST (Statement objects) │   sql/
 └───────────┘   └──────────────┘   └──────────────────────────┘
                                              │
                                              ▼
 ┌──────────────────────────────────────────────────────────────┐
 │                    Database engine (engine/)                   │
 │   binds names ▸ asks Optimizer ▸ builds Executors ▸ runs them  │
 │   takes Locks ▸ writes WAL ▸ maintains Indexes                 │
 └──────────────────────────────────────────────────────────────┘
     │              │                 │              │          │
     ▼              ▼                 ▼              ▼          ▼
 ┌────────┐   ┌───────────┐    ┌────────────┐  ┌────────┐  ┌────────┐
 │Optimizer│  │ Executors │    │  Catalog   │  │  Lock  │  │  Log   │
 │optimizer│  │ execution │    │  catalog   │  │ Manager│  │Manager │
 └────────┘   └───────────┘    └────────────┘  │  txn   │  │recovery│
                   │                 │          └────────┘  └────────┘
                   ▼                 ▼
            ┌──────────────┐   ┌──────────────┐
            │  B+ Tree     │   │  TableHeap   │   index/ , storage/
            │  (in memory) │   │ (heap file)  │
            └──────────────┘   └──────────────┘
                                      │
                                      ▼
                            ┌──────────────────┐
                            │   Buffer Pool     │  storage/  (LRU cache)
                            └──────────────────┘
                                      │
                                      ▼
                            ┌──────────────────┐
                            │   Disk Manager    │  storage/  (the .db file)
                            └──────────────────┘
```

**Major modules** (each directory under `src/`):

| Module | Directory | Responsibility |
|--------|-----------|----------------|
| Common | `common/` | shared types: `page_id_t`, `RID`, `PAGE_SIZE`, exceptions |
| Records | `record/` | `Value`, `Schema`, `Tuple` (+ serialization) |
| Storage | `storage/` | `DiskManager`, `BufferPool` (LRU), slotted `TablePage`, `TableHeap` |
| Index | `index/` | `BPlusTree` (search / insert / delete / range) |
| SQL | `sql/` | `Lexer`, `Parser`, `ast.h` |
| Execution | `execution/` | Volcano operators: scan, index scan, join, projection |
| Optimizer | `optimizer/` | selectivity, scan choice, join order |
| Catalog | `catalog/` | tables, schemas, indexes (+ persistence) |
| Transactions | `txn/` | `Transaction`, `LockManager` (2PL + deadlock) |
| Recovery | `recovery/` | `LogManager`, `LogRecord`, `RecoveryManager` (WAL) |
| Engine | `engine/` | `Database` — ties everything together |
| LSM (extension) | `lsm/` | `LSMTree` (MemTable / SSTable / compaction) |

**Data flow for `SELECT * FROM users WHERE id = 3;`**
1. Lexer → tokens; Parser → a `SelectStmt`.
2. Engine takes a **shared lock** on `users`, asks the **Optimizer** for an
   access path. The optimizer sees `id` is the PK (indexed, unique) → chooses an
   **IndexScan**.
3. Engine builds `IndexScanExecutor`, calls `init()` then `next()` repeatedly.
4. The executor asks the **B+ Tree** for the RID of key 3, fetches the tuple from
   the **TableHeap** (via the **Buffer Pool**), and returns it.
5. The REPL prints the row. On auto-commit, the engine **commits** and releases
   the lock.

---

## 3. Storage Layer

Files: [`storage/disk_manager.*`](src/storage/disk_manager.h),
[`storage/buffer_pool.*`](src/storage/buffer_pool.h),
[`storage/page.h`](src/storage/page.h),
[`storage/table_page.h`](src/storage/table_page.h),
[`storage/table_heap.*`](src/storage/table_heap.h).

**Page format.** The unit of I/O is a fixed **4 KB page** (`PAGE_SIZE`). A table's
pages are **slotted pages**:

```
+-----------------------------------------------------------------------+
| header | slot0 slot1 slot2 ...  ->     free space    <-   tuple bytes  |
|        | (slot directory grows right)                (grow left)       |
+-----------------------------------------------------------------------+
 header = [ next_page_id (4) | num_slots (4) | free_ptr (4) ]
 slot   = [ offset (4) | length (4) ]    length == -1  =>  deleted (tombstone)
```
Variable-length tuples are placed from the end of the page backwards; the slot
directory grows from the front. A tuple's address is its **RID = (page_id, slot)**.

**Heap files.** A table is a singly-linked list of pages
(`first_page → next → next → …`). New rows go into the last page; when it fills,
a new page is allocated and linked. The page chain structure is flushed eagerly
so it is always walkable after a crash.

**Buffer pool.** Only `BUFFER_POOL_SIZE` (64) pages fit in RAM at once. The buffer
pool:
- maps `page_id → frame`, and **pins** a page while it is in use (a pinned page is
  never evicted);
- tracks **dirty** pages and writes them back before reuse (so updates are not
  lost on eviction — the *steal* policy);
- uses **LRU** replacement: the least-recently-used unpinned page is evicted first;
- before writing any dirty page it **flushes the WAL** (the write-ahead rule).

You can watch eviction happen by inserting more than 64 pages worth of data.

---

## 4. Indexing

File: [`index/bplus_tree.*`](src/index/bplus_tree.h).

**Why.** A heap scan is O(n). A B+ Tree gives O(log n) lookups and *ordered*
access, so `WHERE id = 3` and range queries `WHERE age > 30` are cheap.

**Node structure.**
- **Internal nodes** hold only keys (separators) and child pointers — they are
  signposts.
- **Leaf nodes** hold the real `(key → RID)` entries and are **chained left to
  right**, so a range scan is "find the first leaf, then walk the chain".
- All leaves sit at the same depth; the tree stays balanced because growth
  happens by **splitting** a full node and pushing one key up to its parent.

**Search path.** Start at the root; at each internal node pick the child whose key
range contains the search key; stop at a leaf and binary-search it.

```
                 [ 20 | 40 ]                 internal (separators)
                /     |     \
         [10|15]  [25|30]  [50|60]           leaves (key -> RID), chained:
            └────────┴────────┘              10→15→25→30→50→60
```

**Operations.** `insert` (with full node splitting), `search` (point lookup),
`range(low, high)` (ordered scan over the leaf chain), and `remove(key, rid)`.
Keys may repeat, so the same structure serves a **unique primary key** and a
**non-unique secondary index** (that's why `remove` takes the exact RID).

**Design note.** The B+ Tree is an in-memory structure, rebuilt by scanning the
heap on startup. This keeps the index logic self-contained and means it needs no
on-disk format or its own recovery. The heap (which *is* page-backed) is the
source of truth.

---

## 5. Query Execution

Files: [`sql/`](src/sql/), [`execution/`](src/execution/).

**Parser.** A hand-written **lexer** turns SQL text into tokens; a
**recursive-descent parser** turns tokens into an **AST** (`Statement` subclasses
in [`ast.h`](src/sql/ast.h)). One function per grammar rule — easy to read and
extend.

**Query plan = a tree of operators.** Execution uses the **Volcano (iterator)
model**: every operator implements `init()` then `next()` (called repeatedly until
it returns `false`). Calling `next()` on the root pulls one row up through the
whole tree — rows stream one at a time, nothing huge is materialized.

Operators (`execution/executor.h`):
- `SeqScanExecutor` — read every live tuple of a heap, keep those matching the
  predicates.
- `IndexScanExecutor` — use the B+ Tree to fetch only RIDs in a key range.
- `NestedLoopJoinExecutor` — for each outer row, find matching inner rows; if the
  inner table has an index on the join column it **probes the index** (index
  nested-loop join), else it scans the inner heap.
- `ProjectionExecutor` — output only the selected columns.

Example plan for
`SELECT users.name, orders.item FROM users JOIN orders ON users.id = orders.uid;`
```
Projection(users.name, orders.item)
   └── NestedLoopJoin(users.id = orders.uid)
          ├── SeqScan(orders)              ← outer (chosen by optimizer)
          └── IndexScan(users on id)       ← inner, probed once per outer row
```

---

## 6. Optimizer

File: [`optimizer/optimizer.*`](src/optimizer/optimizer.h).

The optimizer makes two **cost-based** decisions.

**(a) Access path: SeqScan vs IndexScan.** It estimates how many rows a predicate
keeps — its **selectivity** — using textbook heuristics:

| Predicate | Estimated selectivity |
|-----------|-----------------------|
| `col = v` on a unique / PK column | 1 row (1/N) |
| `col = v` on a non-unique column | 10% |
| range `<, >, <=, >=` | 33% |

Then it compares costs (tuples touched):
`seq cost ≈ N` vs `index cost ≈ N × selectivity`. The index is used only when it
is cheaper. The chosen predicate becomes B+ Tree key bounds; **all** predicates
are re-checked after fetch, so correctness never depends on the estimate.

**(b) Join order.** For a two-table join the optimizer puts the table that yields
**fewer rows** on the outside of the nested loop (the inner side runs once per
outer row, so a smaller outer is cheaper).

Every `SELECT` prints its chosen plan (the `-- plan:` line), so you can *see* the
optimizer working:
```
-- plan: IndexScan on users.id using users_pk (est 1 rows)
-- plan: SeqScan on users (est 1 of 4 rows)
-- plan: NestedLoopJoin [outer: SeqScan on orders ...] x [inner: IndexScan on users.id]
```

---

## 7. Transactions & Concurrency

Files: [`txn/transaction.h`](src/txn/transaction.h),
[`txn/lock_manager.*`](src/txn/lock_manager.h).

**Isolation level: SERIALIZABLE**, enforced by **Strict Two-Phase Locking (2PL)**.

**Locking strategy.** MiniDB locks at **table granularity** (deliberately coarse,
for clarity):
- `SELECT` takes a **shared (S)** lock — many readers may hold it together.
- `INSERT` / `DELETE` take an **exclusive (X)** lock — only one holder, excludes
  all readers.

| | held S | held X |
|---|---|---|
| **want S** | ✅ | ❌ |
| **want X** | ❌ | ❌ |

**Strict 2PL** means every lock is released *together*, at COMMIT or ABORT (the
"shrinking phase" is a single instant). Table-level S/X locking gives true
serializability with **no phantom problem** (an inserting writer is blocked by any
reader's table lock).

**Deadlock handling.** Two transactions can wait for each other forever. Before a
request blocks, the lock manager builds the **wait-for graph** (edge `A→B` = "A
waits for a lock B holds") and runs cycle detection through the requester. If a
cycle exists, the request is refused and that transaction is **aborted**, breaking
the cycle.

**See it live:** `make demo && ./build/demo_concurrency`
```
=== Demo 1: lock blocking (Strict 2PL) ===
[t+0ms]   T1(writer): holding X lock; working for 600ms...
[t+158ms] T2(reader): wants S lock on accounts -> should BLOCK
[t+601ms] T1(writer): COMMIT -> releases locks
[t+601ms] T2(reader): got the lock -> SELECT ran          ← waited ~440ms

=== Demo 2: deadlock detection ===
[t+0ms] T1: ABORTED -> deadlock detected; transaction aborted
[t+0ms] T2: COMMIT (this txn won)
```

---

## 8. Recovery

Files: [`recovery/log_record.h`](src/recovery/log_record.h),
[`recovery/log_manager.*`](src/recovery/log_manager.h),
[`recovery/recovery_manager.*`](src/recovery/recovery_manager.h).

**WAL design.** MiniDB uses **Write-Ahead Logging**: before any change reaches the
data file on disk, a record describing it is already in the log (`*.wal`). The
buffer pool enforces this by flushing the log before writing any dirty page, and
COMMIT flushes the log so a committed transaction is durable.

**Log records.** Five kinds: `BEGIN`, `INSERT`, `DELETE`, `COMMIT`, `ABORT`.
`INSERT`/`DELETE` carry the table, the exact RID, and the tuple bytes — enough to
both **redo** (re-apply) and **undo** (reverse) the change.

**Crash recovery procedure** (a simplified ARIES, no checkpoints):
1. Read the whole WAL.
2. **Winners** = transactions with a COMMIT record; **losers** = the rest.
3. **REDO** every `INSERT`/`DELETE` in log order (operations are idempotent, so
   re-applying is safe) → the heap matches its state at crash time.
4. **UNDO** every loser's change in reverse order → uncommitted work disappears.
5. Rebuild the in-memory indexes from the recovered heaps.

Result: **committed transactions are preserved, uncommitted ones vanish.**

**See it live** (`.crash` exits without flushing — like pulling the power):
```
$ ./build/minidb /tmp/db
CREATE TABLE acct (id INT PRIMARY KEY, bal INT);
BEGIN; INSERT INTO acct VALUES (1,100); INSERT INTO acct VALUES (2,200); COMMIT;
BEGIN; INSERT INTO acct VALUES (3,999);   -- not committed
.crash
$ ./build/minidb /tmp/db
SELECT * FROM acct;   -- rows 1 and 2 are present; row 3 is gone
```

---

## 9. Extension Track — LSM Storage (Track C)

Files: [`lsm/lsm_tree.*`](src/lsm/lsm_tree.h), benchmark in
[`src/bench.cpp`](src/bench.cpp).

**Motivation.** A B+ Tree does in-place, random updates — expensive on disk for
write-heavy workloads. An **LSM-tree** (the engine behind RocksDB / LevelDB /
Cassandra) turns random writes into sequential ones to maximize write throughput.

**Design.**
1. **MemTable** — all writes go into an in-memory sorted map. Fast, no disk I/O.
2. **Flush** — when the memtable fills, it is written out in one sequential pass
   as an immutable, sorted **SSTable** file.
3. **Reads** check the memtable first, then SSTables newest → oldest (newer data
   shadows older).
4. **Compaction** — many SSTables are merged into one, keeping the newest value
   per key and dropping deleted keys.
5. **Deletes** write a **tombstone** marker; compaction physically removes it.

**Results (see [benchmarks/REPORT.md](benchmarks/REPORT.md)).** Compared to the
B+ Tree, the LSM has competitive write throughput and, crucially, demonstrates the
classic trade-offs: **space amplification** (≈2× until compaction) and **read
amplification** (lookups get ~7× slower with many SSTables, then drop back after
compaction).

---

## 10. Benchmarks

See the full report: **[benchmarks/REPORT.md](benchmarks/REPORT.md)**.

Run it yourself:
```
make bench
./build/bench 200000
```

Representative output (Apple M-series, `-O2`, 200k keys):
```
[WRITE THROUGHPUT]   B+ Tree ~2.7M inserts/s   LSM ~1.9M puts/s
[READ LATENCY]       B+ Tree ~250 ns/lookup    LSM ~130 ns/lookup (1 SSTable)
[SPACE AMP]          before compaction ~2x     after ~1x
[READ AMP]           40 SSTables ~550 ns       1 SSTable ~80 ns
```

---

## 11. Limitations

We chose simplicity on purpose; these are the conscious trade-offs and what we'd
do next.

- **Coarse locking.** Locks are table-level, not row-level. Correct and
  serializable, but lower concurrency than row locking with intention locks.
- **In-memory B+ Tree.** The index is rebuilt from the heap on startup rather than
  persisted as pages. Fine for our data sizes; a page-backed B+ Tree would scale
  to larger-than-RAM indexes.
- **No intra-page compaction / free-space reuse.** Deleted slots are tombstoned
  but their space is not reclaimed until... it isn't; pages only grow. A vacuum
  process would reclaim it.
- **No checkpoints in recovery.** Recovery replays the whole WAL from the start;
  a real system periodically checkpoints to bound replay time.
- **Small SQL surface.** Two types (INT, VARCHAR), conjunctive (`AND`-only) WHERE,
  single two-table joins, no aggregation/GROUP BY/ORDER BY, single-row INSERT.
- **B+ Tree delete doesn't merge** underfull nodes (it tolerates them), so the
  tree can waste space after many deletions.

---

## 12. How to Run

**Dependencies:** a C++17 compiler (`g++` or `clang++`). No external libraries.
Build with **CMake** (like the course repo) or the provided Makefile.

**Build with CMake:**
```bash
cmake -S . -B build
cmake --build build
# -> build/minidb   build/bench   build/demo
```

**Or build with Make:**
```bash
make            # builds build/minidb        (the REPL)
make bench      # builds build/bench         (LSM vs B+ Tree benchmark)
make demo       # builds build/demo_concurrency (2PL + deadlock demo)
make clean      # remove build artifacts
```

**Run the REPL:**
```bash
./build/minidb                 # uses ./minidb_data.* files
./build/minidb /tmp/mydb       # custom database path
./build/minidb /tmp/mydb < demos/demo.sql   # run a script
```

**Example commands** (inside the REPL):
```sql
CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR, age INT);
INSERT INTO users VALUES (1, 'alice', 30);
SELECT * FROM users WHERE id = 1;     -- uses the index (see the plan line)
SELECT name FROM users WHERE age > 25;
.tables        -- list tables
.crash         -- simulate a crash (for the recovery demo)
.exit
```

**Run the demos:**
```bash
./build/bench 200000          # benchmark
./build/demo_concurrency      # locking + deadlock
```
