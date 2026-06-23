# MiniDB — a relational database engine with MVCC

**Team:** Team-Lock&Roll
**Extension track:** **B — Concurrency (Multi-Version Concurrency Control)**

MiniDB is a small but complete relational database engine written from scratch in
C++17. It brings together a page-based storage engine, a B+ tree index, a SQL parser, a
cost-based optimizer, a Volcano execution engine, ACID transactions (strict 2PL and
MVCC), and write-ahead-logging crash recovery.

> ### Team members
>
> | Full Name | Scaler Email | Roll Number |
> | --------- | ------------ | ----------- |
> | Parth Sankhla | parth.24bcs10229@sst.scaler.com | 24BCS10229 |
> | Parvam Shah | parvam.24bcs10231@sst.scaler.com | 24BCS10231 |

---

## 1. Project Overview

**Problem statement.** Build a working relational database from foundational components
and integrate them into a single engine that can parse SQL, plan and execute queries,
run concurrent serializable transactions, and recover correctly from a crash.

**Goals.**
- Implement every required core feature: storage engine, B+ tree index, SQL execution
  (SELECT/WHERE/JOIN/INSERT/DELETE), a cost-based optimizer, transactions with
  serializable isolation, and WAL-based recovery.
- Implement the **MVCC** extension track and demonstrate, with a benchmark, that it
  delivers higher throughput and avoids reader/writer blocking under contention compared
  to two-phase locking.
- Keep the architecture clean and explainable, covered by a test suite.

**Chosen extension track:** Track B — replace 2PL with Multi-Version Concurrency Control
(snapshot isolation). Both schemes are implemented and selectable at database open so they
can be compared directly.

---

## 2. System Architecture

```
                                ┌─────────────────────────┐
              SQL text  ─────►  │      Parser (lexer +    │
                                │   recursive descent)    │
                                └────────────┬────────────┘
                                             │  AST
                                ┌────────────▼────────────┐
                                │   Cost-based Optimizer   │  selectivity, join order,
                                │   (PlanNode tree)        │  SeqScan vs IndexScan
                                └────────────┬────────────┘
                                             │  physical plan
                                ┌────────────▼────────────┐
                                │  Execution engine        │  Volcano operators:
                                │  (open / next iterators) │  Scan, Filter, Join,
                                └────────────┬────────────┘  Project, Aggregate
                                             │  row API (txn-scoped)
        ┌────────────────────────────────────┼────────────────────────────────────┐
        │                                     │                                     │
┌───────▼────────┐   ┌────────────────────────▼───────────┐   ┌────────────────────▼──┐
│  Catalog       │   │  Concurrency control                │   │  Recovery (WAL)        │
│  tables,       │   │  • LockManager (strict 2PL +        │   │  LogManager + ARIES    │
│  schemas, pk   │   │    deadlock detection)              │   │  analysis/redo/undo    │
│  index         │   │  • VersionStore (MVCC snapshots)    │   │                        │
└───────┬────────┘   └────────────────────┬───────────────┘   └───────────┬────────────┘
        │                                  │                               │
        │            ┌─────────────────────▼───────────────────────────────▼──┐
        └───────────►│             Storage engine                              │
        B+ tree      │  TableHeap (slotted pages) ─ BufferPool (LRU, WAL rule) │
        index        │                          ─ DiskManager (paged files)   │
                     └─────────────────────────────────────────────────────────┘
```

**Major modules** (`src/`):

| File | Responsibility |
| ---- | -------------- |
| `common.h` | Value/Tuple types, RID, Schema, serialization |
| `storage.{h,cpp}` | Slotted page layout, DiskManager, BufferPool (LRU + WAL rule) |
| `table.{h,cpp}` | TableHeap: WAL-logged insert/get/delete/scan over pages |
| `wal.{h,cpp}` | Log records, LogManager (fsync), ARIES-style RecoveryManager |
| `index.{h,cpp}` | B+ tree (search / insert / delete with split & merge) |
| `catalog.{h,cpp}` | Table metadata + persistence |
| `concurrency.{h,cpp}` | LockManager (2PL), VersionStore (MVCC), TransactionManager |
| `parser.{h,cpp}` | Lexer + recursive-descent SQL parser → AST |
| `optimizer.{h,cpp}` | Statistics, cost model, plan generation, EXPLAIN |
| `execution.{h,cpp}` | Volcano executors + expression evaluation |
| `engine.{h,cpp}` | `Database` facade: SQL dispatch, the transactional row API, recovery |
| `main.cpp` | Interactive SQL shell + scripted demos |

