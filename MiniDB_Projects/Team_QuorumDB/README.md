# MiniDB — Team QuorumDB

A working relational database engine built from foundational components for the
Advanced DBMS capstone. MiniDB implements a page-based storage engine, B+Tree
indexing, a SQL parser / cost-based optimizer / executor, strict two-phase
locking with deadlock detection, write-ahead logging with ARIES-style crash
recovery, and — as its extension — **Track D: primary-replica replication**
with failover.

Pure Python 3.10+, **zero external runtime dependencies**.

```
                 ┌──────────────────────────────────────────────┐
   SQL text ───► │  Parser → Optimizer → Executor (Volcano ops)  │
                 └───────────────┬──────────────────────────────┘
                                 │
   ┌─────────────┐      ┌────────▼────────┐      ┌──────────────────┐
   │  Catalog    │◄────►│   Buffer Pool   │◄────►│   Disk Manager   │
   │ schemas,    │      │  (CLOCK, pin/   │      │  paged heap file │
   │ B+Tree idx  │      │   unpin, dirty) │      └──────────────────┘
   └─────────────┘      └───┬─────────┬───┘
                            │         │ write-ahead rule
            locks (2PL) ────┘         └──── WAL (LogManager) ──► Recovery
                                                  │
                                                  └──► Replication (Track D)
```

---

## Team

**Team Name:** QuorumDB

| Member | GitHub | Email | Roll Number |
|---|---|---|---|
| _Snehangshu Roy_ | `alienxviking` | snehangshu.24bcs10155@sst.scaler.com | _24BCS10155_ |
| _Rohan Ranjan_ | `RohanRanjan250` | rohan.24bcs10428@sst.scaler.com | _24BCS10428_ |
| _Rudhar Bajaj_ | `rudhar07` | rudhar.24bcs10143@sst.scaler.com | _24BCS10143_ |


---

## 1. Project Overview

**Problem statement.** Build a small but genuinely functional relational
database that integrates the core internals studied across the course —
storage, indexing, query processing, transactions, and recovery — into one
coherent system that can be demonstrated and defended, rather than a pile of
disconnected components.

**Goals.**
- Correctness and completeness of the required core features.
- A clean, layered architecture where each subsystem has one responsibility.
- Engineering decisions we can explain and defend, with trade-offs documented.
- End-to-end SQL execution, durable across crashes, observable via demos.

**Chosen extension track — Track D (Distributed Systems).** A two-node
primary-replica replication layer that ships the write-ahead log to a replica,
serves consistent reads from it, and supports promotion on primary failure.
We chose Track D because it composes naturally with the WAL we already need for
recovery: the same physiological log records drive both crash recovery and
replication, which is an elegant, defensible design.

## 2. System Architecture

### Major modules (`src/minidb/`)

| Module | Responsibility |
|---|---|
| `storage/page.py` | Slotted page format, stable-RID slot directory, page-LSN |
| `storage/disk_manager.py` | Single-file paged store: allocate/read/write + fsync |
| `storage/buffer_pool.py` | CLOCK buffer pool, pin/unpin, dirty tracking, WAL rule |
| `storage/heapfile.py` | Per-table heap: free-space inserts, tombstone deletes, scan |
| `index/bplustree.py` | B+Tree: search, range, insert (split), delete (merge) |
| `catalog/schema.py` | Types, schema, null-bitmap tuple (de)serialisation |
| `catalog/catalog.py` | Table metadata, heaps, indexes, JSON persistence |
| `sql/parser.py` | Tokenizer + recursive-descent parser → AST |
| `sql/optimizer.py` | Selectivity, access-path choice, join ordering |
| `sql/plan.py` | Volcano physical operators + EXPLAIN |
| `sql/executor.py` | DDL/DML/SELECT execution, index maintenance |
| `txn/lock_manager.py` | Strict 2PL, S/X locks, deadlock detection |
| `txn/transaction.py` | Transactions, commit, abort/undo, txn manager |
| `wal/log_record.py` | Physiological log records (shared by recovery + replication) |
| `wal/log_manager.py` | LSN assignment, durable append/flush |
| `wal/recovery.py` | ARIES analysis → redo → undo |
| `replication/*` | Track D: primary, replica, wire protocol |
| `engine.py` | `Database` facade + per-session `Connection` |
| `cli.py` | Interactive SQL shell |

