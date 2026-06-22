# PostgreSQL vs SQLite: An Architecture Comparison

*Advanced DBMS — System Design Document*

A study of two relational databases that solve the *same* problem (durable, transactional, SQL-queryable storage) with almost *opposite* architectures. The interesting question is not "which is better" but **which constraints each design optimizes for, and what each gives up in return.**

---

## 1. Problem Background

Both systems implement the relational model with SQL and ACID transactions, yet they were born from different problems.

### PostgreSQL — the research-grade server
PostgreSQL descends from **POSTGRES**, started in 1986 at UC Berkeley by Michael Stonebraker as a successor to the earlier **Ingres** project. POSTGRES explored "post-relational" ideas: extensible types, rules, and rich data modeling, with the database running as a long-lived **server process** that many clients connect to. The project gained SQL support in the mid-1990s (the "Postgres95" fork) and became the open-source **PostgreSQL** maintained by a global community.

The problem it solves: a **shared, concurrent, networked database** for many users and applications at once, where correctness under heavy concurrent writes, extensibility, and long-term durability dominate. The implicit assumption is that a *dedicated machine (or cluster) runs the database* and clients talk to it over a socket.

### SQLite — the embedded zero-config library
SQLite was created by **D. Richard Hipp in 2000**, originally for a system that needed a database with **no separate server to administer** — ideally one that could not "go down" independently of the application. The design goal was a self-contained, serverless, zero-configuration SQL engine that lives *inside* the application as a linked library and stores an entire database in **a single ordinary file**.

The problem it solves: give a *single application* reliable transactional SQL storage with **no installation, no daemon, no network, and no DBA**. It is explicitly positioned not as a replacement for client-server databases but as a replacement for `fopen()` — i.e., a better application file format.

> **Core divergence:** PostgreSQL assumes *many clients, one shared server*. SQLite assumes *one application, no server at all*. Almost every other difference follows from this single decision.

---

## 2. Architecture Overview

### PostgreSQL — client-server, multi-process

```
        TCP / Unix socket
  client ──┐  client ──┐  client ──┐
           ▼           ▼           ▼
     ┌───────────────────────────────────┐
     │            postmaster              │  (listener / supervisor)
     │   forks one backend per connection │
     └───────────────────────────────────┘
        │ fork        │ fork        │ fork
        ▼             ▼             ▼
   ┌─────────┐   ┌─────────┐   ┌─────────┐
   │ backend │   │ backend │   │ backend │   parse→plan→execute
   └────┬────┘   └────┬────┘   └────┬────┘
        └──────┬──────┴──────┬──────┘
               ▼             ▼
        ┌──────────────────────────────┐
        │  SHARED MEMORY                │
        │   shared_buffers (page cache) │◄── all backends read/write here
        │   WAL buffers, locks, etc.    │
        └───────┬───────────────┬───────┘
                ▼               ▼
     ┌────────────────┐  ┌──────────────┐
     │ background      │  │ WAL writer / │
     │ writer +        │  │ checkpointer │
     │ autovacuum etc. │  └──────┬───────┘
     └───────┬─────────┘         ▼
             ▼              pg_wal/  (write-ahead log)
        base/  (heap + index files on disk)
```

Key components:
- **postmaster**: master process; listens, authenticates, and `fork()`s a dedicated **backend** per client connection.
- **Backend process**: one OS process per connection that parses, plans, and executes SQL. Process isolation means a crash in one backend can be contained.
- **Shared memory**: `shared_buffers` is a shared page cache used by *all* backends — this is how concurrent sessions see each other's committed data.
- **WAL (Write-Ahead Log)**: changes are logged before data files are updated, enabling crash recovery and replication.
- **Background processes**: **background writer** (flushes dirty buffers), **checkpointer** (periodically reconciles buffers + WAL to data files), **autovacuum** (reclaims dead MVCC tuples), WAL writer, stats collector, replication walsenders.

### SQLite — embedded, in-process, single file

```
   ┌─────────────────────────────────────────┐
   │            Application process            │
   │                                           │
   │   app code ──► SQLite library (linked)    │
   │                 ├─ SQL compiler (→ VDBE)  │
   │                 ├─ VDBE bytecode engine   │
   │                 ├─ B-tree layer           │
   │                 ├─ pager (cache + txn)    │
   │                 └─ OS interface (VFS)      │
   └───────────────────────┬───────────────────┘
                            │ ordinary file I/O + locks
                            ▼
                ┌────────────────────────┐
                │  mydb.sqlite (1 file)   │
                │  + journal / -wal file  │
                └────────────────────────┘
```

There is **no server, no separate process, no socket**. The query goes: SQL text → tokenizer/parser → code generator → **VDBE** (a virtual machine that runs bytecode) → **B-tree** layer → **pager** (page cache + transaction/locking control) → **VFS** (OS file abstraction) → the file. "Connecting to the database" is just opening a file.

---

## 3. Internal Design

### 3.1 On-disk file organization