**Data flow.** SQL → AST → physical plan → executor tree. Executors pull rows through a
transaction-scoped row API (`scan_table`, `read_key`, `insert_row`, `delete_row`) that
transparently applies the active concurrency-control scheme. Mutations are written to the
WAL and the buffer pool; the buffer pool enforces the write-ahead-log rule before any
dirty page reaches disk.

---

## 3. Storage Layer

**Page format (slotted page, 4 KB).**

```
[ int64 page_lsn | u16 num_slots | u16 free_ptr | slot[0]..slot[n-1] | … free … | tuple data ]
                                                  each slot = (u16 offset, u16 length)
```

Tuple data grows downward from the end of the page; the slot array grows upward from the
header. A slot with `length == 0` is a tombstone and is reused on the next insert. The
`page_lsn` is the LSN of the last log record that modified the page; recovery and the WAL
rule both rely on it.

**Heap files.** Each table is one paged file managed by `TableHeap`. Insert finds the
first page with room (or grows the file), get/scan read live slots, delete tombstones a
slot. Every mutation is logged before the page is allowed to be flushed.

**Buffer pool.** A shared, fixed-size frame pool keyed by `(file_id, page_id)` with **LRU**
replacement and pin counting. Before evicting or flushing a dirty page it calls
`LogFlusher::flush_to(page_lsn)`, guaranteeing the **write-ahead-log rule**. Hit/miss
counters are exposed for inspection.

---

## 4. Indexing

**Design.** An in-memory **B+ tree** mapping the `INTEGER` primary key → `RID`. The heap
is the source of truth, so the index is rebuilt by scanning the heap on database open and
after recovery (so it is always consistent with durable data).

**Node structure.** Order 64 (≤ 63 keys/node). Internal nodes hold sorted separator keys
and child pointers; leaf nodes hold sorted `(key, RID)` pairs and a `next` pointer chaining
all leaves for efficient range scans.

**Search path.** Descend from the root, at each internal node binary-search the separators
(`upper_bound`) to pick the child, until a leaf is reached; binary-search the leaf for the
key. Insert splits full nodes and propagates a separator upward; delete rebalances via
borrow-from-sibling or merge, collapsing the root when it empties. (Tested on 5,000 keys
including a half-delete pass that forces merges.)

---

## 5. Query Execution

**Parser.** A hand-written lexer + recursive-descent parser produces an AST for
`CREATE TABLE`, `INSERT`, `SELECT` (projection / `*`, `WHERE`, `JOIN … ON`, `GROUP BY`,
`ORDER BY`, `LIMIT`, aggregates), `DELETE`, and `BEGIN`/`COMMIT`/`ROLLBACK`. Expression
parsing uses precedence climbing (`OR < AND < comparison < +/- < */ < primary`).

**Plan generation.** The optimizer turns a `SELECT` into a physical `PlanNode` tree
(see §6).

**Operator execution.** A classic **Volcano / iterator** model — every operator exposes
`open()` and `next()`:

- `SeqScan` / `IndexScan` — materialize rows from the heap through the transactional row API
- `Filter` — evaluate the `WHERE` predicate
- `NestedLoopJoin` — inner join on the `ON` predicate (inner side materialized once)
- `Projection` — evaluate the select list
- `Aggregation` — `COUNT/SUM/AVG/MIN/MAX` with optional `GROUP BY`

`ORDER BY` and `LIMIT` are applied to the operator output.

---

## 6. Optimizer

A cost-based optimizer that makes two decisions:

**1. Sequential scan vs. index scan.** It walks the top-level `AND` conjuncts of the
`WHERE` clause. If one is an equality on the table's primary key (`pk = <const>`) and there
are no joins, it chooses an **IndexScan** (estimated 1 row, cost ≈ 1 B+ tree descent);
otherwise a **SeqScan** (cost = number of tuples).

