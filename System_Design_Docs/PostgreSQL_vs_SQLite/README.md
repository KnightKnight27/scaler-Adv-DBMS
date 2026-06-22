# PostgreSQL vs SQLite: Client-Server vs Embedded Architecture

> Advanced DBMS — System Design Discussion · Topic 1
>
> **Author:** Mayank Gupta · **Roll Number:** 24BCS10220
> _Submitted as PR title_ `SCALER_24BCS10220`.

A comparison of two relational databases that sit at opposite ends of the
design space. The point of this document is not to list features but to explain
*why* each system is built the way it is: almost every difference below follows
from one root decision — **embedded library vs. client-server server** — and the
workloads each was created to serve.

All measurements in the Experiments section were produced locally on identical
data (a 1,000,000-row table) using PostgreSQL 18.3 and SQLite 3.51.

## Problem Background

The two systems were designed for different problems, and that intent explains
their architectures.

**SQLite (2000)** was written to be an *embedded* database: a C library that
links directly into an application and stores the whole database in a single
ordinary file. Its design goal was to replace ad-hoc use of `fopen()` and
custom file formats — it targets phones, browsers, desktop apps, IoT devices,
and application-local storage where there is exactly one application and no
database administrator. The guiding constraints are *zero configuration, no
separate server process, and portability of the file*.

**PostgreSQL (1986 as POSTGRES at Berkeley; 1996 as PostgreSQL)** was designed
as a *client-server* relational database for multi-user, concurrent, long-lived
workloads: many clients connecting over a network to a managed server that
guards shared data. Its guiding constraints are *concurrency, correctness under
contention, extensibility, and standards compliance*.

So the fork is not "one is better." SQLite optimizes for *one application,
in-process, simple*; PostgreSQL optimizes for *many concurrent clients, a
managed server*. Concurrency model, process model, storage layout, type system,
and durability knobs all descend from this single difference.

## Architecture Overview

```
        SQLite (embedded)                     PostgreSQL (client-server)

  ┌───────────────────────────┐        client(s) ── TCP / Unix socket
  │      Application process   │                         │
  │  ┌─────────────────────┐   │            ┌────────────┴────────────┐
  │  │  app code           │   │            │  postmaster (listener)  │
  │  │   │ function calls   │   │            └────────────┬────────────┘
  │  │   ▼                 │   │                  forks one backend per connection
  │  │  libsqlite          │   │            ┌────────────┴────────────┐
  │  │   • SQL compiler    │   │            │ backend: parse→plan→exec│
  │  │   • VDBE (bytecode) │   │            └────────────┬────────────┘
  │  │   • B-tree + pager  │   │                         │
  │  │   • VFS (OS layer)  │   │     ┌───────────────────┴───────────────────┐
  │  └─────────┬───────────┘   │     │  shared memory: shared_buffers, WAL    │
  └────────────┼───────────────┘     │  buffers, lock table                   │
               ▼                      └───────────────────┬───────────────────┘
        one database file              background procs:  │ checkpointer,
        (+ -wal, -shm)                 bgwriter, walwriter, autovacuum
                                                          ▼
                                          data directory (heap files, indexes, WAL)
```

**Components — SQLite:** there is no server. The engine is a library compiled
into the application. SQL is compiled to bytecode for a small virtual machine
(the VDBE), which calls a B-tree module, which calls a *pager* (cache +
transaction control), which calls the OS through a thin Virtual File System
(VFS). The entire database — tables, indexes, schema — is one file.

**Components — PostgreSQL:** a `postmaster` process listens for connections and
forks a dedicated *backend* process for each client. Backends share a region of
shared memory (the buffer pool `shared_buffers`, WAL buffers, the lock table)
and cooperate with background processes — the checkpointer, background writer,
WAL writer, and autovacuum — that I observed running in the live cluster.

**Data flow — SQLite:** `SQL → prepare (compile to VDBE) → execute → pager →
VFS → file`. Every step is an ordinary function call inside the application's
address space; there is no inter-process communication.

**Data flow — PostgreSQL:** `client → socket → backend → parser → planner →
executor → access methods → buffer manager → shared buffers / WAL → disk`, and
the result travels back over the socket. Every query crosses a process boundary.

## Internal Design

