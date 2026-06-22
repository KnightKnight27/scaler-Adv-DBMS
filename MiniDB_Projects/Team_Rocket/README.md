# MiniDB — A Relational Database Engine from Scratch

> Advanced DBMS Capstone Project · Extension **Track A — Performance (Columnar / Vectorized Execution)**

MiniDB is a small but complete relational database engine written from scratch
in C++17. It integrates the components studied across the course — a page-based
storage engine, a B+ tree index, a SQL parser and Volcano-model executor, a
cost-based optimizer, strict two-phase-locking transactions with deadlock
prevention, and write-ahead-logging crash recovery — into one coherent system
driven by a SQL shell. For the extension track it adds a **columnar, vectorized
execution path** and benchmarks it head-to-head against the row-oriented engine.

---

## Team Information

**Team Name:** Rocket

| Full Name | Scaler Email | Roll Number |
|-----------|--------------|-------------|
| Mayank Gupta | mayank.24bcs10220@sst.scaler.com | 24BCS10220 |
| Tanish Kothari | tanish.24bcs10008@sst.scaler.com | 24BCS10008 |
| Rajveer Bishnoi | rajveer.24bcs10404@sst.scaler.com | 24BCS10404 |

_Submitted as PR title_ `TEAM_Rocket` _to_ <https://github.com/KnightKnight27/scaler-Adv-DBMS>
_at_ `MiniDB_Projects/Team_Rocket/`.

---

## 1. Project Overview

**Problem statement.** Build a working relational database engine that
demonstrates how real databases are engineered internally — not a feature
checklist, but a correct, integrated system whose every layer can be explained.

**Goals.**
- One engine, end-to-end: `SQL text → parse → optimize → execute → storage`.
- Durable, recoverable storage with ACID transactions.
- A real cost-based decision (index scan vs table scan, join order).
- A second, read-optimized execution path (columnar + vectorized) to study a
  fundamental storage/execution trade-off.

**Chosen extension track: A — Performance.** We implemented columnar storage and
a vectorized (batched) execution path and benchmarked it against the row engine
on an analytical aggregate, measuring the speedup and verifying identical
results.

---

## 2. System Architecture

```
                         ┌─────────────────────────┐
   SQL text  ──────────► │ query/  Lexer + Parser   │  → Statement (AST)
                         └────────────┬────────────┘
                                      │
                         ┌────────────▼────────────┐
                         │ query/  Cost-based       │  selectivity,
                         │   Optimizer              │  scan choice, join order
                         └────────────┬────────────┘
                                      │ operator tree
                         ┌────────────▼────────────┐
                         │ query/  Volcano executor │  SeqScan / IndexScan /
                         │   open() + next()        │  Filter / Join / Project
                         └──────┬─────────────┬─────┘
                                │             │
                ┌───────────────▼──┐   ┌──────▼──────────────┐
                │ index/ B+ tree   │   │ storage/ HeapFile   │  rows in
                │  key → RID       │   │  (slotted pages)    │  slotted pages
                └────────┬─────────┘   └──────────┬──────────┘
                         │                        │
                         └──────────┬─────────────┘
                          ┌─────────▼──────────┐   ┌───────────────────┐
                          │ storage/ BufferPool │   │ txn/ LockManager  │ 2PL +
                          │  (LRU, pin/unpin)   │   │  (wound-wait)     │ deadlock
                          └─────────┬──────────┘   └───────────────────┘
                          ┌─────────▼──────────┐   ┌───────────────────┐
                          │ storage/ DiskManager│   │ recovery/ WAL +   │ redo/undo
                          │   (4 KB pages)      │   │  recover()        │ on restart
                          └─────────┬──────────┘   └───────────────────┘
                                    ▼
                              <db>.data  +  <db>.meta  +  <db>.wal

   Extension:  columnar/  ColumnStore (contiguous columns) → vectorized batch
               execution  (alternative read path; benchmarked vs the row engine)
```

