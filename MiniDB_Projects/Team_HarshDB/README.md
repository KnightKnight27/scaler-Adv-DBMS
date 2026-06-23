# MiniDB — A Working Relational Database Engine (Capstone Project)

**Team Name:** Team_HarshDB

**Team Members**

| Name | Roll Number | Scaler Email |
|------|-------------|--------------|
| Harsh Kumar | 23BCS10021 | harsh.23bcs10021@sst.scaler.com |

> Solo submission. (The brief recommends 2–4 members; this project was built and
> is understood end-to-end by the single author for the viva.)

**Chosen Extension Track:** **Track B — Concurrency (MVCC)**

---

## 1. Project Overview

### Problem statement
A relational database is not one clever algorithm — it is a stack of cooperating
subsystems (storage, indexing, query processing, concurrency control, recovery),
each making trade-offs that only make sense in the context of the others. The
goal of this capstone is to build that stack from scratch, small but *real*, and
to be able to explain why each piece is shaped the way it is.

MiniDB is a single-file, page-based relational engine written in C++17. It speaks
a useful subset of SQL, stores data in 4 KB pages through a buffer pool, indexes
primary keys with a B+ tree, plans queries with a cost-based optimizer, runs
transactions under Strict Two-Phase Locking with deadlock detection, survives
crashes via a Write-Ahead Log, and — for the extension track — provides
multi-version concurrency control (MVCC) so that readers never block writers.

### Goals
- Integrate every core component from the lab modules into one coherent engine.
- Keep each subsystem small enough to read and defend, but genuinely functional.
- Demonstrate correct behaviour (snapshot isolation, deadlock detection, crash
  recovery) and measure the trade-offs (index vs scan, MVCC vs 2PL).

### What works (quick tour)
```sql
CREATE TABLE students (id INT PRIMARY KEY, name TEXT, age INT);
INSERT INTO students VALUES (1, 'alice', 20);
SELECT name FROM students WHERE age > 18;
EXPLAIN SELECT name FROM students WHERE id = 1;     -- shows Index Scan
SELECT students.name, enroll.course FROM students
       JOIN enroll ON students.id = enroll.sid;     -- nested-loop join
BEGIN; DELETE FROM students WHERE id = 1; COMMIT;   -- transactional write
```

---

## 2. System Architecture

MiniDB is layered. A SQL string enters at the top and flows down to 4 KB pages
on disk; results flow back up. Every write also flows sideways into the WAL
before it is considered durable.

```
                         ┌──────────────────────────┐
        SQL string  ───► │  Lexer → Parser (AST)     │   src/sql/
                         └────────────┬─────────────┘
                                      ▼
                         ┌──────────────────────────┐
                         │  Optimizer (access path,  │   src/optimizer/
                         │  selectivity, join order) │
                         └────────────┬─────────────┘
                                      ▼
                         ┌──────────────────────────┐
                         │  Executor (scan, filter,  │   src/execution/
                         │  project, nested-loop join│
                         │  insert, delete)          │
                         └───┬───────────────┬───────┘
              reads/visibility│               │ writes (locks + WAL)
                   ▼          ▼               ▼
        ┌───────────────┐ ┌──────────────┐ ┌───────────────┐
        │ Txn Manager   │ │ B+ Tree index│ │ Lock Manager  │   src/txn/, src/index/
        │ (MVCC xmin/   │ │ key -> RID   │ │ (2PL+deadlock)│
        │  xmax, snaps) │ └──────┬───────┘ └───────────────┘
        └──────┬────────┘        │
               │                 ▼
               │        ┌──────────────────┐        ┌───────────────┐
               └──────► │  Heap File        │ ◄────► │  WAL          │  src/recovery/
                        │ (tuples in slots) │        │ (redo log)    │
                        └─────────┬─────────┘        └───────────────┘
                                  ▼
                        ┌──────────────────┐
                        │  Buffer Pool      │   src/storage/buffer_pool.h
                        │ (ClockSweep)      │
                        └─────────┬─────────┘
                                  ▼
                        ┌──────────────────┐
                        │  Disk Manager     │   src/storage/disk_manager.h
                        │  one file, 4KB pgs│
                        └──────────────────┘
```

