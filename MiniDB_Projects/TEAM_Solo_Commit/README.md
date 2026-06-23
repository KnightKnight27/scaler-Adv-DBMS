# MiniDB: A Relational Database Engine Built From Scratch

MiniDB is a small but complete relational database engine written in C++17. It runs real SQL
end to end: it stores rows in page based heap files, indexes them with a B+Tree, parses and
plans queries, picks between an index scan and a sequential scan with a cost model, runs
transactions under Strict Two Phase Locking with deadlock detection, and survives crashes
through a Write Ahead Log. The chosen extension track adds Multi Version Concurrency Control so
that readers never block writers.

Every component here is built from the ground up. The buffer pool, B+Tree, SQL parser, and
transaction manager grew directly out of the individual lab exercises earlier in the course;
this project is where they come together into one working system.

---

## Team Information

**Team Name:** Solo Commit

| Full Name | Roll Number | Scaler Email |
|---|---|---|
| Om Malviya | 24BCS10448 | om.24bcs10448@sst.scaler.com |

> Note: this submission was done solo. The guidelines suggest teams of 2 to 4, so please
> confirm solo submission with the instructor if needed.

---

## 1. Project Overview

**Problem.** A database has to solve the same hard problem that every storage system faces:
data lives on slow disk, memory is small and volatile, and many transactions run at once, yet
queries must still be fast, correct, and durable. The goal of this project is not to build a
feature rich SQL dialect but to build the *internals* that make those guarantees possible and
to be able to explain why each piece is there.

**Goals.**
- A working storage engine with page based heap files and a buffer pool.
- A B+Tree index used during query execution.
- End to end SQL: `CREATE`, `INSERT`, `SELECT` (with `WHERE`, `JOIN`, projection), `DELETE`.
- A cost based optimizer that chooses between an index scan and a sequential scan, and picks a
  join order.
- Transactions with serializable behavior under Strict 2PL, including deadlock detection.
- Crash recovery through a Write Ahead Log.

**Chosen extension track: Track B, Concurrency (MVCC).** On top of the 2PL baseline we add a
multi version store with snapshot visibility. It is the natural next step from the version
chain idea in the transaction lab, and it lets us show the classic MVCC result: readers do not
block writers and read throughput stays high under contention.

---

## 2. System Architecture

A SQL statement flows top to bottom through the pipeline; the bottom three layers are the
storage and durability stack that everything sits on.

```
                 SQL text
                    |
                    v
        +-----------------------+
        |   Parser (Lab 5)      |  tokenizer + recursive descent -> AST
        +-----------------------+
                    |  AST
                    v
        +-----------------------+        +-----------------------+
        |   Planner             |<------>|   Optimizer           |
        |   AST -> operator tree|        |   cost model:         |
        +-----------------------+        |   index vs seq scan,  |
                    |                    |   join order          |
                    |  Executor tree     +-----------------------+
                    v
        +-------------------------------------------------------+
        |   Execution (Volcano iterators)                       |
        |   SeqScan IndexScan Filter Project NLJoin Insert Delete|
        +-------------------------------------------------------+
              |                 |                    |
              | locks           | row versions       | WAL records
              v                 v                    v
   +----------------+  +------------------+  +------------------+
   | Lock Manager   |  | Version Store    |  | Log Manager (WAL)|
   | Strict 2PL,    |  | MVCC (Track B)   |  | redo/undo log    |
   | deadlock DFS   |  | snapshot reads   |  | crash recovery   |
   +----------------+  +------------------+  +------------------+
              |
              v
        +-----------------------+
        |   Catalog             |  tables, schemas, indexes
        +-----------------------+
                    |
                    v
        +-----------------------+
        |   Buffer Pool         |  clock sweep eviction (Lab 3)
        +-----------------------+
                    |  4 KB pages
                    v
        +-----------------------+
        |   Disk Manager        |  page based file I/O (Lab 1)
        +-----------------------+
```

**Major modules** (all under `src/`):