**Major modules** (`src/minidb/`):

| Module | Responsibility |
|--------|----------------|
| `types.h` | shared types (`Value`, `Tuple`, `RID`, `Schema`), row ↔ bytes serialization |
| `storage/` | `DiskManager` (page manager), slotted `Page`, `BufferPool` (LRU), `HeapFile` |
| `index/` | `BPlusTree` — primary-key index (search / insert / delete) |
| `catalog/` | table/column metadata + page lists, persisted to `<db>.meta` |
| `query/` | `Parser` (lexer + recursive descent), Volcano operators, cost-based `Optimizer`, `Executor` |
| `txn/` | `LockManager` (strict 2PL + wound-wait), `Transaction` |
| `recovery/` | `LogManager` — WAL records + redo/undo recovery |
| `columnar/` | `ColumnStore` + vectorized batch execution (Track A) |
| `db.h` | `Database` — ties everything together, dispatches SQL, runs recovery |

**Data flow.** `Database::execute(sql)` parses one statement; a `SELECT` goes
through the optimizer to build a Volcano operator tree pulled with
`open()/next()`; `INSERT`/`DELETE` acquire locks, append a WAL record
(write-ahead), then mutate the heap and index. All page access passes through
the buffer pool to the single `<db>.data` file.

---

## 3. Storage Layer

**Page format.** Fixed **4 KB** pages, addressed by `page_id` at byte offset
`page_id * 4096` in one data file (`DiskManager`, the page manager). A heap page
is **slotted**:

```
[ num_slots | free_end | lsn ][ slot0 | slot1 | ... ]
                                     ... free space ...
                       [ ...... tuple1 ][ tuple0 ]
```

Slots `(offset, length)` grow forward from the header; tuple data grows backward
from the end. A delete sets a slot's offset to `-1` (a tombstone) but never
removes the slot, so a row's `RID = (page_id, slot)` stays stable for the index
and for recovery to point at. The header carries a page **LSN** used to enforce
the write-ahead rule.

**Heap files.** A `HeapFile` is the list of pages owned by a table (the list
lives in the catalog). Inserts append into the most recent page and allocate a
new page when it fills, keeping insert O(1); a scan walks every live slot.

**Buffer pool.** `BufferPool` caches a fixed number of 4 KB frames. Pages are
**pinned** while in use; only unpinned pages are evicted, chosen by
**least-recently-used** order, and **dirty pages are written back** before their
frame is reused. Before writing any dirty page it forces the log up to that
page's LSN — the single choke point that enforces write-ahead logging.

---

## 4. Indexing

**B+ tree design.** `index/btree.h` maps an `int64` primary key → `RID`.
Internal nodes hold only separator keys; all values live in a linked list of
leaves, so range traversal follows leaf links. Each table's first integer
column automatically receives a primary index.

**Operations.**
- **Search** — descend from the root, choosing the child by separator keys,
  until a leaf, then locate the key.
- **Insert** — insert into the leaf; a full node **splits** and pushes a
  separator up, creating a new root when the old root splits.
- **Delete** — remove from the leaf; a node that falls below half capacity
  **borrows** from a sibling or **merges** with one, pulling the separator down.

The optimizer uses this tree for equality predicates on the indexed column (see
§6). `minidb_tests` stresses 2,000 keys through insert / search / delete with
multi-level splits, borrows, and merges.

---

## 5. Query Execution

**Parser.** `query/parser.h` is a hand-written lexer + recursive-descent parser
for the supported subset:

```sql
CREATE TABLE t (col INT, col TEXT, ...);
INSERT INTO t VALUES (..., ...);
SELECT * | col,... FROM t [JOIN t2 ON t.a = t2.b] [WHERE col <op> value];
DELETE FROM t [WHERE col <op> value];
BEGIN; COMMIT; ABORT;
```

`<op>` is one of `= != < > <= >=`. It produces a `Statement` AST.