**Data flow for `SELECT name FROM students WHERE id = 5`:**
parse → optimizer sees equality on the PK and picks an **Index Scan** → executor
asks the B+ tree for the RID of key 5 → fetches that page through the buffer pool
→ checks MVCC visibility → evaluates the WHERE predicate → projects `name` → returns.

**Module map**

| Module | Files | Responsibility |
|--------|-------|----------------|
| Common | `src/common.h` | Value/Row/Schema/RID types |
| Storage | `src/storage/*` | disk manager, slotted pages, heap files, buffer pool |
| Index | `src/index/bplus_tree.h` | B+ tree (key → RID) |
| SQL | `src/sql/*` | lexer, AST, recursive-descent parser |
| Catalog | `src/catalog/catalog.h` | table metadata, index registry, persistence |
| Optimizer | `src/optimizer/optimizer.h` | access-path & join-order choice |
| Execution | `src/execution/executor.h` | physical operators |
| Transactions | `src/txn/*` | MVCC visibility + 2PL lock manager |
| Recovery | `src/recovery/wal.h` | write-ahead log + redo |
| Engine | `src/database.h`, `main.cpp` | orchestration + REPL |

---

## 3. Storage Layer

### One file, many pages
The whole database lives in a single file (`<name>.db`) divided into fixed
**4 KB pages**, exactly like the SQLite layout studied in the labs. Page *N* sits
at byte offset *N × 4096*. `DiskManager` (`src/storage/disk_manager.h`) is the
only code that issues `read()`/`write()`; it knows nothing about tuples.

### Slotted page format
Each page (`src/storage/page.h`) uses the classic **slotted** layout:

```
0          12                                                  4096
+----------+-----------+----------------------------+-----------+
| header   | slot dir  |        free space          | tuples    |
| 12 bytes | 8 B/slot  |   (grows  ->   <-  grows)  | (grow <-) |
+----------+-----------+----------------------------+-----------+
header  = { next_page_id, num_slots, free_ptr }
slot    = { offset, length }   (length = -1 means a deleted/tombstoned slot)
```

The slot directory grows downward from the header while tuple bytes grow upward
from the end. Variable-length TEXT values are handled naturally because each slot
records its own length. A slot number is never reused, so a record id
`RID = (page_id, slot)` is a stable physical address.

### Heap files
A table is a **heap**: a singly linked chain of pages (`next_page_id`).
`HeapFile` (`src/storage/heap_file.h`) serialises a row — prefixed with its MVCC
header — into bytes and inserts it on the last page, allocating and linking a new
page when the current one is full. Each stored tuple is:

```
[ int64 xmin ][ int64 xmax ][ col0 ][ col1 ] ...
```

### Buffer pool (ClockSweep)
`BufferPool` (`src/storage/buffer_pool.h`) caches pages in a fixed set of frames.
`fetch_page` pins and returns a page (loading on a miss); callers `unpin` when
done. When all frames are full, a victim is chosen with **ClockSweep** (the
second-chance approximation of LRU used by PostgreSQL): a circular hand sweeps
frames, decrementing each `usage_count`, and evicts the first unpinned frame whose
count reaches zero. Dirty victims are written back on eviction.

> **Demonstrated:** page allocation (`allocate_page`), page reads/writes
> (`fetch_page`/`flush_page`), and buffer-pool reuse under load (Benchmark 1
> loads 20 000 rows across many pages through a bounded pool).

---

## 4. Indexing

### B+ tree design
`BPlusTree` (`src/index/bplus_tree.h`) maps an integer key to an `RID`. It is a
true **B+ tree**, not a plain B-tree:

- **All data entries live in the leaves**; internal nodes hold only separator
  keys that route a search downward.
- **Leaves are singly linked** left-to-right, so a range scan is a cheap walk.
- **Insert performs real node splits** (the "page split" concept) — when a node
  exceeds `order-1` keys it splits and promotes a separator to its parent,
  growing the tree by one level at the root when necessary. This keeps the tree
  balanced and shallow (a 20 000-key tree is only a handful of levels deep).