### Data flow (a `SELECT`)

1. **Parser** turns SQL into an AST.
2. **Optimizer** estimates selectivity/cardinality, picks SeqScan vs IndexScan
   per relation, and orders joins, producing a tree of physical operators.
3. **Executor** pulls rows from the operator tree (Volcano iterator model).
   Scans take **shared table locks** via the transaction (strict 2PL).
4. Operators read tuples through the **heap file**, which fetches pages from the
   **buffer pool**, which reads from the **disk manager** on a miss.

A write (`INSERT`/`DELETE`) additionally takes an **exclusive table lock**, logs
a **WAL** record (stamping the page LSN), and maintains every **index**.

## 3. Storage Layer

**Page format (`page.py`).** Fixed 4 KiB slotted pages. A header holds the
page LSN, slot count, and a free-space pointer. The slot directory grows
forward from the header; record bytes grow backward from the end; free space
sits in the middle.

```
[ pageLSN | nslots | free_end ][ slot0 | slot1 | … →        ← … rec1 | rec0 ]
```

Each slot is an `(offset, length)` pair; `length == 0` is a **tombstone** left
by a delete. Tombstoned slots keep their index, so a **RID = (page_id,
slot_no)** is stable for the life of a record — which is what lets B+Tree leaves
store RIDs safely. Deleted slots are reused by later inserts.

**Heap files (`heapfile.py`).** A table is a set of data pages (page ids owned
by the catalog). Inserts pick the first page with enough free space (tracked by
an in-memory free-space map to avoid I/O), allocating a new page only when none
fits. Deletes tombstone the slot. A scan walks all pages yielding `(RID, bytes)`.

**Buffer pool (`buffer_pool.py`).** A fixed set of frames cache pages. Callers
`fetch_page` (which **pins**) and `unpin_page` (flagging dirty). On a full pool
a **CLOCK** sweep evicts an unpinned, un-referenced victim. Two invariants:
pinned pages are never evicted, and — enforcing the **write-ahead rule** — the
log is flushed up to a page's LSN before that dirty page is written to disk.

## 4. Indexing

**Design (`bplustree.py`).** A classic B+Tree mapping ordered keys → RID lists.

- **Node structure.** A leaf holds sorted `keys` with a parallel `values` list
  (a list of RIDs per key) and a `next` pointer chaining leaves left-to-right.
  An internal node holds separator `keys` and `k+1` `children`.
- **Search path.** Descend from the root: at each internal node binary-search
  the separators to choose a child; at the leaf binary-search the keys. Range
  scans descend once to the low key then walk the `next` chain.
- **Insert.** Recursive; an overflowing leaf or internal node **splits**, a leaf
  copying its median up and an internal node pushing it up.
- **Delete.** Recursive; an underflowing node **borrows** from a sibling or
  **merges**, and the root collapses when it empties.
- **Modes.** Unique (primary key — rejects duplicates) and non-unique
  (secondary index — one key → many RIDs).

Indexes are **rebuilt from the recovered heap on startup** and are not
themselves logged — a deliberate trade-off (see Limitations) that keeps the
index layer free of WAL coupling. Validated by a 2,000-operation randomized
fuzz test against a reference dictionary.

## 5. Query Execution