| Aspect | PostgreSQL | SQLite |
|---|---|---|
| Storage unit | One or more files **per table/index** under `base/<dboid>/` | The **entire database in one file** |
| Page size | **8 KB** default (compile-time configurable) | **4 KB** default (512 B–64 KB) |
| Page layout | Header → array of item pointers (line pointers) → free space → tuples growing from the end | File header (first page) → B-tree pages; cells grow from end, pointer array from start |
| Large values | **TOAST** (oversized attributes stored out-of-line/compressed) | Overflow pages chained from the cell |

Both lay out a page as a *slotted page*: a pointer array at the top and variable-length records filling from the bottom, with free space in the middle. Both use **B-trees** as the primary index structure (and SQLite stores tables themselves as B-trees keyed by rowid). PostgreSQL additionally offers GiST, GIN, BRIN, and hash indexes — extensibility inherited from its research roots.

### 3.2 Concurrency control — the central difference

**PostgreSQL: MVCC (Multi-Version Concurrency Control).**
Every row version (heap tuple) carries hidden system columns, most importantly **`xmin`** (the transaction id that created the version) and **`xmax`** (the transaction id that deleted/superseded it). An `UPDATE` does *not* overwrite in place — it writes a **new tuple version** and marks the old one's `xmax`. Each transaction sees a **snapshot**: a tuple is visible if its `xmin` committed before the snapshot and its `xmax` has not committed (relative to that snapshot).

Consequences:
- **Readers never block writers and writers never block readers.** A reader sees a consistent older version while a writer creates a new one.
- Many writers can proceed concurrently as long as they touch different rows; conflicts on the *same* row are serialized via row locks.
- The cost: **dead tuples accumulate** and must be reclaimed by **VACUUM/autovacuum**. Transaction ids are 32-bit, requiring "freezing" to avoid wraparound. MVCC trades disk/cleanup overhead for concurrency.

**SQLite: file-level (database-level) locking.**
SQLite has no per-row versioning for concurrent writers. It uses coarse locks on the database file. In the classic **rollback-journal** mode, the lock states escalate:

```
UNLOCKED → SHARED (readers) → RESERVED → PENDING → EXCLUSIVE (writer)
```

Multiple readers can hold SHARED locks simultaneously, but a writer must eventually obtain an **EXCLUSIVE** lock, which blocks all readers. In rollback-journal mode, SQLite first copies the original pages into a **`-journal`** file, then modifies the database in place; on crash, the journal is replayed to roll *back* to the consistent state.

**WAL mode** (`PRAGMA journal_mode=WAL`) improves this: new changes are appended to a **`-wal`** file instead of overwriting the main file. Now **readers and one writer can run concurrently** — readers read the original file plus a snapshot of the WAL. But there is still **at most one writer at a time** for the whole database. A second writer gets `SQLITE_BUSY`.

> **The defining contrast:** PostgreSQL = *many concurrent writers* via per-row MVCC. SQLite = *one writer at a time* via whole-file locking (with concurrent readers in WAL mode).

### 3.3 Transactions, durability, and recovery

- **PostgreSQL**: WAL records every change before the data page is flushed. **Checkpoints** flush dirty buffers and record a known-good point; on restart, WAL is **replayed from the last checkpoint** (REDO). Commit durability is governed by `synchronous_commit` / `wal_sync_method`. WAL also powers **streaming replication** and point-in-time recovery — a direct benefit of having a centralized server.
- **SQLite**: durability comes from the journal/WAL file plus carefully ordered `fsync()`s through the VFS. Recovery happens automatically on the next open: a leftover rollback journal is replayed to undo a half-finished transaction, or a `-wal` file is checkpointed back into the main database.

### 3.4 Memory management
- **PostgreSQL**: a *shared* `shared_buffers` cache visible to all backends, plus per-backend `work_mem` for sorts/hashes and `maintenance_work_mem`. Sharing buffers across processes is exactly what enables a coherent multi-user view.
- **SQLite**: a *per-connection* page cache (`PRAGMA cache_size`) inside the application's own address space. There is no cross-process shared cache (shared-cache mode aside); the OS file cache provides the second tier.

---

## 4. Design Trade-Offs

| Dimension | PostgreSQL (client-server) | SQLite (embedded) |
|---|---|---|
| Deployment | Requires a running server, config, often a DBA | Zero-config; link a library, open a file |
| Concurrency | Many concurrent writers (MVCC) | One writer at a time (file lock) |
| Network access | Native (remote clients) | None — same machine, same file |
| Per-query latency | Adds IPC/parse/plan + network round trip | In-process function call; extremely low overhead |
| Scalability | Vertical + replication, large datasets | Bounded by single machine / single file |
| Crash blast radius | Isolated backend processes; server can outlive a client | DB shares the app's process and lifecycle |
| Footprint | Hundreds of MB, background workers | A single ~1 MB library, no daemons |
| Extensibility | Rich type system, custom indexes, procedural langs | Deliberately minimal, app-controlled |

**Why PostgreSQL for large multi-user systems.** The client-server model centralizes the data so dozens to thousands of clients share one authoritative copy with consistent visibility. MVCC lets those clients write concurrently without serializing the whole database, and the server can sophisticatedly plan queries, enforce constraints centrally, run replication, and survive individual client crashes. The cost — a daemon, memory, administration, network hop — is negligible when amortized over a large workload.