### Node structure
```cpp
struct Node {
    bool                 leaf;
    std::vector<int64_t> keys;
    std::vector<RID>     rids;     // leaves only: parallel to keys
    std::vector<Node*>   children; // internal only: keys.size()+1 of them
    Node*                next;     // leaf-to-leaf chain
};
```

### Search path
`search(key)` walks from the root, at each internal node using `upper_bound` on
the separator keys to pick the child, until it reaches a leaf, then finds the key
there and returns its `RID`. `range(low, high)` finds the leaf for `low` and walks
the `next` pointers. Delete uses lazy removal at the leaf (the entry is removed;
underflowing nodes are not merged — search/insert stay correct because the
range-partition invariant holds). Node merging is the natural extension.

> **Index utilisation:** the executor calls `index->search(key)` whenever the
> optimizer selects an Index Scan, turning `WHERE id = 5` into a few pointer hops
> instead of a full heap scan (see Benchmark 1).

---

## 5. Query Execution

### Parser
`src/sql/lexer.h` tokenises the SQL string; `src/sql/parser.h` is a hand-written
recursive-descent parser producing an AST (`src/sql/ast.h`). Supported grammar:

- `CREATE TABLE t (col INT|TEXT [PRIMARY KEY], ...)`
- `INSERT INTO t VALUES (...)`
- `SELECT cols|* FROM t [JOIN t2 ON a = b] [WHERE expr]`
- `DELETE FROM t [WHERE expr]`
- `BEGIN | COMMIT | ABORT`, and an `EXPLAIN` prefix on SELECT

WHERE/ON expressions use the precedence ladder `OR → AND → comparison →
( expr ) | operand OP operand`, the structured equivalent of the Lab 5
Shunting-Yard evaluator.

### Plan generation & operator execution
For a query the executor (`src/execution/executor.h`):
1. Asks the optimizer for an **access path** on the driving table (seq vs index).
2. Pulls tuples from the heap, applying **MVCC visibility** (or 2PL shared locks).
3. For a join, runs a **nested-loop join** (outer = smaller relation) evaluating
   the ON predicate on the merged row.
4. Applies the **WHERE filter**, then **projects** the requested columns.

Writes go through the same engine: `INSERT` serialises the row into the heap,
updates the index, takes an exclusive lock, and logs to the WAL; `DELETE` finds
the matching visible tuples, locks them, stamps `xmax`, and logs the delete.

Results are materialised into vectors — the datasets here are small enough that
clarity beats a streaming iterator (Volcano) model, which is the obvious upgrade.

---

## 6. Optimizer

`src/optimizer/optimizer.h` implements a small **cost-based** optimizer.

### Selectivity & cost estimation
- **Cardinality** of a table ≈ its tuple count; **page cost** of a sequential
  scan ≈ its page count.
- An **equality predicate on the indexed primary key** has selectivity ≈ 1 row,
  with cost ≈ `index_height + 1`.

### Access-path selection
`choose_access(table, where)` scans the (conjunctive) predicate for
`pk = <literal>`. If found and an index exists, it compares the index cost to the
sequential-scan cost and chooses the **Index Scan**; otherwise it falls back to a
**Sequential Scan**. `EXPLAIN` prints the decision and the estimates:

```
EXPLAIN SELECT name FROM students WHERE id = 5;
QUERY PLAN
  Index Scan on students
    -> equality on primary key 'id' -> index scan (cost 3.0 vs seq scan cost ...)
    -> est_rows=1 est_cost=3
```

### Join ordering
For a two-table join the optimizer makes the **smaller relation the outer loop**
of the nested-loop join (`should_swap_join`), minimising how many times the inner
relation is rescanned — the same principle a real planner applies, just with
simpler statistics.

---

## 7. Transactions & Concurrency