**Parser (`parser.py`).** A hand-written tokenizer + recursive-descent parser
producing the AST in `ast.py`. Supported surface: `CREATE TABLE`
(PRIMARY KEY / NOT NULL), `CREATE [UNIQUE] INDEX`, `DROP TABLE`,
`INSERT … VALUES` (multi-row), `DELETE … WHERE`, `SELECT` with projection,
table aliases, `INNER JOIN … ON`, `WHERE` (AND-conjunction of comparisons),
and `BEGIN` / `COMMIT` / `ROLLBACK`.

**Plan generation.** The optimizer (§6) turns a `Select` into a tree of physical
operators.

**Operator execution (`plan.py`).** The **Volcano / iterator model** — each
operator is a generator of rows (dicts keyed by `qualifier.column`):
`SeqScan`, `IndexScan`, `Filter`, `NestedLoopJoin` (block nested loop, inner
side materialised), and `Projection`. Operators compose into a tree the
executor pulls to completion. `EXPLAIN <select>` prints the chosen tree with
row estimates.

## 6. Optimizer

A **cost-based** optimizer (`optimizer.py`) with two jobs:

**Selectivity / cost estimation.** Row counts come from the primary-key index
size; distinct-value counts from `BPlusTree.num_keys()`. Equality on a key has
selectivity `1/ndv`; ranges and `!=` use heuristics (0.3, 0.9).

**Access-path selection.** For each relation it compares estimated costs —
`SeqScan ≈ N` rows versus `IndexScan ≈ tree_height + N·selectivity` — and picks
the cheaper. Any predicate the index didn't satisfy becomes a `Filter`. (Demo:
PK equality → `IndexScan`; non-indexed predicate → `SeqScan`.)

**Join ordering.** It builds a join graph from the `ON` predicates and grows a
left-deep nested-loop plan **greedily**, always adding the connected relation
with the smallest estimated cardinality first, so the smaller inputs drive the
joins.

## 7. Transactions & Concurrency

**Locking strategy (`lock_manager.py`).** **Strict two-phase locking.** Locks
are taken on named resources at **table granularity** (`"table:users"`) in two
modes — **S** (shared, readers) and **X** (exclusive, writers). All locks are
held until commit/abort. Reads (scans) take S; writes take X; lock upgrade
S→X is supported for a sole holder.

**Isolation guarantees.** Strict 2PL at table granularity yields
**serializable** schedules and, because new rows fall under the same table lock,
prevents **phantoms**. Schedules are also recoverable (no dirty reads of
uncommitted writers).

**Deadlock handling.** Before a transaction blocks, the manager records its
edges in a **waits-for graph** and runs cycle detection; if granting the lock
would close a cycle the requester is chosen as the **deadlock victim** and a
`DeadlockError` is raised, so the engine aborts (and could retry) it. (Demo:
two transactions locking two tables in opposite order.)

## 8. Recovery

**WAL design (`log_record.py`, `log_manager.py`).** Every change is a
**physiological log record**: page id, slot, and before/after byte images, with
a monotonic LSN and a per-transaction prev-LSN chain. The log manager appends
records and `flush(lsn)` makes them durable (`fsync`). Commit flushes the log
(commit durability); the buffer pool flushes the log before any dirty page
(write-ahead rule). Policy is **STEAL / NO-FORCE**.

**Log records.** `BEGIN`, `COMMIT`, `ABORT`, `INSERT`, `DELETE`, `UPDATE`,
`CLR` (compensation, written during undo), `CHECKPOINT`.

**Crash recovery procedure (`recovery.py`) — ARIES, three passes.**
1. **Analysis** — scan from the last checkpoint; transactions with no
   COMMIT/ABORT are *losers*. Also collect which pages each table touched.