**Storage.** SQLite keeps everything in one file as a forest of B-trees; the
*table itself is a B-tree clustered on the rowid* (an `INTEGER PRIMARY KEY`
*is* the rowid). Rows are variable-length records packed with varint encoding,
default 4 KB pages. PostgreSQL stores each table as a *heap* — an unordered
collection of 8 KB pages in per-relation files inside a data directory. Each
heap tuple carries a ~23-byte header plus MVCC bookkeeping, and large values are
pushed out-of-line via TOAST. Consequence: in SQLite the primary-key data is
stored once (it is the table); in PostgreSQL the data lives in the heap *and*
again in the primary-key index. This shows up directly in on-disk size
(Experiments).

**Memory.** SQLite uses a per-connection page cache (a couple of MB by default)
living in the application's own heap; by default two connections do not share a
cache. PostgreSQL uses `shared_buffers` — a single buffer pool in shared memory
that *all* backends use, with a clock-sweep replacement policy, layered on top
of the OS page cache. This is a direct corollary of the process model: SQLite
has nothing to share a cache *with*; PostgreSQL must.

**Indexing.** Both use B-tree indexes. The difference is clustering: SQLite's
table is the rowid B-tree, so the primary key needs no separate structure and
secondary indexes store the rowid as their pointer. In PostgreSQL *every* index
is secondary — even the primary key — and stores a `ctid` (page, slot) pointing
into the heap. SQLite's `EXPLAIN QUERY PLAN` confirms this with
`SEARCH ... USING INTEGER PRIMARY KEY`, whereas PostgreSQL reports a separate
`Index Scan using accounts_pkey`.

**Transactions.** Both are fully ACID. SQLite makes a transaction durable by
controlling the file with a *journal*: the default rollback journal writes
undo information to a side file, or WAL mode appends new pages to a `-wal` file.
A commit is an atomic file operation guarded by `fsync`. PostgreSQL implements
transactions through MVCC plus a redo WAL (below).

**Concurrency.** This is the sharpest divergence. SQLite allows **one writer at
a time** — a write takes a database-level lock; in WAL mode readers may run
concurrently with the single writer, but two writers cannot proceed together.
PostgreSQL uses **MVCC**: each `UPDATE`/`DELETE` creates a new row version
stamped with transaction ids, readers see a consistent snapshot without taking
locks, and many writers proceed concurrently using row-level locks. SQLite
deliberately trades write concurrency for simplicity (correct for one
application); PostgreSQL pays storage and vacuum costs to buy concurrency.

**Recovery.** SQLite recovers using its journal: after a crash, an incomplete
transaction is rolled back from the rollback journal, or replayed/truncated from
the `-wal` file, restoring an atomic state. PostgreSQL uses write-ahead logging
with checkpoints — every change is logged before its page is written, and on
restart the WAL is replayed (redo) and uncommitted work undone. Both obey the
write-ahead/journal-ahead principle; they differ in *redo* (PostgreSQL replays
forward) vs SQLite's primarily *undo*-style rollback journal.

## Design Trade-Offs

**SQLite — advantages.** Zero configuration and no server to run or secure; no
IPC, so calls are in-process and extremely cheap; the entire database is a
single portable file you can copy or embed; a tiny footprint; very few moving
parts to fail. Ideal for embedded systems, edge/offline apps, browser and mobile
storage, application file formats, and test fixtures.

**SQLite — limitations.** Only one writer at a time, so write-heavy concurrent
workloads serialize; no built-in network access (clients must share the file,
which is unsafe over network filesystems); dynamic typing via "type affinity"
rather than strict types; limited `ALTER TABLE`; and no MVCC, so it does not
scale to many concurrent writers.

**PostgreSQL — advantages.** High concurrency through MVCC; genuine client-server
access for many remote clients; a rich, strict, extensible type system; a mature
cost-based optimizer; and replication and high-availability features.

**PostgreSQL — limitations.** Operationally heavy — a server to configure,
monitor, and secure; a process and memory cost per connection (which is why
connection pooling is standard practice); larger on-disk footprint and the need
to vacuum dead row versions created by MVCC; and a network/IPC round trip on
every query.

**Performance implications.** For a single user doing local point queries,
SQLite's in-process model wins because it pays no IPC. For storage, MVCC and
duplicated PK storage make PostgreSQL noticeably larger on the same data. For
*concurrent writers*, the comparison inverts: SQLite's single-writer lock
serializes them while PostgreSQL's MVCC lets them run in parallel — the workload
PostgreSQL was built for.

## Experiments / Observations