| Module | Files | Responsibility |
|---|---|---|
| common | `common/types`, `tuple`, `schema`, `rid` | typed values, rows, schemas, record ids |
| storage | `storage/page`, `disk_manager`, `buffer_pool`, `heap_file` | 4 KB slotted pages, file I/O, clock sweep cache, heap tables |
| index | `index/bplus_tree` | B+Tree mapping a key to RIDs |
| parser | `parser/tokenizer`, `ast`, `parser` | SQL text to AST |
| catalog | `catalog/catalog` | table, schema, and index registry |
| planner | `planner/planner`, `optimizer` | AST to operator tree, cost based choices |
| execution | `execution/executors`, `evaluator`, `exec_context` | Volcano operators, expression evaluation |
| txn | `txn/lock_manager`, `transaction` | Strict 2PL, deadlock detection, transaction state |
| mvcc | `mvcc/version_store` | Track B: version chains + snapshot visibility |
| recovery | `recovery/log_record`, `log_manager` + `database` recovery | WAL and crash recovery |
| engine | `database`, `main` | orchestration, REPL, demo |

**Data flow for a query.** `Database::Execute` parses the SQL into an AST, the `Planner` walks
the AST and asks the `Optimizer` for the access path and join order, producing a tree of
`Executor` objects. Calling `Init` then `Next` repeatedly pulls rows up the tree. Scans read
pages through the `BufferPool`, which fetches from the `DiskManager` on a miss. When a
transaction is active, scans take shared locks and writes take exclusive locks through the
`LockManager`, and every mutation is appended to the WAL through the `LogManager`.

---

## 3. Storage Layer

**Page format.** The unit of storage is a 4 KB slotted page (`storage/page.h`). A small header
holds the slot count and a free space pointer. The slot directory grows forward from the
header while tuple payloads grow backward from the end of the page:

```
[ num_slots | free_ptr | slot0 | slot1 | ... ->   free   <- ... tupleN | tuple1 | tuple0 ]
```

Each slot is an `(offset, length)` pair. Deleting a tuple sets its slot length to zero (a
tombstone) rather than compacting the page, so a record id stays valid for the life of the row.
This is what lets an index entry keep pointing at the same physical row.

**Heap files.** A table is an unordered collection of pages (`storage/heap_file.h`). Inserts go
to the last page and allocate a new one when it is full. A forward iterator walks every live
slot for sequential scans.

**Buffer pool.** Pages are cached in a fixed set of frames (`storage/buffer_pool.h`). The
eviction policy is the **clock sweep (second chance)** algorithm from Lab 3: each frame has a
reference bit, a hand sweeps the frames in a circle, a set reference bit is cleared and given a
second chance, and the first unpinned frame with a clear bit is evicted (its page is flushed
first if dirty). Pinned pages, which are in active use, are never evicted. This is the same
strategy PostgreSQL uses.

---

## 4. Indexing (B+Tree)

The index (`index/bplus_tree.h`) maps a column value to the record ids of matching rows. It
started as the B-Tree from Lab 4 and became a B+Tree: all data lives in the leaves, leaves are
chained left to right for range scans, and internal nodes hold only separator keys for routing.

**Node structure.** A node is either a leaf (parallel arrays of keys and RID lists, plus a
`next` pointer to the following leaf) or internal (separator keys plus child pointers). Storing
a list of RIDs per key lets a non unique secondary index hold many rows under one key.

**Search path.** A lookup walks from the root, at each internal node choosing the child whose
key range contains the search key, until it reaches a leaf, then scans the leaf for the key.
Insertion splits a full node and pushes a separator up, growing the tree from the root when the
root itself splits (proactive split, like the lab). A range scan finds the low key's leaf and
follows the leaf chain until it passes the high key.

The index is rebuilt from the heap when the database reopens, because an index is derived data;
the heap plus the WAL are the source of truth.

---

## 5. Query Execution

**Parser.** A hand written tokenizer and recursive descent parser (`parser/`, from Lab 5)
produce an AST. Operator precedence is encoded in the grammar: comparisons bind tightest, then
`AND`, then `OR`. It supports `CREATE TABLE`, `CREATE INDEX`, `INSERT`, `SELECT` with `WHERE`,
inner `JOIN`, projection and `SELECT ... FOR UPDATE`, `DELETE`, and `BEGIN/COMMIT/ABORT`.