2. **Redo** — replay every data change forward (winners *and* losers — "repeat
   history"), skipping any a page already reflects (`page.lsn ≥ record.lsn`).
3. **Undo** — roll back losers by applying before-images in reverse; undo is
   idempotent (it targets a specific slot with a specific image), so a crash
   mid-undo is safe.

Committed transactions are preserved; uncommitted ones vanish. (Demo:
`demos/demo_recovery.py`.)

## 9. Extension Track — D: Primary-Replica Replication

**Motivation.** Real systems replicate for availability and read scaling. The
WAL we built for recovery is exactly the right thing to ship: a replica that
applies the redo stream is, in effect, performing *continuous recovery*.

**Design (`replication/`).**
- **Protocol** — a length-framed TCP message protocol (`CATALOG`, `RECORDS`,
  `ACK`, `HEARTBEAT`) that reuses the WAL record encoding verbatim.
- **Primary** — streams a catalog snapshot + the log to a new replica, then
  pushes new records live; tracks each replica's acknowledged LSN for **lag**.
- **Replica** — applies redo with the *same* `page_ops.redo` recovery uses
  (allocating pages to match the primary's numbering, guarded by page LSN),
  rebuilds indexes for read consistency, and serves read-only queries.
- **Failover** — on primary loss the replica is **promoted** to writable.

**Results.** Read-after-replicate consistency (replica reads match the primary
exactly), ~223k redo records/sec applied in-process, near-zero streaming lag
over sockets, and a working promotion-on-failover path. See §10 and
`demos/demo_replication.py`.

## 10. Benchmarks

Full setup, results, and analysis in **[`benchmarks/REPORT.md`](benchmarks/REPORT.md)**
(`python benchmarks/run_benchmarks.py`). Headline numbers:

| Experiment | Result |
|---|---|
| IndexScan vs SeqScan (point lookup) | **22.7× faster** with the B+Tree |
| Batched txn vs autocommit inserts | **14.5× higher** throughput (fsync amortised) |
| Buffer-pool hit ratio (pool ≥ working set) | **0 → 0.83** |
| Replication redo apply | **~223k records/sec**, replica consistent |

## 11. Limitations

- **SQL surface is intentionally small**: no `UPDATE` statement, `OR`/nested
  expressions, aggregation/`GROUP BY`, `ORDER BY`, or subqueries.
- **Indexes are in-memory and rebuilt on startup** from the heap (not persisted
  or WAL-logged). Trade-off: simpler, decoupled index code and crash safety for
  free, at the cost of startup rebuild time on large tables.
- **Locking is table-granularity** (serializable but coarse) — correct, but it
  limits concurrency versus row-level or MVCC schemes.
- **Joins are nested-loop only** (no hash/merge join); join-order search is
  greedy, not exhaustive.
- **Catalog is a JSON sidecar**; DDL is auto-committed and not transactional.
- **Scale**: tuned for teaching-scale datasets; the WAL keeps a full in-memory
  mirror and checkpoints don't truncate the log.

**Future improvements.** Page-resident B+Tree with logged index changes,
row-level/MVCC concurrency, `UPDATE` + aggregation, hash joins, histogram-based
statistics, and log truncation at checkpoints.

## 12. How to Run

**Requirements.** Python 3.10+ (developed on 3.13). No runtime dependencies;
`pytest` only for the test suite.

```bash
cd MiniDB_Projects/Team_QuorumDB

# Interactive SQL shell
PYTHONPATH=src python -m minidb.cli data/mydb      # Windows: set PYTHONPATH=src

# Run the demos (each is self-contained)
python demos/demo_sql.py
python demos/demo_concurrency.py     # deadlock detection
python demos/demo_recovery.py        # crash + recovery
python demos/demo_replication.py     # two-node replication + failover

# Benchmarks
python benchmarks/run_benchmarks.py [--scale N]

# Tests
pip install pytest
python -m pytest tests/ -q
```

**Example session.**
```sql
CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT);
INSERT INTO users VALUES (1,'alice',30),(2,'bob',25);
SELECT id, name FROM users WHERE age > 28;
EXPLAIN SELECT name FROM users WHERE id = 1;   -- shows IndexScan
.tables
.schema users
.exit
```