**Operator execution — Volcano (iterator) model.** Every operator implements
`open()` and `next(Row&)`; the root is pulled until exhausted. Operators:
`SeqScan`, `IndexScan`, `Filter`, `Project`, and `NestedLoopJoin` (materializes
the inner side once, streams the outer). `INSERT`/`DELETE` are executed directly
by the engine with locking + WAL. Predicate columns are resolved against the
operator's output schema (qualified `table.col` names are matched by suffix).

---

## 6. Optimizer

`query/optimizer.h` is cost-based.

**Selectivity estimation.** Equality predicates are estimated at `0.10`, range
predicates at `0.33`, inequality at `0.90`. Estimated cardinality is
`rows × selectivity`.

**Scan choice (table scan vs index scan).** When an equality predicate hits the
indexed column, the optimizer chooses an **index scan**; otherwise it uses a
**sequential scan** and pushes the predicate down as a `Filter`. Every plan
prints its reasoning (shown in the shell above results):

```
index scan on users (key 2, selectivity 0.10)

seq scan on users + filter (selectivity 0.33)
```

**Join ordering.** For a two-table join the nested-loop cost is dominated by
`|outer| × |inner|`, so the optimizer makes the **smaller estimated relation the
outer** one and reports the decision:

```
join order: outer=users (~3 rows), inner=orders
nested-loop join on users.id = orders.user_id
```

---

## 7. Transactions & Concurrency

**Locking strategy — Strict 2PL.** `txn/lock_manager.h` provides
shared/exclusive locks keyed per row (`table:page:slot`). Reads take `SHARED`,
writes take `EXCLUSIVE`, and **all locks are held until commit/abort** (strict
2PL), which yields **serializable** schedules and makes rollback safe (no other
transaction can have read an uncommitted write).

**Deadlock handling — wound-wait.** A transaction's id doubles as its timestamp,
so a smaller id is "older." When a transaction wants a lock held incompatibly:
an **older** requester aborts ("wounds") the holder and proceeds; a **younger**
requester aborts itself. No cycle of waiters can form, so deadlock is impossible
without running cycle detection. The transaction test demonstrates an older
transaction wounding a younger holder and a younger requester dying.

The SQL engine exposes this via `BEGIN` / `COMMIT` / `ABORT`; each write takes
an exclusive lock, and `ABORT` undoes the transaction's applied row operations.

---

## 8. Recovery

**WAL design.** `recovery/wal.h` is an append-only binary log (`<db>.wal`). The
**write-ahead rule** is honored by the buffer pool (the log is forced before any
page it describes reaches disk), and the log is flushed before a `COMMIT` is
reported, so a committed transaction is always recoverable.

**Log records.** `BEGIN`, `INSERT` (table, RID, *after-image*), `DELETE`
(table, RID, *before-image*), `COMMIT`, `ABORT`. Before- and after-images make
both redo and undo possible.

**Crash recovery procedure.** On startup `Database::recover()`:
1. **Analysis** — scan the log; a transaction is a *winner* iff it has a
   `COMMIT` record.
2. **Redo** — replay every logged `INSERT`/`DELETE` in order (repeating
   history). Redo is idempotent because it writes each tuple back at its
   original RID.
3. **Undo** — reverse every loser's operations from newest to oldest.

Indexes and row counts are then rebuilt from the recovered base data. The
`CRASH;` shell command (and the recovery test) drops the buffer pool to simulate
a crash and verifies committed rows survive while uncommitted work is rolled
back.

---

## 9. Extension Track — A: Columnar / Vectorized Execution

**Motivation.** The row engine decodes a whole tuple per row and pulls one row
at a time through the operator tree. Analytical queries that touch few columns
over many rows pay for decoding columns they never read and for per-row
iterator overhead. A **columnar** layout stores each column contiguously, and
**vectorized** execution processes values in batches — a different and important
point in the execution design space.