**Plan generation.** The `Planner` turns the AST into a tree of physical operators and records
an `EXPLAIN` string describing the choices. For a single table query it asks the optimizer for
the access path; for a join it builds a nested loop join with a qualified output schema; it
then wraps a `Filter` for the `WHERE` and a `Project` for the select list.

**Operator execution.** Operators follow the Volcano (iterator) model: every operator exposes
`Init()` and `Next(&tuple)`, and a tree of them streams one row at a time.

| Operator | Role |
|---|---|
| SeqScan | full heap scan |
| IndexScan | B+Tree point lookup, then heap fetch by RID |
| Filter | keep rows passing the `WHERE` predicate |
| Project | select a subset and order of columns |
| NestedLoopJoin | inner equi join, right side materialized |
| Insert / Delete | mutate the heap, maintain indexes, take locks, write WAL |

---

## 6. Optimizer

The optimizer (`planner/optimizer.h`) makes two cost based decisions.

**Access path: index scan vs sequential scan.** It looks for an equality predicate `col = const`
on an indexed column (descending only through `AND`, since an equality under `OR` cannot drive a
single lookup). It then compares costs:

- Sequential scan cost = number of rows (every row is touched).
- Index scan cost = tree height + estimated matching rows, where the estimate uses selectivity
  `1 / num_rows` for a unique index and a default fraction otherwise.

It uses the index only when the index cost is lower, which is why a large table with a primary
key equality uses the index while a tiny table still does a sequential scan.

**Join order.** For a two table join, the smaller table (by row count) becomes the outer,
driving relation so the materialized inner side stays small. The chosen plan is visible in the
`EXPLAIN` output.

---

## 7. Transactions and Concurrency

**Locking strategy.** Concurrency control is **Strict Two Phase Locking** (`txn/`, from Lab 6).
Reads take shared (S) locks, writes take exclusive (X) locks, and `SELECT ... FOR UPDATE` takes
X locks on the rows it reads. All locks are held until commit or abort (the "strict" part),
which gives serializable behavior and avoids cascading aborts.

**Isolation.** Because all conflicting accesses are locked for the whole transaction and
released together at the end, schedules are conflict serializable.

**Deadlock handling.** The lock manager maintains a waits for graph: when a request cannot be
granted, an edge is added from the requester to each holder it is blocked on. Before the
requester sleeps, a depth first search looks for a cycle. If a cycle exists the requesting
transaction backs off and is aborted, which breaks the deadlock; the surviving transaction then
proceeds. The deadlock demo and `test_deadlock` show two transactions locking two rows in
opposite order, with exactly one aborted as the victim and the other committing.

---

## 8. Recovery

**WAL design.** Durability comes from a Write Ahead Log (`recovery/`). It is a logical
redo/undo log: it records DDL (`CREATE TABLE`, `CREATE INDEX`), the row image of every `INSERT`
and `DELETE` tagged with the transaction that did it, and `BEGIN/COMMIT/ABORT` markers. Because
the log is self describing, replaying it from the start fully reconstructs the database.

**Log records.** Each record is length prefixed and stores its type, transaction id, table, and
a self describing payload (the schema for `CREATE TABLE`, the typed row values for
`INSERT/DELETE`). A `COMMIT` flushes the log, so a committed transaction is durable.

**Crash recovery procedure.** On reopen, if the WAL is non empty, MiniDB:
1. Scans the log and computes the set of committed transactions (a transaction counts only if
   it has a `COMMIT` and no `ABORT`; autocommit statements always count).
2. Rebuilds from a clean slate (the WAL is the source of truth): it replays DDL to recreate
   tables and indexes, replays `INSERT`/`DELETE` only for committed transactions, and skips the
   rest.

The effect is the textbook guarantee: committed transactions survive a crash, and the work of
an in flight transaction that never committed is rolled back. The recovery demo and
`test_recovery` show a committed row surviving while an uncommitted insert and an uncommitted
delete both vanish.

---

## 9. Extension Track B: MVCC

**Motivation.** Strict 2PL is simple and correct, but a reader must wait for a writer to finish
because shared and exclusive locks conflict. Under a read heavy workload with occasional long
writes, that blocking destroys read throughput. MVCC removes the conflict entirely.

**Design** (`mvcc/version_store.h`). Every write creates a new *version* of the row instead of
overwriting it. Versions form a newest first chain per key:

