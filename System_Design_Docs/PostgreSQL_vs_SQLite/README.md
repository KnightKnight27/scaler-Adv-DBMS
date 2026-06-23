# PostgreSQL vs SQLite: An Architectural Comparison

> Advanced DBMS — System Design Discussion
> Topic 1: PostgreSQL vs SQLite Architecture Comparison

---

## Table of Contents

1. [Problem Background](#1-problem-background)
2. [Architecture Overview](#2-architecture-overview)
3. [Internal Design](#3-internal-design)
4. [Design Trade-Offs](#4-design-trade-offs)
5. [Experiments / Observations](#5-experiments--observations)
6. [Key Learnings](#6-key-learnings)
7. [References](#references)

---

## 1. Problem Background

Both PostgreSQL and SQLite are mature, widely deployed relational databases that
speak SQL — yet they were built to solve almost **opposite** problems. Comparing
them is instructive precisely because they make different choices at nearly every
architectural layer, and each choice is *correct* for the workload it targets.

### PostgreSQL

PostgreSQL traces its lineage to the **POSTGRES** project led by Michael Stonebraker
at UC Berkeley in 1986, which itself grew out of the earlier Ingres research. It was
designed as a general-purpose, **multi-user, server-class** RDBMS. The problem it
solves: *let many concurrent clients safely read and write a large shared dataset,
with strong correctness (ACID), rich data types, extensibility, and high
availability*. It is the database you reach for when many applications/users hit one
authoritative data store at the same time — OLTP backends, analytics, geospatial
(PostGIS), etc.

### SQLite

SQLite was created by **D. Richard Hipp in 2000**, originally to run on a Navy
destroyer's onboard system where a server process could not be relied upon. The
problem it solves: *give a single application transactional SQL storage with
zero configuration, zero administration, and zero separate server process*. It is
not a competitor to client-server databases — its own docs say it competes with
`fopen()`. It is the most widely deployed database engine in the world, shipping
inside every Android/iOS device, every major browser, most operating systems, and
countless desktop applications.

| | PostgreSQL | SQLite |
|---|---|---|
| Born | 1986 (Berkeley POSTGRES) | 2000 |
| Designed for | Many users, one shared server | One application, embedded |
| Deployment | Separate server process(es) | A library linked into your app |
| "Competes with" | Oracle, MySQL, SQL Server | `fopen()` |
| Database = | A cluster managed by a daemon | A single ordinary file |

---

## 2. Architecture Overview

### 2.1 PostgreSQL — Client-Server, Process-per-Connection

```
                         ┌──────────────────────────────────────────────┐
   Client A ──TCP/socket─┤                PostgreSQL Cluster              │
   Client B ──TCP/socket─┤                                                │
   Client C ──TCP/socket─┤   postmaster (supervisor, listens & forks)     │
                         │        │                                       │
                         │        ├── backend proc (1 per connection)     │
                         │        ├── backend proc                        │
                         │        ├── backend proc                        │
                         │        │                                       │
                         │   ┌────┴───────── Shared Memory ───────────┐   │
                         │   │  Shared Buffers (page cache)           │   │
                         │   │  WAL buffers, lock table, proc array   │   │
                         │   └────────────────────────────────────────┘   │
                         │        │                                       │
                         │   Background processes:                        │
                         │     • WAL writer    • Background writer        │
                         │     • Checkpointer  • Autovacuum launcher      │
                         │     • Archiver      • Stats collector          │
                         │        │                                       │
                         │   ┌────┴────┐   ┌──────────┐                   │
                         │   │ Data    │   │   WAL    │                   │
                         │   │ files   │   │  (pg_wal)│                   │
                         │   └─────────┘   └──────────┘                   │
                         └──────────────────────────────────────────────┘
```

Key points:
- A **postmaster** daemon owns shared memory and **forks a dedicated backend
  process per client connection**. PostgreSQL uses a *process* model, not a thread
  model (historically for isolation and portability).
- All backends share one big region of **shared memory** (shared buffers, WAL
  buffers, lock tables) so they cooperate on the same on-disk data.
- A fleet of **background processes** handles durability and maintenance
  asynchronously (WAL writer, checkpointer, background writer, autovacuum).

**Query lifecycle inside a backend:**
```
SQL text → Parser → Analyzer/Rewriter → Planner/Optimizer → Executor → result
                                              │
                                       uses pg_statistic
                                       (cost-based plan)
```

### 2.2 SQLite — Embedded, In-Process Library

```
        ┌───────────────────────────────────────────────┐
        │            Your Application Process            │
        │                                                │
        │   App code  ──calls──►  libsqlite3 (linked in) │
        │                              │                 │
        │        ┌─────────────────────┴──────────────┐  │
        │        │  Core: SQL Compiler → VDBE bytecode │  │
        │        │  B-tree module                      │  │
        │        │  Pager (cache + transactions + lock)│  │
        │        │  OS Interface (VFS)                 │  │
        │        └─────────────────────┬──────────────┘  │
        └──────────────────────────────┼─────────────────┘
                                        │ file syscalls
                              ┌─────────┴──────────┐
                              │  my_app.db (one    │
                              │  file) + -wal/-shm │
                              └────────────────────┘
```

Key points:
- **No server, no daemon, no IPC.** SQLite is a C library compiled directly into the
  host application. A function call — not a network round trip — executes SQL.
- A **database is a single file** on disk (plus a `-wal` and `-shm` sidecar in WAL
  mode). Backup = copy the file.
- The engine is a clean stack: **SQL compiler → VDBE (a bytecode virtual machine)
  → B-tree → Pager → VFS**. The VDBE executes the compiled query as bytecode
  instructions, which is an elegant, portable design.

---

## 3. Internal Design

### 3.1 Storage Engine & File Organization

**PostgreSQL**
- A "database cluster" is a directory tree (`PGDATA`). Each table/index is one or
  more **heap files** (a "relation"), split into 1 GB segments.
- Files are divided into **8 KB pages** (blocks). A page holds a header, an array
  of **item pointers (line pointers)** growing from the front, and the actual
  **tuples** growing from the back — the classic *slotted page* layout.
- Tables are stored as an **unordered heap**; row order is not the index order.
  A row's physical address is a **CTID** = `(block number, item offset)`.

```
PostgreSQL 8 KB heap page (slotted page):
┌──────────────┬───────────────┬───────────────┬──────────────┐
│ PageHeader   │ ItemId array →│  free space   │← tuples       │
│ (24 bytes)   │ (line ptrs)   │               │  (heap rows)  │
└──────────────┴───────────────┴───────────────┴──────────────┘
```

**SQLite**
- The whole database is **one file** built as a forest of B-trees, all sharing a
  fixed **page size** (default 4 KB).
- Page 1 holds the file header + the `sqlite_schema` table (the catalog).
- **Every table is itself a B-tree** keyed by `rowid` (or by the PRIMARY KEY for
  `WITHOUT ROWID` tables). This is a **clustered** layout — the table *is* the index.
  Indexes are separate B-trees whose leaves store the key + rowid.

> **Core structural difference:** PostgreSQL stores rows in an **unordered heap**
> with all indexes (even the primary key) pointing into it. SQLite **clusters** the
> row data inside the primary B-tree keyed by rowid. This single decision ripples
> through lookup cost, update cost, and index design (see §4).

### 3.2 Index Implementation

| | PostgreSQL | SQLite |
|---|---|---|
| Default index | B-tree (Lehman & Yao high-key variant) | B-tree |
| Other index types | Hash, GiST, GIN, BRIN, SP-GiST | Only B-tree (+ expression/partial) |
| Primary storage | Heap + separate index | Table *is* a clustered B-tree on rowid |
| Index → row | Index leaf stores **CTID** (heap location) | Index leaf stores **rowid** → second B-tree lookup |
| Covering / index-only | Yes (with visibility map) | Yes (when index covers all needed columns) |

PostgreSQL's secondary index points directly at the heap tuple's physical location
(CTID). SQLite's secondary index stores the rowid, so a non-covering lookup does a
B-tree search on the index, then a **second** B-tree search on the table to fetch the
row.

### 3.3 Transaction Management & Concurrency Control

This is where the two diverge most sharply.

**PostgreSQL — MVCC via multiple physical row versions**
- Every row carries hidden system columns **`xmin`** (transaction that inserted it)
  and **`xmax`** (transaction that deleted/updated it).
- An `UPDATE` does **not** overwrite the row in place. It writes a **new tuple
  version** and marks the old one's `xmax`. This is "append-mostly" heap storage.
- Each transaction takes a **snapshot**; visibility rules compare a tuple's
  `xmin`/`xmax` against the snapshot to decide which version this transaction sees.
- Result: **readers never block writers, writers never block readers.** Multiple
  versions coexist.
- The cost: dead tuples accumulate, so **`VACUUM`** must reclaim space and freeze old
  transaction IDs (to prevent XID wraparound). Autovacuum does this in the background.
- Concurrency is **highly granular** — row-level locks for writes, and reads
  generally take no row locks at all.

**SQLite — Coarse, file-level locking (or one writer in WAL mode)**
- Classic **rollback-journal** mode: a writer takes an **EXCLUSIVE lock on the whole
  database file**, blocking all other access while it writes. Durability comes from a
  rollback journal that lets a crashed transaction be undone.
- **WAL mode** (the modern default for concurrency) is much better: writes append to a
  `-wal` file, and **readers can proceed concurrently with one writer** by reading a
  consistent snapshot. But there is still **at most one writer at a time** for the
  whole database.
- There is no per-row MVCC and no `xmin`/`xmax` bookkeeping — far simpler, far less
  metadata, but far less write concurrency.

| Concept | PostgreSQL | SQLite |
|---|---|---|
| Concurrency model | Per-row MVCC, snapshots | Whole-file lock; WAL = 1 writer + many readers |
| Concurrent writers | Many (row-level) | **One**, database-wide |
| Readers vs writers | Never block each other | Don't block in WAL mode |
| Version cleanup | `VACUUM` / autovacuum | Not needed (in-place, no dead versions) |
| Isolation default | Read Committed (Serializable available, true SSI) | Serializable (a single writer makes this cheap) |

### 3.4 Durability & Recovery

Both use **write-ahead logging**, but at very different scales.

**PostgreSQL WAL**
- Every change is recorded in the **WAL** (`pg_wal`) *before* the dirty data page is
  flushed — the WAL is the source of truth for recovery.
- **Checkpoints** periodically flush all dirty buffers and record a safe restart
  point so the WAL doesn't have to be replayed from the beginning of time.
- On crash, PostgreSQL **redoes** WAL records from the last checkpoint to restore a
  consistent state.
- WAL also powers **streaming replication** and **point-in-time recovery (PITR)** via
  archiving — features that only matter in a multi-server world.

**SQLite**
- **Rollback journal mode:** before modifying a page, the original is copied to a
  `-journal` file; on crash, the journal is replayed to **undo** incomplete changes.
- **WAL mode:** changes are appended to a `-wal` file and later **checkpointed** back
  into the main database file. The `-shm` shared-memory file coordinates readers.
- Recovery is automatic and local — there is no replication, archiving, or PITR built
  in; durability is about surviving a power loss on one device, not a server fleet.

### 3.5 Memory Management

- **PostgreSQL:** A shared `shared_buffers` page cache lives in shared memory so *all*
  backends benefit from cached pages; per-backend `work_mem` handles sorts/hashes.
  Also leans on the OS page cache. Tuning these is a core DBA task.
- **SQLite:** A per-connection **page cache** in the Pager; size set by `cache_size`.
  No shared cross-process buffer pool — because there's usually one process anyway.

---

## 4. Design Trade-Offs

### 4.1 PostgreSQL

**Advantages**
- True multi-user concurrency: many simultaneous readers *and* writers via MVCC.
- Strong correctness: full ACID, real Serializable isolation (SSI), constraints,
  foreign keys, triggers.
- Extensible & feature-rich: custom types, many index types (GIN/GiST/BRIN), JSON,
  full-text, PostGIS, replication, PITR.
- Scales to large datasets and high availability.

**Limitations / costs**
- **Operational weight:** a server to install, configure, secure, back up, and tune.
- **MVCC bloat:** dead tuples require `VACUUM`; neglecting it causes table bloat and,
  in the extreme, transaction-ID wraparound risk.
- **Process-per-connection** is relatively heavyweight — thousands of connections
  need a pooler (PgBouncer); this is a known scaling pain point.
- Overkill (and a network hop of latency) for a single-app, embedded use case.

### 4.2 SQLite

**Advantages**
- **Zero-configuration, serverless, in-process:** no daemon, no ports, no DBA. SQL
  calls are function calls, so reads are *extremely* fast (no IPC/network).
- **Single-file** database: trivial to ship, copy, back up, embed, and version.
- Tiny footprint, extremely well-tested, rock-solid for its design point.
- Clustered B-tree storage gives fast primary-key lookups with no `VACUUM` needed.

**Limitations / costs**
- **One writer at a time** for the whole database — unsuitable for write-heavy,
  high-concurrency multi-user servers.
- No client-server access over a network (by design); no built-in users/roles,
  replication, or PITR.
- Flexible/dynamic typing ("type affinity") is more permissive than PostgreSQL's
  strict types (mitigated by `STRICT` tables in newer versions).
- Fewer index types and a simpler planner than PostgreSQL.

### 4.3 The MVCC vs Single-Writer trade-off, stated plainly

PostgreSQL *pays* — in storage bloat, vacuuming, and per-row metadata — to *buy*
high write concurrency and non-blocking reads. SQLite *refuses* that cost by allowing
only one writer, which keeps the engine tiny and simple. **Neither is "better"; each
optimizes for its workload.** A phone app with one process writing occasionally wants
SQLite's simplicity; a web service with hundreds of concurrent writers needs
PostgreSQL's MVCC.

---

## 5. Experiments / Observations

> The following are reproducible observations that illustrate the architectural
> claims above. Commands are included so a grader can re-run them.

### 5.1 SQLite is a file; PostgreSQL is a cluster

```bash
# SQLite: the entire database is one ordinary file. Back up by copying it.
sqlite3 students.db ".tables"
sqlite3 students.db ".schema"
cp students.db students_backup.db          # a complete, valid backup

# PostgreSQL: there is no single file — it's a managed directory + a daemon.
psql -c "SHOW data_directory;"              # e.g. /var/lib/postgresql/data
pg_dump mydb > mydb.sql                     # logical backup goes through the server
```
**Observation:** SQLite needs no running process to be read; PostgreSQL's data is only
safely accessible *through* its server.

### 5.2 Observing MVCC and dead tuples in PostgreSQL

```sql
-- Hidden version columns are visible if you ask for them:
SELECT xmin, xmax, ctid, * FROM accounts WHERE id = 1;

BEGIN;
UPDATE accounts SET balance = balance + 10 WHERE id = 1;
-- The row's ctid CHANGES — a new physical tuple was written, old one kept.
SELECT xmin, xmax, ctid FROM accounts WHERE id = 1;
COMMIT;

-- Dead tuples accumulate until VACUUM reclaims them:
SELECT relname, n_live_tup, n_dead_tup FROM pg_stat_user_tables;
VACUUM (VERBOSE) accounts;
```
**Observation:** an `UPDATE` does not modify the row in place — `ctid` moves and a dead
version is left behind. This is MVCC working, and it is *why* `VACUUM` exists.

### 5.3 Reading a query plan and the planner's use of statistics

```sql
ANALYZE;   -- refresh statistics into pg_statistic

EXPLAIN ANALYZE
SELECT s.name, c.title
FROM students s
JOIN enrollments e ON e.student_id = s.id
JOIN courses c     ON c.id = e.course_id
WHERE s.year = 2026;
```
Typical output to analyze:
```
Hash Join  (cost=... rows=… width=…) (actual time=… rows=… loops=1)
  -> Seq Scan on students s (... rows=… )   Filter: (year = 2026)
  -> Hash
       -> ...
Planning Time: 0.4 ms
Execution Time: 3.1 ms
```
**What to look at:** compare the planner's **estimated `rows`** (from `pg_statistic`,
populated by `ANALYZE`) against the **actual rows**. Large gaps mean stale or
insufficient statistics, which causes bad plan choices (e.g. nested loop vs hash
join). The chosen join algorithm is **cost-based**, driven by those statistics.

### 5.4 Concurrency observation

```
PostgreSQL:  open two psql sessions, BEGIN + UPDATE different rows in each.
             → Both succeed concurrently (row-level locks, MVCC).

SQLite:      open two connections, BEGIN IMMEDIATE in both.
             → The second gets "database is locked" / SQLITE_BUSY.
                Only one writer is allowed database-wide.
```
**Observation:** this single test makes the central trade-off concrete — PostgreSQL
allows concurrent writers to different rows; SQLite serializes all writes.

### 5.5 SQLite query plan (for completeness)

```sql
EXPLAIN QUERY PLAN
SELECT * FROM students WHERE id = 42;        -- uses the rowid clustered B-tree
EXPLAIN QUERY PLAN
SELECT * FROM students WHERE name = 'Abdur';  -- SCAN unless an index on name exists
```
**Observation:** a primary-key/rowid lookup is a single clustered B-tree search;
a non-indexed column triggers a full `SCAN`, demonstrating the clustered-storage model.

---

## 6. Key Learnings

1. **The client-server vs embedded split explains everything else.** PostgreSQL's
   process model, shared memory, background workers, replication, and operational
   surface all follow from "many users share one server." SQLite's single-file,
   in-process, single-writer simplicity all follows from "one app, no server."

2. **Storage layout is a fork in the road.** PostgreSQL's unordered heap + separate
   indexes enables flexible MVCC versioning; SQLite's clustered B-tree (table = index
   on rowid) gives fast key lookups and a compact file. Each enables and constrains
   the rest of the design.

3. **MVCC is a deliberate trade, not a free lunch.** PostgreSQL accepts row bloat and a
   mandatory `VACUUM` process in exchange for non-blocking, highly concurrent reads
   and writes. SQLite avoids that machinery entirely by permitting only one writer.

4. **Both honor ACID with write-ahead logging — at different scales.** WAL in
   PostgreSQL also underpins replication and PITR across servers; in SQLite it's about
   surviving a power cut on one device. Same principle, vastly different blast radius.

5. **"Best database" is a category error.** SQLite isn't a weaker PostgreSQL — it
   competes with `fopen()`. PostgreSQL isn't an over-engineered SQLite — it competes
   with Oracle. The right answer is entirely workload-dependent: embedded/single-user
   → SQLite; concurrent/multi-user/server → PostgreSQL.

6. **Surprising takeaway:** SQLite is the *most deployed* database on Earth precisely
   *because* it gave up server features — minimalism is its competitive advantage,
   not a limitation.

---

## References

- PostgreSQL Documentation — Internals (Storage, MVCC, WAL): https://www.postgresql.org/docs/current/internals.html
- *The Internals of PostgreSQL* — Hironobu Suzuki: https://www.interdb.jp/pg/
- PostgreSQL Source: `src/backend/storage/buffer/`, `src/backend/access/nbtree/`
- SQLite — Architecture of SQLite: https://www.sqlite.org/arch.html
- SQLite — Database File Format: https://www.sqlite.org/fileformat2.html
- SQLite — Write-Ahead Logging: https://www.sqlite.org/wal.html
- SQLite — "Appropriate Uses For SQLite": https://www.sqlite.org/whentouse.html

---

*Submitted for the Advanced DBMS System Design Discussion. All analysis and prose are
original; cited sources were used for fact-checking architectural details.*