**Selectivity estimation.** Equality on a unique key → `1/N`; equality on a non-key column
→ `0.1`; range predicate → `1/3`; combined with independence across conjuncts. These feed
the per-node `est_rows`/`est_cost` annotations.

**2. Join ordering.** Tables are added greedily: at each step the optimizer picks the
eligible table (its `ON` predicate references only already-joined tables) with the smallest
estimated cardinality, building a left-deep nested-loop tree.

`EXPLAIN <select>` prints the chosen plan with cost annotations, e.g.:

```
-> Projection  [est_rows=1 est_cost=3]
  -> Filter  [est_rows=1 est_cost=2]
    -> IndexScan(users pk=2)  [est_rows=1 est_cost=1]
```

---

## 7. Transactions & Concurrency

Two schemes are implemented, selectable at open (`CCMode::TWO_PL` default, `CCMode::MVCC`).

**Strict 2PL.** Row-level shared/exclusive locks keyed by `(table, pk)`, acquired on
read/write and held until commit/abort. A scan acquires each row's shared lock *before*
reading the row's value (then re-reads under the page latch), so a returned value is always
the one protected by the lock. Blocked requests wait on a condition variable; a **waits-for
graph** is checked for cycles on every block, and a **deadlock** aborts the requesting
transaction (`AbortException`). Writes apply eagerly to the heap and are rolled back with
logical inverse operations on abort.

**MVCC / snapshot isolation (extension).** Each transaction takes a snapshot timestamp at
`BEGIN`. Reads see the newest version with `begin_ts ≤ snapshot` (plus their own
uncommitted writes); **readers never block and never take locks**. Writes are staged as
uncommitted versions and only touch the heap at commit, where they receive a commit
timestamp. Write-write conflicts are resolved **first-committer-wins**: a transaction that
tries to write a row already written by a concurrent (or newer-committed) transaction is
aborted.

**Isolation guarantees.** Strict 2PL serializes access to existing rows (conflict
serializable for the locked items); because locks are per-row and there is no predicate or
index-range locking, **phantom inserts are not prevented** — true phantom-free
serializability would need predicate/next-key locking. MVCC provides snapshot isolation.
**Deadlock handling:** detection + victim abort (2PL); MVCC is deadlock-free for writers
(conflict → immediate abort) and never blocks readers.

See `learning-notes.md` for the version-visibility rules in detail.

---

## 8. Recovery

**WAL design.** A single append-only log file of length-prefixed records. Heap mutations
log a **physiological slot image** (`file, page, slot, before-image, after-image`); control
records are `BEGIN/COMMIT/ABORT/CHECKPOINT`. A durable `COMMIT` fsyncs the log; the buffer
pool never writes a page before the log is flushed through that page's `page_lsn`.

**Log records.** `INSERT` (before = empty slot, after = tuple bytes), `DELETE` (before =
tuple bytes, after = empty), and the control records.

**Crash recovery procedure (ARIES-style).**
1. **Analysis** — scan the log; collect the set of committed transactions.
2. **Redo (repeat history)** — replay every heap change whose effect is not yet on its page
   (`page_lsn < record.lsn`), making redo idempotent.
3. **Undo** — walk the log backward and revert changes of transactions that never
   committed.

The result: committed transactions are preserved and uncommitted ones disappear. Run the
live demonstration with `./minidb_cli --crashdemo`.

---

## 9. Extension Track — MVCC