```
head -> v3(begin=30) -> v2(begin=20) -> v1(begin=10) -> null
```

Each version records the commit timestamp at which it became visible (`begin_ts`) and the
timestamp at which it was superseded (`end_ts`). A transaction reads as of its snapshot
timestamp `S` and sees the newest committed version with `begin_ts <= S`. Uncommitted versions
(an in flight writer) and versions committed after `S` are invisible, and a delete version
hides the row going forward. Commit stamps a transaction's pending versions with the commit
timestamp; abort marks them never visible. This is the version chain idea from Lab 6 made
timestamp ordered with proper snapshot visibility.

**Results.** Because a reader walks the version chain without taking a row lock, readers never
block writers and writers never block readers. Measured under contention (8 readers on a hot row
while a writer holds it for 150 ms):

| | Reads in window | Latency of a read during the write |
|---|---|---|
| Strict 2PL | 1,972,936 | 148.1 ms (blocked) |
| MVCC | 4,869,696 | 0.001 ms (never blocked) |

MVCC completed about 2.5x more reads and eliminated the blocking. See `benchmarks/BENCHMARKS.md`.

---

## 10. Benchmarks

Full methodology and numbers are in [`benchmarks/BENCHMARKS.md`](benchmarks/BENCHMARKS.md).
Two harnesses, run on an Apple M4 with clang 17 at `-O2`:

- **Index vs sequential scan** (`bench_index_vs_scan`): on 50,000 rows, a primary key equality
  served by the B+Tree runs at 0.0018 ms per query versus 3.49 ms for a full scan, about a
  **1949x** speedup. This is the result the optimizer is exploiting.
- **MVCC vs 2PL under contention** (`bench_mvcc_vs_2pl`): a read issued while a write is in
  flight blocks for **148 ms** under 2PL but returns in **0.001 ms** under MVCC, and MVCC does
  about **2.5x** more reads in the observation window.

---

## 11. Limitations

This is a teaching engine, so several real database features are deliberately out of scope:

- **No `UPDATE` statement.** Updates are expressed as delete plus insert. `SELECT ... FOR
  UPDATE` exists to take write locks for the concurrency demo.
- **Index keys.** The B+Tree handles the supported scalar types; it is not a composite or
  covering index, and indexes are rebuilt on reopen rather than persisted.
- **No version garbage collection.** The MVCC store keeps every version. A production system
  would prune versions no longer visible to any active snapshot.
- **MVCC is a separate subsystem.** The engine's SQL path runs on heap storage with 2PL; the
  MVCC store demonstrates Track B and is documented as the path to replacing 2PL engine wide.
- **WAL growth.** Recovery replays the whole log; there is no checkpoint compaction yet, so the
  WAL grows over time.
- **Single process, in memory catalog.** The catalog is rebuilt from the WAL on reopen rather
  than stored separately, and orphaned data pages are not reclaimed after a rebuild.

**Future improvements.** `UPDATE`, persisted indexes, version GC, full MVCC integration into the
executor, WAL checkpointing, and aggregates / `GROUP BY` / `ORDER BY`.

---

## 12. How to Run

**Dependencies.** A C++17 compiler (clang or g++) and CMake 3.15+. No external libraries; the
standard library only.

**Build.**

```bash
cmake -B build -S .
cmake --build build -j
```

**Run the tests** (7 suites covering storage, index, SQL, optimizer, deadlock, recovery, MVCC):

```bash
cd build && ctest --output-on-failure
```

**Run the scripted demo** (walks through all six core features plus MVCC):

```bash
./build/minidb --demo
```

**Run the benchmarks:**

```bash
./build/bench_index_vs_scan
./build/bench_mvcc_vs_2pl
```

**Interactive shell:**

```bash
./build/minidb mydb.db
minidb> CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR, age INT)
minidb> INSERT INTO students VALUES (1, 'alice', 20), (2, 'bob', 23)
minidb> SELECT name FROM students WHERE age >= 22
minidb> EXPLAIN SELECT * FROM students WHERE id = 1
minidb> BEGIN
minidb> SELECT * FROM students WHERE id = 1 FOR UPDATE
minidb> COMMIT
minidb> \q
```