**Design** (`columnar/`):
- **ColumnStore** — stores each column's values in a contiguous array, so a
  query reads only the columns it needs instead of decoding whole rows.
- **Vectorized execution** — the aggregate is computed in fixed **batches of
  1024** values over contiguous memory; each batch is a tight loop the compiler
  can vectorize, instead of one virtual `next()` call per row.

**Benchmark.** The same workload — the total of `amount` over rows where
`region < 5` — is run through the row engine and the columnar/vectorized path on
identical data, and the two answers are checked equal.

**Results.** On 200k rows the columnar path is **~270×** faster (and up to
**~430×** on smaller sets), with identical results — the textbook
analytical-scan win for columnar over row storage. Full numbers in
[`benchmarks/results.md`](benchmarks/results.md).

---

## 10. Benchmarks

**Setup.** Apple clang `-O2`, release build; `./build/bench_columnar <rows>`.
Identical 3-column table loaded into both the row engine and the column store;
the row path executes `SELECT amount FROM sales WHERE region < 5` and sums the
result, the columnar path runs the vectorized batch sum.

| Rows | Row engine | Columnar + vectorized | Speedup |
|------|-----------:|----------------------:|--------:|
| 50,000 | 42.2 ms | 0.10 ms | ~430× |
| 100,000 | 78.0 ms | 0.23 ms | ~339× |
| 200,000 | 154.1 ms | 0.57 ms | ~269× |

**Analysis.** The columnar path wins by (1) reading only the two needed columns
instead of decoding full tuples, (2) operating directly on `int64` arrays with
no per-row deserialization, and (3) summing contiguous memory in batches that
are cache-friendly and vectorizable. The row layout remains the right default
for point lookups and whole-row access; columnar is the read-optimized path for
analytical scans — that trade-off is the lesson of the extension.

---

## 11. Limitations

- **Index keys are `int64`.** The primary-key B+ tree indexes the first integer
  column only; secondary indexes are not implemented.
- **In-memory index.** The B+ tree is held in memory and rebuilt from the base
  data at startup (the base data, not the index, is the source of truth).
- **SQL subset.** No `UPDATE`, `GROUP BY`, `ORDER BY`, aggregates, sub-queries,
  or multi-way (>2 table) joins; only single-predicate `WHERE`.
- **Single-connection shell.** 2PL and wound-wait are exercised by the
  transaction tests; the shell executes statements serially.
- **Heap space reuse.** Inserts append to the latest page and do not reclaim
  space freed by deletes in earlier pages.
- **Log growth.** The WAL is not truncated at a checkpoint, so recovery replays
  the full history each startup.
- **Crash model** is process crash (the buffer pool is dropped); the WAL is
  forced at commit so committed work survives.

---

## 12. How to Run

**Dependencies.** A C++17 compiler (Apple clang / g++) and CMake ≥ 3.15. No
external libraries.

```bash
# Build the engine, shell, tests, and benchmark
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run the interactive shell (creates <db>.data / .meta / .wal)
./build/minidb demo
```

```sql
minidb> CREATE TABLE users (id INT, name TEXT, age INT);
minidb> INSERT INTO users VALUES (1, 'alice', 30);
minidb> INSERT INTO users VALUES (2, 'bob', 25);
minidb> SELECT * FROM users WHERE id = 2;       -- uses the index
minidb> SELECT name, age FROM users WHERE age > 28;
minidb> BEGIN;
minidb> INSERT INTO users VALUES (3, 'carol', 40);
minidb> ABORT;                                  -- carol is undone
minidb> CRASH;                                  -- drop buffer pool, recover from WAL
```

```bash
# Run the full test suite (storage, index, WAL, transactions, recovery)
./build/minidb_tests

# Run the Track A benchmark (columnar/vectorized vs row engine)
./build/bench_columnar 200000

# Or run the scripted end-to-end demo
./build/minidb demo < docs/demo.sql
```