### Locking strategy — Strict 2PL
`src/txn/lock_manager.h` implements **Strict Two-Phase Locking** at row
granularity (lock key = `table#pk`). Shared (read) locks coexist; an exclusive
(write) lock conflicts with everything. Locks are only acquired during the
*growing* phase; `release_all` at commit/abort is the instantaneous *shrinking*
phase, which avoids cascading aborts.

### Isolation guarantees
- **Core (2PL) mode:** reads take shared locks and writes take exclusive locks
  held until commit → **serializable** behaviour for the locked rows.
- **Extension (MVCC) mode:** reads use a snapshot and take no locks → **snapshot
  isolation**; writes still take exclusive locks to serialise write–write
  conflicts.

### Deadlock handling
Before a transaction blocks on a lock, the manager records a **waits-for edge**
and runs a **DFS cycle check**. A cycle means deadlock, so it throws
`DeadlockException`; the engine aborts that transaction, releasing its locks and
breaking the cycle. (Demo step 5 shows two transactions deadlocking and exactly
one being aborted.)

---

## 8. Recovery

### WAL design (no-force / redo)
`src/recovery/wal.h` is the Write-Ahead Log. The rule: a record describing a
change is appended (and, at COMMIT, flushed) **before** the change is considered
durable. MiniDB uses a **no-force / redo** discipline — dirty data pages may
still be in the buffer pool at commit time, because the committed WAL record is
enough to reconstruct the change after a crash.

### Log records
One record per line (fields separated by `0x1f`, values tagged `I`/`S`):
```
BEGIN  <txid>
INSERT <txid> <table> <pk> <ncols> <v0> <v1> ...
DELETE <txid> <table> <pk>
COMMIT <txid>          <-- flushed to disk: the durability point
ABORT  <txid>
```

### Crash recovery procedure
On startup `Database::recover()`:
1. Reads the whole WAL and collects the set of **committed** transaction ids.
2. **Re-registers** those ids as committed in the transaction manager (so their
   `xmin`/`xmax` stamps are honoured again) and advances the id counter.
3. **REDO:** replays each committed `INSERT`/`DELETE` idempotently (an insert is
   skipped if the PK is already present and live), then flushes.

Uncommitted transactions have no COMMIT record, so their effects are never
replayed and remain invisible (MVCC visibility also depends on commit status).

> **Demonstrated (demo step 7):** a transaction commits rows 1 & 2, another
> inserts row 3 without committing, then `simulate_crash()` drops all dirty pages
> *without* flushing. On reopen, recovery restores rows 1 & 2 and row 3 is gone.

---

## 9. Extension Track — B: Concurrency (MVCC)

### Motivation
Under pure 2PL, readers and writers block each other: a reader needs a shared
lock that conflicts with a writer's exclusive lock. For read-heavy, contended
workloads that serialisation is the bottleneck. **MVCC removes read–write
blocking** by keeping multiple versions of a row and letting each transaction
read from a consistent snapshot.

### Design
Every stored tuple carries `xmin` (creating txn) and `xmax` (deleting/superseding
txn). Each transaction gets a **snapshot** at `begin()`. The visibility rule
(`src/txn/transaction.h`):

```
visible(v) := (v.xmin == me OR (committed(v.xmin) AND v.xmin < snapshot))
          AND (v.xmax == 0  OR v.xmax == me  OR NOT (committed(v.xmax) AND v.xmax < snapshot))
```

- **INSERT** writes a version with `xmin = me, xmax = 0`.
- **DELETE** stamps `xmax = me` on the visible version (the old bytes stay for
  other snapshots).
- **UPDATE** = delete + insert (stamp old `xmax`, append new version).
- **ABORT** needs no physical undo: because visibility checks commit status, an
  aborted transaction's versions are simply never visible.

Readers in MVCC mode take **no locks**; writers still take exclusive locks so
write–write conflicts remain serialised. The mode is switchable at runtime
(`.mvcc on|off` in the REPL), which is exactly what the benchmark toggles.

### Results
Snapshot isolation is shown live in demo step 4 (a reader does not see a row
committed by another transaction *after* the reader's snapshot was taken, while a
fresh transaction does). The throughput benefit is quantified in Benchmark 2
below.

