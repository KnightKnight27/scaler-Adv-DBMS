# MiniDB — a relational database engine, from scratch

**Team walError | Advanced DBMS Capstone**

MiniDB is a working relational database engine written from scratch in pure
Python (standard library only; `matplotlib` is used solely for benchmark charts
and `pytest` for tests). It implements the full path from a SQL string to bytes
on disk — parser, cost-based optimizer, Volcano executor, a page-based heap with
a buffer pool, a B+ tree index, serializable transactions with deadlock
detection, and write-ahead-logging crash recovery — plus an **LSM-tree storage
engine (Track C extension)** built alongside the heap.

> 168 automated tests pass. Every feature has a runnable, narrated demo.

---

## 1. Project Overview

MiniDB demonstrates how a relational database works internally by building each
layer explicitly and wiring them into one engine reachable through a single
`Database.execute(sql)` facade and a REPL.

- **Language:** Python 3.11+ (developed on 3.14), standard-library-first.
- **Chosen extension track:** **Track C — Modern Storage (LSM-tree)**, implemented
  in `src/minidb/lsm.py` as an independent engine so the core heap engine stays
  stable.
- **Supported SQL:** `CREATE TABLE`, `CREATE INDEX`, `INSERT` (multi-row),
  `SELECT` (projection, `WHERE` with `AND`/`OR`, `INNER JOIN`, aliases),
  `DELETE`, `EXPLAIN`, and `BEGIN`/`COMMIT`/`ROLLBACK`.

## 2. System Architecture

```
            SQL string
                │
                ▼
   ┌─────────────────────────┐
   │ sql.py  tokenizer+parser │  → AST
   └─────────────────────────┘
                │
                ▼
   ┌─────────────────────────┐
   │ plan.py  optimizer       │  → physical operator tree (SeqScan/IndexScan/
   └─────────────────────────┘     Filter/NestedLoopJoin/Project) with est. cost
                │
                ▼
   ┌─────────────────────────┐
   │ executor.py  Volcano     │  pull tuples: open()/next()/close()
   └─────────────────────────┘
                │
                ▼
   ┌─────────────────────────┐    ┌───────────────────────┐
   │ catalog.py  tables,      │◄──►│ btree.py  B+ tree index│
   │ schemas, indexes, stats  │    └───────────────────────┘
   └─────────────────────────┘
                │
                ▼
   ┌─────────────────────────┐
   │ heap.py  RID-addressed   │
   │ records over pages       │
   └─────────────────────────┘
                │
                ▼
   ┌─────────────────────────┐
   │ buffer_pool.py clock-sweep│  cache + eviction + dirty flush
   └─────────────────────────┘
                │
                ▼
   ┌─────────────────────────┐
   │ disk_manager.py  one file │  fixed-size pages + fsync
   └─────────────────────────┘

   Cross-cutting:
     transaction.py + lock_manager.py  — strict 2PL, S/X locks, deadlock detection
     wal.py                            — write-ahead log + redo recovery (durability)
     engine.py                         — Database facade wiring it all together
     lsm.py                            — independent LSM-tree engine (Track C)
```

The full data flow with code references is in [`docs/DATA_FLOW.md`](docs/DATA_FLOW.md);
component interfaces are in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md); a
per-module reference is in [`docs/MODULES.md`](docs/MODULES.md).

## 3. Storage Layer

- **Slotted pages** (`page.py`): fixed `PAGE_SIZE` (4 KB) pages with a forward-
  growing slot directory and backward-growing record data. Deletes are tombstones,
  so record ids (**RID** = page id + slot) never move — exactly the model
  PostgreSQL uses with deferred `VACUUM`.
- **Disk manager** (`disk_manager.py`): treats the database as a flat array of
  pages in one file; `allocate`/`read`/`write` plus an `fsync` flush that is the
  durability point relied on by the WAL.
- **Buffer pool** (`buffer_pool.py`): a bounded set of frames with **clock-sweep**
  eviction (a reference bit per frame, approximating LRU), pin counts (a pinned
  page is never evicted), dirty-page flush on eviction, and hit/miss statistics.
- **Heap file** (`heap.py`): a table as a sequence of pages; append-mostly insert,
  `get`/`delete`/`scan`, RID serialization for indexes.

## 4. Indexing

`btree.py` is an in-memory **B+ tree** mapping keys → RID:

- Split on insert (separator copied up for leaves, moved up for inner nodes);
  borrow-from-sibling or merge on delete; the root grows/shrinks by a level.
- Leaves are linked in sorted order, so the same structure serves both point
  lookups and ordered range scans.
- The catalog auto-creates a unique index for each table's primary key, and
  supports non-unique **secondary indexes** via composite `(value, page, slot)`
  keys so duplicate values coexist.

Design choice: indexes are rebuilt from the base table, so durability is provided
solely by the heap + WAL and an index can never be left inconsistent by a crash.

## 5. Query Execution

