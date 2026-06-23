# MiniDB — A Relational Database Engine Built From Scratch

> Advanced DBMS Capstone · Extension Track **B (MVCC)**
> Team **SuperSymmetry**
>
> **Members:**
> - Lokendra Singh Rajawat (10075, lokendra.23bcs10075@sst.scaler.com)
> - Anika Tripathi (10409, anika.24bcs10409@sst.scaler.com)

MiniDB is a small but genuinely working relational database engine written in
pure Python. It implements the full stack a real RDBMS needs — page-based
storage, a buffer pool, B+ tree indexing, a SQL front-end, a cost-based
optimizer, ACID transactions under **strict two-phase locking**, **write-ahead
logging with crash recovery**, and an **MVCC** snapshot-isolation engine as the
extension — and demonstrates each with runnable demos, an automated test suite,
and a benchmark harness producing real numbers.

---

## Table of Contents
1. [Overview](#1-overview)
2. [Architecture](#2-architecture)
3. [Storage Engine](#3-storage-engine)
4. [Indexing](#4-indexing)
5. [Query Execution](#5-query-execution)
6. [Query Optimizer](#6-query-optimizer)
7. [Transactions & Concurrency](#7-transactions--concurrency)
8. [Recovery](#8-recovery)
9. [Extension: MVCC](#9-extension-mvcc)
10. [Benchmarks](#10-benchmarks)
11. [Limitations & Future Work](#11-limitations--future-work)
12. [How to Run](#12-how-to-run)

---

## 1. Overview

MiniDB accepts a useful subset of SQL and executes it against durable,
page-based storage. Supported statements:

- `CREATE TABLE` (with `PRIMARY KEY`) and `CREATE INDEX`
- `INSERT` (single- and multi-row)
- `SELECT` with projection, `WHERE` (conjunctions), multi-table `JOIN ... ON`,
  `GROUP BY`, and the aggregates `COUNT / SUM / AVG / MIN / MAX`
- `DELETE` with `WHERE`
- `BEGIN / COMMIT / ABORT (ROLLBACK)` for explicit transactions

Two isolation engines share the same SQL front-end and physical operators and
are selectable per database: **2PL** (serializable strict two-phase locking) and
**MVCC** (snapshot isolation).

## 2. Architecture

Layered design, each layer depending only on those beneath it:

```
            SQL text
               │
        sql.py (parser)  ──►  AST
               │
        optimizer.py  ──►  physical plan (cost-based)
               │
        executor.py   ──►  Volcano operators (SeqScan, IndexScan, Join, ...)
               │
   ┌───────────┴───────────────────────────┐
   │            database.py                 │  integration / execution context
   │   (autocommit, txns, isolation modes)  │
   └─┬─────────┬──────────┬────────┬────────┘
     │         │          │        │
 catalog   lock_mgr     mvcc      wal       ◄─ transaction services
     │         │          │        │
  btree    heap_file ─── buffer_pool ─── disk_manager   ◄─ storage
                            │
                          page.py (slotted pages)
```

The single public entry point is `Database.execute(sql)`; everything else is an
internal collaborator. See `src/minidb/__init__.py` for the layer map.

## 3. Storage Engine

- **Disk manager** (`disk_manager.py`) — gives every table its own
  `<table>.dat` file of fixed **4 KB pages**, addressed by `PageId(file_key,
  page_num)`, with `pread`/`pwrite` and explicit `fsync`.
- **Slotted pages** (`page.py`) — a 16-byte header (`page_lsn`, slot count, free
  pointer) plus a slot directory growing from the front and records growing from
  the back. Supports insert, in-place update, and tombstone delete. The
  `page_lsn` is what ties a page to the log for the WAL rule.
- **Buffer pool** (`buffer_pool.py`) — an LRU cache of pinned frames over the
  disk manager. Before any dirty page is written back it invokes a
  `log_flush(page_lsn)` callback, enforcing **write-ahead logging**: the log is
  always durable past a page's LSN before the page itself reaches disk.
- **Heap files** (`heap_file.py`) — unordered record storage addressed by
  `RID = (page_num, slot)`, with an insertion hint and a recovery-safe
  `redo_set` used during replay.

## 4. Indexing

`btree.py` is a classic **B+ tree** (default order 64) mapping a key to a list
of RIDs, so it serves both unique primary keys and non-unique secondary indexes.
Internal nodes route; leaves hold data and are chained for efficient range
scans. It implements `search` (point), `range_scan` (`low..high` over the leaf
chain), `insert` with node splitting, and `delete` with borrow/merge
rebalancing. It is validated in the test suite against a dictionary oracle over
thousands of randomized operations. Primary keys are indexed automatically;
additional indexes are created with `CREATE INDEX`.

## 5. Query Execution

`executor.py` uses the **Volcano (iterator) model**: every operator exposes
`execute(ctx)` yielding rows, where a row is a dict keyed by fully-qualified
`table.column`. Operators: `SeqScan`, `IndexScan`, `Filter`, `NestedLoopJoin`,
`HashJoin`, `Aggregate` (with `GROUP BY`), and `Project`. Operators are identical
across isolation modes because all data access goes through an **execution
context** (`TwoPLCtx` / `MVCCCtx` in `database.py`) that hides whether a read
takes a lock or consults a snapshot.

## 6. Query Optimizer

`optimizer.py` is a **cost-based** optimizer. It resolves columns/aliases, pushes
single-table predicates down to scans, and:

- **chooses SeqScan vs IndexScan** by estimated cost — a sequential scan costs
  ≈ `n_tuples`; an index probe costs ≈ `2·log₂(n) + matched`. On a tiny table
  the scan wins; on a large table with a selective predicate the index wins.
- **estimates selectivity** — equality = `1/NDV`, ranges from column `min/max`,
  with an independence assumption across ANDed predicates.
- **orders joins** greedily (smallest intermediate result first) and uses a
  **hash join** with the smaller input as the build side.

`db.explain(sql)` prints the chosen physical plan with cardinality and cost
annotations. Run `db.analyze()` after a bulk load to refresh statistics.

## 7. Transactions & Concurrency

The default engine is **strict two-phase locking** (`lock_manager.py`),
providing serializable isolation:

- Shared (`S`) locks for reads, exclusive (`X`) for writes, at **row
  granularity**, all held until commit (strict 2PL).
- A real blocking lock manager built on a condition variable; waiters sleep
  until the resource frees.
- **Deadlock detection** via a wait-for graph: before a transaction waits, the
  manager checks whether doing so would close a cycle and, if so, raises
  `DeadlockError`. The database aborts the victim, releases its locks, and the
  survivor proceeds. `demos/demo_concurrency.py` shows two real threads
  deadlocking with exactly one victim.

## 8. Recovery

`wal.py` is a **write-ahead log** of length-prefixed records (`BEGIN`,
`UPDATE` with before/after images, `COMMIT`, `ABORT`, `CHECKPOINT`). It tolerates
a torn final record. Recovery is **ARIES-style**:

1. **Analysis** — classify transactions into committed vs *losers* (began but
   neither committed nor aborted).
2. **Redo** — replay every logged `UPDATE` (repeating history; idempotent via
   the page LSN), so committed work survives even a **steal** (uncommitted dirty
   pages that reached disk).
3. **Undo** — roll losers back in reverse using before-images, writing
   compensating records so the rollback is itself durable.

Live `ABORT` uses the same before-image mechanism through logged compensating
records. `demos/demo_recovery.py` simulates a crash and shows committed rows
surviving while an uncommitted transaction is rolled back and its indexes
restored.

## 9. Extension: MVCC

`mvcc.py` implements **multi-version concurrency control** with snapshot
isolation (Extension Track B). Each logical row owns a chain of versions; a
shared monotonic `Clock` issues start and commit timestamps.

- **Readers take no locks.** A transaction sees the newest version whose commit
  timestamp ≤ its start timestamp (plus its own uncommitted writes) — a stable
  snapshot for the transaction's lifetime.
- **Writers** append new versions; on commit each version is stamped with the
  commit timestamp and the superseded version is closed.
- **First-committer-wins**: if a row was committed after a writer's snapshot, or
  is held uncommitted by another active transaction, the writer raises
  `MVCCConflict` and aborts.

Because readers never block on writers, MVCC delivers higher read throughput
under contention — quantified below. `demos/demo_concurrency.py` and the MVCC
test demonstrate that an old snapshot stays stable while a concurrent writer
commits a new value.

## 10. Benchmarks

Generated by `benchmarks/bench.py` (your numbers will vary by machine; these are
representative):

**Concurrency — 2PL vs MVCC** (8 readers + 2 writers, 40-row hot set):

| Metric | 2PL | MVCC |
|---|---:|---:|
| Read throughput (txn/s) | ~13,900 | ~20,800 |
| Avg read latency (ms) | ~0.56 | ~0.36 |
| Write throughput (txn/s, writers-only) | ~128 | ~143 |

→ **MVCC reads run ~1.5× faster with lower latency** because snapshot reads do
not block on writers — the headline Track-B result. (Write throughput is
reported writers-only; under CPython's GIL, MVCC's lock-free readers otherwise
busy-run and starve writer threads, an interpreter artifact rather than an
engine property.)

**Access methods** — 800 point lookups over 4,000 rows:

| Path | µs/query | Speedup |
|---|---:|---:|
| B+ tree IndexScan | ~55 | **≈ 480× faster** |
| Full SeqScan | ~26,000 | — |

**Buffer pool** — 3 scans of a 20,000-row table (104 pages):

| Pool capacity | Hit rate |
|---:|---:|
| 16 pages | 0% |
| 64 pages | 0% |
| 256 pages | 100% |

(Classic scan/LRU behaviour: once the pool holds the whole file, repeat passes
are all hits.)

## 11. Limitations & Future Work

Honest scope boundaries (good viva discussion points):

- **In-memory B+ trees.** Indexes are rebuilt by scanning the heap at startup
  rather than being page-backed on disk. Future work: persist B+ tree pages
  through the buffer pool.
- **Row-level 2PL does not prevent phantoms.** There are no predicate or
  next-key locks, so it is serializable for existing rows but not against
  insertions matching a prior predicate.
- **MVCC store is in-memory**, seeded from the persisted heap at startup; it has
  no version garbage collection (old versions accumulate for a long-running
  process). Durable, page-backed multiversion storage is future work.
- **SQL subset**: no `UPDATE` statement (modelled as delete+insert), no `OR` in
  `WHERE`, no subqueries, no `ORDER BY`/`LIMIT`, single-column indexes only.
- **Single-process** engine; no networked client/server.

## 12. How to Run

Requires **Python 3.10+** (developed on 3.12), standard library only — no
third-party dependencies.

```bash
cd Team_SuperSymmetry

# Interactive SQL shell (2PL by default; --isolation MVCC for snapshot mode)
PYTHONPATH=src python -m minidb.cli --dir ./mydata
PYTHONPATH=src python -m minidb.cli --dir ./mydata --isolation MVCC

# Demos
python demos/demo_recovery.py        # crash recovery (redo + undo)
python demos/demo_concurrency.py     # 2PL locking + deadlock detection
python demos/demo_optimizer.py       # index utilization + EXPLAIN + joins

# Test suite (standalone, or `pytest tests/`)
python tests/test_minidb.py

# Benchmarks (writes benchmarks/results.md)
python benchmarks/bench.py
```

Minimal programmatic use:

```python
import sys; sys.path.insert(0, "src")
from minidb import Database

db = Database("./mydata", isolation="2PL")
db.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT)")
db.execute("INSERT INTO users VALUES (1, 'alice', 30), (2, 'bob', 25)")
print(db.execute("SELECT name FROM users WHERE age > 26"))
print(db.explain("SELECT name FROM users WHERE id = 1"))
db.close()
```