---

## 10. Benchmarks

**Setup:** Apple Silicon (arm64), Apple Clang, `-O2`. Single-file database in
`/tmp`. Numbers vary run to run; representative figures from `./minidb_bench`:

### Benchmark 1 — Index Scan vs Sequential Scan
20 000-row table; 300 random point lookups.

| Access path | Total | Per query |
|-------------|-------|-----------|
| Index Scan (`WHERE id = k`, PK) | ~576 ms | ~1.9 ms |
| Sequential Scan (`WHERE val = k`, unindexed) | ~1478 ms | ~4.9 ms |

**~2.5× faster** with the index. The gap grows with table size, because the seq
scan is O(rows) per query while the index scan is O(tree height).

### Benchmark 2 — MVCC vs 2PL read throughput (extension track)
4 reader threads + 1 writer hammering the **same hot row** for 500 ms.

| Mode | Reads completed | Reads/sec |
|------|-----------------|-----------|
| MVCC | ~42 700 | ~85 000 |
| 2PL  | ~36 000 | ~72 000 |

**MVCC sustains ~1.2× the read throughput** because its readers never block on
the writer's exclusive lock. The advantage widens as the writer holds its lock a
larger fraction of the time.

### Benchmark 3 — Write throughput
20 000 INSERTs in one transaction: ~45 ms → **~430 000 inserts/sec** (in-memory
buffer pool, WAL appended per row).

### Analysis
The three results map directly onto three design decisions: indexing trades a
little write/maintenance cost for a large read speedup on selective queries; MVCC
trades extra storage (version chains) for non-blocking reads; and batching writes
in one transaction amortises per-commit WAL flush cost.

---

## 11. Limitations

- **Types:** only `INT` (64-bit) and `TEXT`. No NULLs, no floats, no constraints
  beyond a single `INT PRIMARY KEY`.
- **Indexes:** B+ tree is in-memory (rebuilt from the heap at startup) and uses
  *lazy* delete (no node merging). Only the primary key is indexed.
- **Joins:** single nested-loop `JOIN` (two tables, equi-join); no hash/merge
  join, no aggregation/GROUP BY/ORDER BY in the engine.
- **MVCC garbage:** dead versions accumulate; there is no VACUUM to reclaim them.
- **Recovery:** redo-only with logical replay keyed on the primary key; no
  physiological logging, no checkpointizing of the WAL, no undo pass (not needed
  given the no-force/visibility-by-commit design).
- **Concurrency:** row locks are keyed by primary key; tables without a PK get
  weaker locking.

### Future improvements
On-disk persistent B+ tree, B+ tree node merging, hash joins + aggregation, a
Volcano-style streaming executor, WAL checkpoints + log truncation, and a VACUUM
process for dead tuples.

---

## 12. How to Run

### Dependencies
A C++17 compiler (`clang++` or `g++`) and, optionally, `make`. No third-party
libraries.

### Build
```bash
cd MiniDB_Projects/Team_HarshDB
make                 # builds ./minidb and ./minidb_bench
# or, without make:
./build.sh
```

### Run the scripted demo (every feature, incl. crash recovery)
```bash
./minidb demo
```

### Interactive SQL REPL
```bash
./minidb                       # data persists to ./minidb_data.{db,wal,catalog}
# or choose a name:  ./minidb mydata
```
Example session:
```sql
CREATE TABLE students (id INT PRIMARY KEY, name TEXT, age INT);
INSERT INTO students VALUES (1, 'alice', 20);
INSERT INTO students VALUES (2, 'bob', 25);
SELECT * FROM students;
EXPLAIN SELECT name FROM students WHERE id = 2;
BEGIN
INSERT INTO students VALUES (3, 'carol', 19);
ABORT
SELECT * FROM students;        -- carol is gone (rollback)
.mvcc off                      -- switch reads to 2PL locking
.tables
.exit
```

### Run the benchmarks
```bash
./minidb_bench
```

### Clean
```bash
make clean
```