**Motivation.** Under 2PL, a long read-only query holds shared locks on every row it reads,
so writers block behind it (and a writer's exclusive lock blocks readers). MVCC removes this
reader/writer interference by versioning rows and serving each transaction a consistent
snapshot.

**Design.** A `VersionStore` keeps a version chain per logical row
`(begin_ts, end_ts, committed, deleted, tuple, rid)`. Visibility, conflict detection, and
commit/abort are described in §7 and `learning-notes.md`. The store uses a shared mutex so
snapshot readers run concurrently; the durable heap + index are updated only at commit, which
keeps recovery semantics identical to the 2PL path.

**Results.** Long readers + point writers (see §10): MVCC sustains **~13× higher total
throughput** and eliminates reader blocking. 2PL shows zero aborts but serializes
readers/writers; MVCC shows a small number of write-conflict aborts on hot rows — the
expected snapshot-isolation trade-off.

---

## 10. Benchmarks

**Experimental setup.** `benchmarks/bench.cpp`. A table of `rows` rows. `readers` threads
each run full-table scans inside a transaction; `writers` threads do point updates
(delete+reinsert) on random rows. fsync-on-commit is disabled so the measurement reflects
concurrency-control cost, not disk latency (durable commits are exercised by the recovery
tests). Built with `-O2`, 16-core Linux.

```
./build/minidb_bench
```

**Representative result** (`rows=500, readers=6, writers=2`):

| Mode | Read txns/s | Write txns/s | Total txn/s | Aborts |
| ---- | ----------- | ------------ | ----------- | ------ |
| 2PL  | ~4,100      | ~13,500      | ~17,600     | 0      |
| MVCC | ~54,700     | ~182,000     | ~237,000    | ~11    |
| **MVCC speedup** | | | **~13×** | |

**Analysis.** Under 2PL the scanners hold shared locks on all 500 rows for the life of each
read transaction, so the two writers stall waiting for exclusive locks, and scanners stall on
rows a writer is updating. Under MVCC, scanners read an immutable snapshot and never block;
writers proceed independently, conflicting only when two touch the same hot row in the same
window (the ~11 aborts). That gap is the whole point of the MVCC track.

---

## 11. Limitations

- **Schema:** every table must declare exactly one `INTEGER PRIMARY KEY` (it backs the index
  and is the logical row key for concurrency control). One primary index per table; secondary
  indexes are not implemented (the B+ tree supports it, but the planner does not use it).
- **Types:** `INTEGER` (64-bit), `VARCHAR`, `BOOLEAN`. No floats/dates; `AVG` is integer
  division. No `UPDATE` statement (model an update as `DELETE` + `INSERT`).
- **SQL:** inner joins only; no subqueries, `HAVING`, `DISTINCT`, or outer joins. Aggregates
  over the whole table or a single `GROUP BY`.
- **Indexes** are in-memory and rebuilt on open (durability comes from the heap + WAL).
- **MVCC** provides snapshot isolation (not full serializable SSI); old versions are not
  garbage-collected.
- **Scalability:** the lock table and version store use coarse global latches; per-page
  latching/latch-crabbing would be the next step.

**Future improvements:** persistent/secondary B+ tree indexes, hash joins, SSI for true
serializable MVCC, version GC, group commit, and a fuller type system.

---

## 12. How to Run

**Dependencies:** a C++17 compiler (g++ ≥ 11 or clang ≥ 14), CMake ≥ 3.16, pthreads. No
third-party libraries.

**Build.**
```bash
cd 'MiniDB_by_Team-Lock&Roll'   # quote the path: the & is a shell metacharacter
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

**Run the tests** (18 suites, ~12.6k assertions):
```bash
./build/minidb_tests          # or: ctest --test-dir build
```

**Scripted demos.**
```bash
./build/minidb_cli --demo         # SQL: create/insert/select/join/aggregate/EXPLAIN/delete
./build/minidb_cli --crashdemo    # WAL crash recovery
./build/minidb_bench              # 2PL vs MVCC concurrency benchmark
```

**Interactive shell** (statements end with `;`, Ctrl-D to quit):
```bash
./build/minidb_cli --dir mydata          # 2PL mode
./build/minidb_cli --dir mydata --mvcc   # MVCC mode
```
```sql
CREATE TABLE users (id INTEGER PRIMARY KEY, name VARCHAR, age INTEGER);
INSERT INTO users VALUES (1,'alice',30),(2,'bob',25);
SELECT name FROM users WHERE id = 1;
EXPLAIN SELECT * FROM users WHERE id = 1;
SELECT age, COUNT(*) FROM users GROUP BY age;
BEGIN; INSERT INTO users VALUES (3,'carol',40); ROLLBACK;
```