**Why SQLite for mobile/embedded apps.** A phone app typically has **one process** reading and writing **its own local data**, often offline, with no operator to run a server. SQLite's serverless, single-file design means *no setup, no IPC, no network*, a tiny footprint, and a database that is just a file you can copy or back up. The single-writer limit is rarely a problem because there is usually only one writer (the app) anyway, and per-call latency is excellent because it's an in-process function call rather than a socket round trip.

**The architectural decision behind it all:** *server process vs linked library*. Once PostgreSQL chose a shared server, MVCC and shared buffers became necessary to let many connections coexist. Once SQLite chose to be a library with no server, coarse file locking became sufficient (there's typically one writer) and *desirable* (simple, no coordination daemon). Each system's "limitations" are the deliberate price of its target environment.

---

## 5. Experiments / Observations

*The following are illustrative observations of the kind one would see when probing each engine; exact numbers vary by hardware and settings.*

### 5.1 PostgreSQL query plan (`EXPLAIN ANALYZE`)
```sql
EXPLAIN ANALYZE SELECT * FROM orders WHERE customer_id = 4242;
```
```
 Index Scan using orders_customer_id_idx on orders
   (cost=0.43..18.power rows=12 width=64)
   (actual time=0.038..0.061 rows=11 loops=1)
   Index Cond: (customer_id = 4242)
 Planning Time: 0.21 ms
 Execution Time: 0.09 ms
```
**Observation:** the cost-based planner chose a B-tree **Index Scan** and reports *both* estimated (`cost`, `rows`) and measured (`actual time`, `rows`) values. Without the index, the same query shows a `Seq Scan` with a much higher cost — visible evidence of the planner reasoning about access paths.

### 5.2 SQLite schema and plan
```sql
.schema orders
-- CREATE TABLE orders(id INTEGER PRIMARY KEY, customer_id INT, total REAL);
-- CREATE INDEX orders_cust ON orders(customer_id);

EXPLAIN QUERY PLAN SELECT * FROM orders WHERE customer_id = 4242;
```
```
QUERY PLAN
`--SEARCH orders USING INDEX orders_cust (customer_id=?)
```
**Observation:** SQLite reports `SEARCH ... USING INDEX` (vs `SCAN orders` for a full-table scan). It does not print measured timings — consistent with its minimalist design. The plan output is qualitative, not cost-annotated.

### 5.3 Concurrent writes
- **SQLite:** open two connections, `BEGIN` a write on both. The second `INSERT`/`UPDATE` returns **`SQLITE_BUSY` ("database is locked")** because only one writer can hold the lock. Enabling WAL mode (`PRAGMA journal_mode=WAL;`) lets *readers* continue during a write but still rejects a *second concurrent writer*. The fix in practice is a `busy_timeout` so the second writer retries.
- **PostgreSQL:** two sessions updating *different* rows both commit without blocking (MVCC). Two sessions updating the *same* row: the second blocks until the first commits, then proceeds — row-level, not database-level, contention.

### 5.4 File / storage observations
- **SQLite:** after `PRAGMA journal_mode=WAL`, a `-wal` and `-shm` file appear next to the database; the main file only grows after a checkpoint. After bulk deletes, the file does **not** shrink until `VACUUM`, mirroring page-reuse behavior.
- **PostgreSQL:** repeated `UPDATE`s grow the table on disk because old tuple versions linger until autovacuum runs — directly observable as table bloat in `pg_stat_user_tables` (`n_dead_tup`). This is MVCC's storage cost made visible.

---

## 6. Key Learnings

1. **One decision cascades through the whole design.** "Server process" vs "linked library" is the root from which concurrency model, memory layout, durability machinery, and deployment story all follow. Reading either codebase, almost every difference traces back to this.
2. **Coarse locking is not a bug — it's a fit.** SQLite's single-writer limit looks primitive next to MVCC, but for one-app/one-writer workloads it is *simpler, smaller, and faster*, with no vacuum and no background daemons. The "weaker" mechanism is the right engineering choice for its target.
3. **MVCC buys concurrency with maintenance debt.** Non-blocking readers/writers are powerful, but they create dead tuples, the need for VACUUM, and transaction-id management. There is no free concurrency.
4. **They occupy different niches, not a quality ladder.** SQLite is "a replacement for `fopen()`"; PostgreSQL is "a shared database server." Comparing them on raw concurrency or footprint is comparing tools built for different physics.
5. **Both still rely on the same fundamentals.** B-trees, slotted pages, write-ahead logging, and snapshot/locking-based isolation appear in both — a reminder that the hard, shared core of a DBMS is durable storage + correct concurrency, and the *architecture* is mainly about who runs it and for how many clients.
6. **Surprising takeaway:** the same `EXPLAIN`-family tooling exists in both, but their *output* reveals their philosophy — PostgreSQL exposes a rich cost model because a server must choose plans well across diverse workloads, while SQLite gives terse, deterministic plans because it optimizes for predictability and small size.

---

*Concepts referenced generically from the official PostgreSQL documentation and the SQLite documentation; all explanations, diagrams, and observations here are written for this assignment.*
