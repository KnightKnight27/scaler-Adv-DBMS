# PostgreSQL vs SQLite: Architecture Comparison

**Name:** Om Malviya &nbsp;|&nbsp; **Roll Number:** 24BCS10448

> Two relational databases, one fundamental design question: where does the database engine live relative to your application? **PostgreSQL** runs as its own server process ecosystem, built for many concurrent users. **SQLite** is an embedded library inside your application, with no separate process, no network socket, and no daemon. That single architectural decision ripples through how each system handles storage, concurrency, durability, and scalability.

All experiments in this document were run locally; the commands and their real output are quoted inline in [Section 5](#5-experiments--observations). Versions: PostgreSQL **16.14**, SQLite **3.51.0** (macOS, arm64).

---

## 1. Problem Background

Relational databases all speak SQL, but they are not built for the same deployment.

**SQLite (2000, D. Richard Hipp).** Originally written for a US Navy program that ran on guided-missile destroyers where a database *server* could not be assumed to exist. The goal was a SQL engine with zero administration, zero configuration, and zero separate process: something an application could simply link against, like `libc`. SQLite's tagline is that it is *"not a replacement for Oracle, it is a replacement for `fopen()`"*: it competes with reading and writing application files, not with database servers.

**PostgreSQL (1986, UC Berkeley POSTGRES, then 1996 PostgreSQL).** Descended from Ingres, POSTGRES was designed to be a full-featured object-relational database for many concurrent users, with extensibility (custom types, operators, index methods), strong transactional guarantees, and ANSI-SQL compliance. It runs as a long-lived server that mediates access from many clients at once.

So the two solve different problems:

| | SQLite | PostgreSQL |
|---|---|---|
| **Problem solved** | Embed SQL inside one app, no DBA | Serve many concurrent clients reliably |
| **Competes with** | `fopen()`, app config/data files | Oracle, SQL Server, MySQL |
| **Unit of deployment** | A single `.db` file + a linked library | A server process + a data directory |

---

## 2. Architecture Overview

### PostgreSQL: client-server, process-per-connection

```
     Client 1          Client 2          Client 3
      (psql)          (app / ORM)         (app)
        │                 │                 │
        └──────── TCP / Unix-domain socket ─┘
                          │
                          ▼
  ┌──────────────────────────────────────────────────────┐
  │  PostgreSQL server  (one OS process tree)              │
  │                                                        │
  │    postmaster ──┬── backend process  (client 1)        │
  │    (listener)   ├── backend process  (client 2)        │
  │                 └── backend process  (client 3)        │
  │                          │                             │
  │       ┌──────────────────┴───────────────────┐        │
  │       │   SHARED MEMORY                        │        │
  │       │     • shared_buffers   (8 KB pages)    │        │
  │       │     • WAL buffers      • lock table    │        │
  │       └──────────────────┬───────────────────┘        │
  │                          │                             │
  │   checkpointer · background writer · walwriter ·       │
  │   autovacuum            (background processes)         │
  └──────────────────────────┬─────────────────────────────┘
                             │  read / write 8 KB pages
                              ▼
        Data directory:   base/   pg_wal/   global/  ...
```

Every connection gets its own backend OS process; they coordinate through a region of shared memory (shared buffers, WAL buffers, lock tables). Dedicated background processes handle checkpointing, dirty-page flushing, WAL writing, and vacuuming. The process list is captured live in [Experiment A](#experiment-a--process-model--file-layout).

Each background daemon exists for a specific reason:

| Daemon | Purpose |
|---|---|
| checkpointer | Flushes dirty pages to disk periodically; sets WAL recovery boundary |
| background writer | Proactively writes dirty pages so queries are not interrupted by I/O bursts |
| walwriter | Flushes WAL buffers to disk; makes COMMIT durable before client gets response |
| autovacuum launcher | Spawns workers to run VACUUM + ANALYZE; prevents dead-tuple accumulation |

### SQLite: embedded, in-process library

```
  ┌──────────────────────────────────────────────────┐
  │  Your application   (single OS process)            │
  │                                                    │
  │     application code                               │
  │            │  function calls (C API)               │
  │            ▼                                        │
  │     libsqlite3                                      │
  │     SQL compiler → VDBE → B-tree → pager            │
  │            │  read() / write() syscalls             │
  └────────────┼────────────────────────────────────────┘
               ▼
        app.db   ← ONE file  (+ -wal / -shm in WAL mode)

   No server. No socket. Nothing shows up in `ps`.
```

There is no server, no socket, no separate process. The application calls SQLite functions directly; SQLite reads and writes one database file using ordinary file-system syscalls. "Data flow" is just a function-call stack inside your process.

---

## 3. Internal Design

### Storage engine and file organization

| Aspect | PostgreSQL | SQLite |
|---|---|---|
| **On-disk unit** | A **directory** (`base/<dboid>/<relfilenode>`), one or more 1 GB files per table/index | A **single file** containing all tables + indexes |
| **Page size** | 8 KB (compile-time `BLCKSZ`) | 4 KB default (512 B to 64 KB) |
| **Table storage** | **Heap** files: rows in no guaranteed order; PK is a *separate* B-tree index pointing at heap tuples | Every table is a **B-tree keyed by `rowid`** (or by the `INTEGER PRIMARY KEY`); the table *is* the index |
| **Page layout** | Page header → line-pointer array (`ItemId`) → free space → tuples growing from the end | File header (first 100 B of page 1) → cell-pointer array → free space → cells growing from the end |
| **Indexes** | B-tree (default), plus Hash, GiST, GIN, BRIN, SP-GiST | B-tree only |

A key structural difference: PostgreSQL tables are heap-organized (the primary key is just another B-tree that references heap tuples by physical address `ctid`), whereas SQLite tables are index-organized on `rowid`: the row data lives in the leaves of the table B-tree. This is closer to InnoDB's clustered index than to PostgreSQL's heap.

### Memory management

- **PostgreSQL**: a fixed shared-buffer pool in shared memory is visible to all backends; it caches 8 KB pages and is managed by a clock-sweep replacement policy. Each backend also has private `work_mem` for sorts and hashes.
- **SQLite**: a per-connection page cache (`PRAGMA cache_size`) inside the application's own heap. There is no cross-process shared cache (in WAL mode a small `-shm` file coordinates readers/writers, but page data is still per-connection).

### Transaction processing and concurrency control

**PostgreSQL uses MVCC with row-level locking.** Each row version carries `xmin`/`xmax` transaction stamps; readers never block writers and writers never block readers. Two transactions can write *different* rows simultaneously (demonstrated in [Experiment C](#experiment-c--concurrent-writers-the-headline-result)). This is covered in depth in the companion topic, [PostgreSQL Internals](../PostgreSQL_Internals/README.md).

**SQLite uses coarse, file-level locking.** The classic rollback-journal mode uses a lock ladder `SHARED → RESERVED → PENDING → EXCLUSIVE` on the whole database file. There is at most one writer at a time for the entire database. WAL mode relaxes this to one writer + many concurrent readers, but still never more than one concurrent writer.

### Durability / recovery

Both use write-ahead logging, but at very different scales:

- **PostgreSQL** writes WAL records to 16 MB segment files in `pg_wal/` before dirty pages reach the heap; `synchronous_commit` + `fsync` guarantee committed transactions survive a crash, and recovery replays WAL from the last checkpoint.
- **SQLite** offers `journal_mode = DELETE` (rollback journal, the default) or `journal_mode = WAL` (redo log in a `-wal` file). Both make a single transaction atomic and durable across a power loss, scoped to one file.

---

## 4. Design Trade-Offs

| Dimension | PostgreSQL | SQLite |
|---|---|---|
| **Concurrency** | Many concurrent readers and writers (MVCC + row locks) | One writer at a time for the whole DB |
| **Setup / ops** | Needs a running server, a DBA, tuning, users/roles | Zero-config; copy one file to back up |
| **Multi-user / network** | Built for it (TCP, auth, connection pooling) | No network layer; access is in-process only |
| **Footprint** | Hundreds of MB resident, background processes | ~1 MB library, no daemon |
| **Write latency (single client)** | Network/IPC + process hop | Direct function call, no IPC |
| **Extensibility / SQL features** | Rich types, window funcs, CTEs, FDW, extensions | Smaller surface; dynamic typing |
| **Scaling ceiling** | Vertical + replication + partitioning | Bounded by one machine, one writer |

Why the difference exists: PostgreSQL pays a constant overhead (process startup, shared memory, IPC, background workers) to buy concurrency, isolation, and multi-user safety. SQLite refuses to pay that overhead, so it is astonishingly simple and fast for a single user, but it cannot offer true multi-writer concurrency because there is no central arbiter to coordinate requests. The file-level lock is its entire concurrency mechanism.

### Real-world use cases
- **SQLite**: mobile apps (iOS/Android ship it), browsers, IoT/edge devices, desktop app file formats, application caches, test fixtures, small websites.
- **PostgreSQL**: SaaS backends, analytics, multi-tenant systems, anything with many simultaneous users or that must scale beyond one machine.

---

## 5. Experiments / Observations

The dataset is an identical 3-table schema (10k customers, 50k orders, 200k order_items) loaded into both engines; outputs below are quoted directly from the live runs.

### Experiment A: Process model & file layout

PostgreSQL shows a tree of cooperating OS processes:
```
16959     1 .../bin/postgres          <- postmaster
16960 16959 postgres: checkpointer
16961 16959 postgres: background writer
16963 16959 postgres: walwriter
16964 16959 postgres: autovacuum launcher
18079 16959 postgres: ommalviya advdbms 127.0.0.1(63989) idle   <- my backend
```
and its data lives in a directory tree (`base/`, `pg_wal/`, `global/`, ...). SQLite, by contrast, has no process in `ps` at all: the whole database is one file:
```
9.3M app.db
```
> **Observation:** the architectural claim "client-server vs embedded" is visible directly in the OS. PostgreSQL is processes + a directory. SQLite is a function library + a file.

### Experiment B: Same join, two query planners

The same 3-table aggregation join (`country='IN' AND status='DELIVERED'`) returns identical results in both engines (`IN | 4165 orders | 249152860 revenue`), but the planners differ:

**PostgreSQL** (`EXPLAIN ANALYZE`) does a cost-based plan: bitmap index scan on `customers`, hash join to `orders`, then a nested-loop index scan into `order_items`, with row estimates vs. actuals and buffer counts:
```
GroupAggregate  (actual time=14.844..14.845 rows=1)
  -> Nested Loop  (actual rows=16660)
       -> Hash Join (Hash Cond: o.customer_id = c.customer_id)
            -> Seq Scan on orders (Filter: status='DELIVERED', Rows Removed: 37500)
            -> Bitmap Index Scan on idx_customers_country
       -> Index Scan using idx_items_order on order_items
Execution Time: 14.891 ms
```
**SQLite** (`EXPLAIN QUERY PLAN`) picks a simpler nested index-search strategy and even reports a *covering* index:
```
SEARCH c USING COVERING INDEX idx_customers_country (country=?)
SEARCH o USING INDEX idx_orders_customer (customer_id=?)
SEARCH oi USING INDEX idx_items_order (order_id=?)
USE TEMP B-TREE FOR count(DISTINCT)
```
> **Observation:** PostgreSQL's planner is richer (multiple join algorithms, cost model, runtime stats from `EXPLAIN ANALYZE`); SQLite's optimizer is deliberately leaner. Wall-clock was comparable at this size (~10-15 ms) because SQLite has no IPC/network hop.

### Experiment C: Concurrent writers (the headline result)

**SQLite:** writer 1 opens `BEGIN IMMEDIATE` (takes the write lock) and holds it; writer 2 tries to update a *different* row with `busy_timeout = 0`:
```
Runtime error near line 2: database is locked (5)
```
The second writer is rejected even though it touches a different row: the lock is on the whole file.

**PostgreSQL:** the identical scenario (writer 1 updates row 1, keeps transaction open; writer 2 updates row 2 concurrently):
```
writer2: committed while writer1 still open    <- writer 2 succeeds immediately
writer1: committed                             <- writer 1 commits afterward
```
Both writers commit. Row-level locking + MVCC means disjoint writes never collide.
> **Observation:** this single experiment is the clearest expression of the whole architectural divide. It is why you would never put SQLite behind a busy multi-user web service, and why PostgreSQL costs more to run.

### Experiment D: Durability / WAL

SQLite defaults to a `delete` (rollback) journal; switching to WAL creates the auxiliary files that enable concurrent readers during a write:
```
default journal_mode = delete
PRAGMA journal_mode=WAL; -> wal
   8192 waltest.db
  32768 waltest.db-shm    <- shared-memory index
      0 waltest.db-wal    <- write-ahead log
```
PostgreSQL ships WAL on by default (`wal_level=replica`, `fsync=on`, `synchronous_commit=on`) with 16 MB segments in `pg_wal/`:
```
wal_segment_size = 16777216 (16 MB)
pg_current_wal_lsn = 0/1196E218
16777216 00000001000000000000000E
16777216 00000001000000000000000F
```
> **Observation:** both achieve crash-safe durability via WAL, but PostgreSQL's WAL is also the backbone of replication and PITR, while SQLite's is a per-file mechanism with no notion of a cluster.

---

## 6. Key Learnings

1. **One decision dominates everything.** "Server process vs. in-process library" is the root cause of nearly every other difference: concurrency model, file layout, footprint, and scaling ceiling all follow from it.
2. **The lock granularity is the concurrency story.** SQLite's whole-file lock (Experiment C) is not a bug; it is the simplest possible concurrency control, and it is exactly why SQLite needs no shared memory or background coordinator. PostgreSQL pays for row-level MVCC with processes, shared buffers, and VACUUM.
3. **"Best" is workload-dependent.** SQLite was faster wall-clock for the single-client join because it has no IPC overhead. The moment you add concurrent writers, PostgreSQL wins decisively. Neither is universally better.
4. **Storage organization is a real divergence, not a detail.** PostgreSQL's heap + separate PK index vs. SQLite's rowid-clustered B-tree changes how lookups and range scans behave.
5. **Surprising observation:** SQLite is one of the most widely deployed pieces of software on Earth (billions of copies) precisely *because* it gave up multi-user concurrency: that trade-off is what let it become a zero-config library you can embed anywhere.

---

## Comparison Summary

| Metric | SQLite | PostgreSQL |
|---|---|---|
| Page / block size | 4 KB (OS-aligned default) | 8 KB (server I/O-optimized) |
| Table storage | rowid-clustered B-tree (table = index) | Heap-organized (PK is a secondary index) |
| Concurrent writers | 1 (file-level lock) | Many (row-level MVCC) |
| Background processes | 0 | 5+ daemons (checkpointer, walwriter, autovacuum...) |
| Buffer cache | OS page cache + per-connection PRAGMA | `shared_buffers` (shared across all backends) |
| WAL scope | Per-file, no replication concept | Backbone of replication and PITR |
| Setup | Zero config, embed and use | Server install, init, user/role setup |
| Architecture | In-process library | Client-server process ecosystem |
| Best fit | Mobile, embedded, local tools, single-user | Multi-user web apps, analytics, multi-tenant |

---

## Commands Reference

```bash
# SQLite
sqlite3 app.db
.tables
PRAGMA page_size;
PRAGMA page_count;
PRAGMA journal_mode;
PRAGMA journal_mode=WAL;
PRAGMA cache_size;
PRAGMA integrity_check;
EXPLAIN QUERY PLAN SELECT ...;

# PostgreSQL
psql -U <user> -d <db>
SHOW block_size;
SHOW shared_buffers;
SHOW wal_segment_size;
SELECT pg_current_wal_lsn();
\timing on
EXPLAIN ANALYZE SELECT ...;

# Process inspection
ps aux | grep postgres          -- see the process tree
ls -lh app.db                   -- SQLite is one file
ls $PGDATA/base/                -- PostgreSQL is a directory tree
```

---

## References
- PostgreSQL 16 Documentation: *Internals* (Database Physical Storage, MVCC, WAL): https://www.postgresql.org/docs/16/internals.html
- SQLite Documentation: *How SQLite Works*, *File Locking And Concurrency*, *Write-Ahead Logging*, *Appropriate Uses For SQLite*: https://www.sqlite.org/docs.html
- "Architecture of SQLite": https://www.sqlite.org/arch.html
- PostgreSQL source: `src/backend/storage/`, `src/backend/access/heap/`

*All analysis and experiment outputs above are original work produced by running the two engines locally (PostgreSQL 16.14, SQLite 3.51.0).*