**Setup.** Identical schema `accounts(id INTEGER PRIMARY KEY, branch, balance,
note)` with 1,000,000 rows and a secondary index on `branch`. PostgreSQL 18.3
with `shared_buffers=64MB`; SQLite 3.51 in WAL mode. PostgreSQL timings are from
`EXPLAIN (ANALYZE, BUFFERS)` (server-side execution only); SQLite timings are
from `.timer` (whole in-process call).

| Observation | PostgreSQL 18.3 | SQLite 3.51 |
|---|---|---|
| Build 1M rows (single transaction) | ~2.63 s (insert; index built separately) | ~0.69 s (insert **and** index) |
| PK point lookup `id=500000` | 0.015 ms exec, 4 buffer hits (`Index Scan`) — plus a socket round trip not shown | ~0.01 ms (`SEARCH USING INTEGER PRIMARY KEY`, in-process) |
| Indexed lookup `branch=42` (1000 rows) | 2.63 ms, ~1004 pages (`Bitmap Index Scan` + heap fetch) | ~0.03 ms (`COVERING INDEX`, no table fetch)\* |
| Full scan `count(*) WHERE balance>90000` (~100k rows) | 14–26 ms (`Parallel Seq Scan`, 2 workers, 6370 pages) | ~24 ms (single-threaded scan) |
| On-disk size, same data | ~100 MB (heap 60 + PK index 26 + branch index 15, separate files) | ~37 MB (one file; table clustered on PK) |
| OS processes | postmaster + checkpointer, bgwriter, walwriter, autovacuum, io workers | none — a library in the app process |
| WAL for 200k inserts | 44 MB (~231 bytes/row) | (journal/WAL file, truncated at checkpoint) |

\* Not a like-for-like number: the SQLite query was `count(*)`, which `branch`'s
index covers (no row fetch), while the PostgreSQL query fetched full rows. The
honest takeaway is *which plan each optimizer chose*, not the raw millisecond
gap.

**Interpretation.**

- **Storage (100 MB vs 37 MB)** is the clearest architectural signal. PostgreSQL
  stores the data in the heap and *again* in the primary-key index, adds a
  ~23-byte tuple header and MVCC fields per row, and keeps each index in its own
  file. SQLite clusters the table on the integer primary key (stored once) and
  packs rows with varints — roughly a third of the size here.
- **Process model.** PostgreSQL launched a family of cooperating processes; SQLite
  launched nothing. This is the embedded-vs-server divide made visible, and it is
  why SQLite needs no shared buffer pool and PostgreSQL does.
- **Bulk load.** With a single writer, SQLite finished faster, which is expected:
  no IPC, lighter per-row overhead, and less WAL volume (PostgreSQL generated
  44 MB of WAL for just 200k inserts). This advantage would not hold under many
  concurrent writers, which SQLite serializes and PostgreSQL does not.
- **The PostgreSQL latencies exclude the connection round trip** that every real
  client pays, whereas SQLite's numbers are the complete in-process cost — so
  SQLite's true latency advantage for single-client point queries is larger than
  the table alone suggests.

## Key Learnings

- The whole comparison reduces to **one decision — embedded library vs.
  client-server** — and almost everything else (concurrency model, process and
  memory architecture, storage overhead, durability mechanism) is a logical
  consequence of it. Reasoning from that root explains the systems far better
  than memorizing their feature lists.
- **The concurrency model is the decisive practical difference.** SQLite's
  single-writer lock is a feature, not a defect: for an embedded, mostly-local
  workload it removes enormous complexity. PostgreSQL's MVCC is what makes it a
  multi-user server, and the storage bloat and vacuuming are the price paid for
  it — the 100 MB vs 37 MB measurement is that price made concrete.
- **"Better" is workload-dependent.** SQLite wins for embedded, single-writer,
  local, zero-admin use and for raw single-client latency; PostgreSQL wins for
  concurrent multi-client access, network reach, rich types, and extensibility.
  Choosing between them is choosing a workload, not ranking quality.
- Seeing the same query planned two ways — SQLite searching *through* the table's
  own primary-key B-tree versus PostgreSQL doing a separate index scan into a
  heap — made the clustered-vs-heap storage trade-off concrete in a way the
  documentation alone did not.

---

*Reproducibility:* the PostgreSQL figures come from `EXPLAIN (ANALYZE, BUFFERS)`
on the table above; the SQLite figures from `EXPLAIN QUERY PLAN` and `.timer on`
over the identical dataset.