`executor.py` implements the **Volcano (iterator) model**: every operator exposes
`open()/next()/close()` and pulls one tuple at a time from its child. Operators:
`SeqScan`, `IndexScan`, `Filter`, `NestedLoopJoin`, `Project`. Tuples carry their
source RIDs so `DELETE` reuses the same scan/filter pipeline. NULLs follow
SQL-style three-valued logic (a comparison with NULL is false for filtering).

## 6. Optimizer

`plan.py` is a **cost-based optimizer**:

- **Selectivity** from catalog statistics: equality on a unique index ≈ `1/row_count`,
  on a column with `ndv` distinct values ≈ `1/ndv`, ranges ≈ `0.3`.
- **Access-path choice**: SeqScan vs IndexScan, comparing estimated costs; an
  IndexScan may use loose bounds with a residual Filter to stay exact.
- **Join ordering**: greedy left-deep, smallest estimated cardinality on the
  outer side (fewest inner rescans), preferring relations linked by a join
  predicate.
- `EXPLAIN <query>` prints the chosen plan with `est_rows`/`est_cost`.

## 7. Transactions & Concurrency

- `lock_manager.py`: shared/exclusive locks with standard compatibility, lock
  upgrade, blocking acquisition, and **deadlock detection** via a wait-for graph
  (cycle ⇒ the requester becomes the victim and is aborted).
- `transaction.py`: **strict two-phase locking** — locks are held until
  commit/abort, giving serializable schedules with no cascading aborts.
- The engine takes table-level shared locks for reads and exclusive locks for
  writes; on a deadlock the victim transaction is aborted and its effects
  discarded.

## 8. Recovery

`wal.py` + `engine.py` implement **write-ahead logging with redo recovery**,
using a *WAL-as-source-of-truth* model:

- Each change is logged (newline-delimited JSON: `begin`/`create_table`/
  `create_index`/`insert`/`delete`/`commit`/`abort`); `COMMIT` is appended and
  **fsynced** — the durability point.
- On startup the heap file is rebuilt and the WAL is replayed, applying **only**
  the operations of transactions that logged a `COMMIT`. Uncommitted work is
  never replayed, so it vanishes after a crash with **no UNDO logic**.
- Verified by `tests/test_recovery.py` and `demos/demo_recovery.py`: committed
  transactions survive a crash; uncommitted ones do not; `ROLLBACK` is durable;
  a failed statement is atomic.

## 9. Extension Track — Track C (LSM-tree)

`lsm.py` is a self-contained log-structured merge-tree:

- **MemTable** (in-memory) + a **WAL** for unflushed writes.
- Flush to immutable, sorted **SSTables** with an in-memory key→offset index and
  a per-SSTable **Bloom filter** (double hashing) so reads skip most SSTables.
- **Size-tiered compaction** merges SSTables (newest wins) and drops tombstones,
  bounding space amplification.
- Read path: MemTable → SSTables newest→oldest; write path: append-only.

## 10. Benchmarks

`benchmarks/run_all.py` compares LSM vs B+Tree vs heap on write throughput,
point-read latency, and space amplification, producing charts + `results.json`.
Headline findings (N = 20 000): the in-memory **B+Tree wins reads** (~1.9 µs vs
~18 µs); the **LSM bounds space under churn** via compaction (~1.1× vs the heap's
~2.1×). Full methodology and an honest write-throughput analysis are in
[`benchmarks/README.md`](benchmarks/README.md).

## 11. Limitations

- The B+ tree and the catalog are in-memory (rebuilt from the heap/WAL on open);
  startup replays the whole WAL (no checkpointing) — fine at course scale, O(history).
- Locking is table-level (coarse but unambiguously serializable); no MVCC.
- SQL is a teaching subset: no aggregates/`GROUP BY`/`ORDER BY`/subqueries/`UPDATE`
  (modelled as delete + insert), single-column primary keys, `INNER JOIN` only.
- The heap reclaims space only by full rewrite (no `VACUUM`/free-space map).
- The recovery model rebuilds the heap file from the WAL on each open, so the
  `.db` file is a working materialization rather than the durable store.

## 12. How to Run

```bash
cd MiniDB_Projects/Team_walError
python -m venv .venv
./.venv/bin/pip install -e ".[dev]"      # pytest + matplotlib

./.venv/bin/python -m pytest -q          # run all 168 tests
./.venv/bin/python -m minidb.cli         # interactive SQL REPL (in-memory)
./.venv/bin/python -m minidb.cli my.db   # persistent database

# narrated demos
./.venv/bin/python demos/demo_storage.py
./.venv/bin/python demos/demo_btree.py
./.venv/bin/python demos/demo_query.py
./.venv/bin/python demos/demo_optimizer.py
./.venv/bin/python demos/demo_transactions.py
./.venv/bin/python demos/demo_recovery.py
./.venv/bin/python demos/demo_lsm.py

# benchmarks (writes charts + results.json into benchmarks/)
./.venv/bin/python benchmarks/run_all.py
```

---

## Team Information

**Team walError**

| Name | Email | Roll |
|------|-------|------|
| Archisman Midya | archisman.23bcs10027@sst.scaler.com | 10027 |

*Solo team (course guidelines suggest 2–4 members; solo by the member's choice.)*
