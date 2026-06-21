# PostgreSQL vs SQLite: A Comparative Architecture Study

## 1. Problem Background

Relational databases were built to solve one core problem: store structured data
reliably and let multiple queries reason about it through SQL, without the
application having to manage files, locking, or consistency by hand. But "store
data reliably" means very different things depending on where the database runs,
and PostgreSQL and SQLite are the clearest illustration of two opposite answers
to that question.

**PostgreSQL** descends from the POSTGRES project at Berkeley (1980s), built as a
general-purpose, extensible, multi-user object-relational database. Its design
target was always a shared server: many client connections, concurrent writers,
complex analytical queries, and strong durability guarantees, all running on
dedicated hardware that one process tree fully owns.

**SQLite** (2000, D. Richard Hipp) was built to solve a different problem
entirely: applications that need a real SQL engine but cannot justify running a
server process — embedded devices, mobile apps, browsers, local config stores.
SQLite's stated goal is to replace `fopen()`, not to replace PostgreSQL. It is
the database equivalent of a linked library, not a service.

This difference in *purpose* — "be the data layer for one process" vs "be the
data layer for many processes across a network" — is the root cause of nearly
every architectural difference discussed below. Almost nothing here is
arbitrary; each decision follows from the deployment target.

## Why These Databases Exist

Although both PostgreSQL and SQLite are relational databases, they were designed for fundamentally different deployment environments.

### Why PostgreSQL?

PostgreSQL was designed for:

- Multi-user systems
- Networked applications
- High concurrency workloads
- Enterprise-grade reliability

Its architecture prioritizes scalability and concurrent transaction processing, even at the cost of higher operational complexity.

### Why SQLite?

SQLite was designed for:

- Embedded systems
- Mobile applications
- Desktop software
- Environments where running a database server is impractical

Its architecture prioritizes simplicity, portability, and minimal resource consumption, even if that limits concurrency.

## 2. Architecture Overview

### PostgreSQL: Client-Server, Process-per-Connection

```
        ┌────────────┐        TCP/Unix Socket        ┌──────────────────────────┐
        │  Client A  │ ─────────────────────────────▶│   postmaster (listener)  │
        └────────────┘                               └─────────────┬────────────┘
                                                                   │ fork()
                                                                   ▼
        ┌────────────┐                                ┌─────────────────────────┐
        │  Client B  │ ─────────────────────────────▶ │   backend process #1    │
        └────────────┘                                │ (per-connection, owns   │
                                                      │  its own memory, talks  │
                                                      │  to shared buffers)     │
                                                      └────────────┬────────────┘
                                                                   │
                                                                   ▼
                                          ┌───────────────────────────────────────────────┐
                                          │        Shared Memory Segment                  │
                                          │  shared_buffers | WAL buffers | lock tables   │
                                          │  CLOG | proc array | stats collector          │
                                          └────────────────────────┬──────────────────────┘
                                                                   │
                                                                   ▼
                                          ┌─────────────────────────────────────────────────┐
                                          │ Background processes: WAL writer, checkpointer, │
                                          │autovacuum launcher/workers, bgwriter, stats     │
                                          └────────────────────────┬────────────────────────┘
                                                                   │
                                                          Disk (data files, WAL)
```

Every client connection gets its own OS process (a "backend"). Backends are
isolated from each other by the OS but coordinate through a shared memory
segment that holds the buffer pool, lock tables, and the process array used for
MVCC visibility checks. This is fundamentally a **multi-process, shared-memory**
architecture.

### SQLite: In-Process Library, No Server

```
        ┌───────────────────────────────────────────┐
        │              Host Application               │
        │  ┌─────────────────────────────────────┐    │
        │  │     SQLite library (linked .so/.a)    │    │
        │  │   SQL compiler → VDBE bytecode VM     │    │
        │  │   B-tree module → Pager → VFS layer    │    │
        │  └───────────────────┬─────────────────┘    │
        └──────────────────────┼──────────────────────┘
                                ▼
                     single .sqlite file on disk
                     (+ -wal / -journal file)
```

There is no server, no socket, no separate process. SQLite is a C library
linked directly into the calling application. A "connection" is just a struct
in the host process's own address space. Concurrency between *different
processes* accessing the same file is handled through OS-level file locks, not
through a coordinating server.

## 3. Internal Design

### 3.1 Process / Concurrency Model

| Aspect | PostgreSQL | SQLite |
|---|---|---|
| Execution unit | One OS process per client connection | One in-process call stack; no separate process |
| Communication | Sockets + shared memory segment | Direct function calls within the same address space |
| Multi-writer concurrency | Full MVCC, many concurrent writers | Single writer at a time (database-level write lock) |
| Network access | Yes — designed for remote clients | No — file must be on a locally accessible filesystem |

PostgreSQL's choice to fork a process per connection (rather than use threads)
was a 1990s-era design decision favoring fault isolation: a crashing backend
cannot corrupt another connection's memory, and the OS scheduler handles
preemption for free. The cost is heavier per-connection memory overhead, which
is why production deployments almost always sit a connection pooler (PgBouncer)
in front of Postgres.

SQLite avoids this problem entirely by not having connections that span
processes — there is nothing to pool.

### 3.2 Storage Engine and File Organization

**PostgreSQL** stores each table and index as one or more **heap files** on
disk, organized into a directory hierarchy under `PGDATA/base/<db_oid>/<table_filenode>`.
Each file is divided into fixed-size **8KB pages**. A page contains:
- A page header (LSN, checksum, free-space pointers)
- An array of item pointers (line pointers) growing from the front
- Tuple data growing from the back
- Free space in the middle

Tables are **heap-organized**: rows are not stored in any particular order
relative to a key. This is what allows Postgres's append-only MVCC update
strategy (see 3.4).

### 3.2.1 Database File Organization

PostgreSQL stores databases inside the PGDATA directory. Each database is assigned an Object Identifier (OID), and each table and index is represented by one or more physical files on disk.

Example:

PGDATA/
└── base/
    ├── database_oid/
    │   ├── table_filenode
    │   ├── index_filenode
    │   └── ...

Large tables may span multiple segment files.

This layout allows PostgreSQL to scale efficiently while supporting advanced storage management features.

SQLite stores the entire database inside a single file. Tables, indexes, schema definitions, and metadata are all represented as B-tree pages within that file.

This design makes SQLite databases extremely portable because copying a single file copies the entire database.

**SQLite** stores the *entire database* — every table, index, and piece of
metadata — inside a **single file**, also organized into fixed-size pages
(default 4096 bytes, configurable). The file's first page is the "schema page"
holding the `sqlite_master` table. All tables are physically **B-tree
structures themselves** — a SQLite table *is* a B-tree keyed by `rowid`, and
each leaf page holds the actual row data. There is no separate heap file; the
table's B-tree leaves are the heap.

This is a meaningful structural difference: in Postgres, indexes point at heap
tuples by physical location (TID); in SQLite, a table's primary structure is
already a B-tree, so a non-rowid index's leaf stores the indexed columns plus
the rowid, and a lookup means traversing the index B-tree, then traversing the
table B-tree by rowid — two tree descents, similar in spirit to a Postgres
secondary index, but no separate heap file is needed for the primary table
storage.


## 3.2.2 Disk Layout

### PostgreSQL

Disk layout consists of:

- Heap files
- Index files
- WAL files
- Configuration files
- System catalogs

These components are distributed across multiple directories within the PostgreSQL data directory.

### SQLite

Disk layout consists primarily of:

- Main database file
- WAL file (optional)
- Rollback journal (optional)

This simplified disk layout contributes significantly to SQLite's portability and ease of management.


## 3.2.3 Page Layout

### PostgreSQL Page Layout

PostgreSQL stores data in fixed-size pages, typically 8 KB.

Each page contains:

- Page Header
- Item Pointer Array
- Free Space
- Tuple Data

Example:

+--------------------+
| Page Header        |
+--------------------+
| Item Pointers      |
+--------------------+
| Free Space         |
+--------------------+
| Tuple Data         |
+--------------------+

The separation between item pointers and tuple data allows PostgreSQL to move tuples within a page without invalidating references.

### SQLite Page Layout

SQLite also stores data in pages, typically 4 KB by default.

Each page contains:

- Page Header
- Cell Pointer Array
- B-tree Cells
- Free Space

Example:

+--------------------+
| Page Header        |
+--------------------+
| Cell Pointers      |
+--------------------+
| Free Space         |
+--------------------+
| B-tree Cells       |
+--------------------+

Because SQLite tables are implemented as B-trees, the page structure is optimized for tree traversal and lookup efficiency rather than heap storage.


### 3.3 Indexing

Both use B+-tree variants for default indexes.
- **PostgreSQL** index pages reference heap tuples through a `(block, offset)`
  TID. An index scan fetches the TID, then visits the heap page to retrieve
  (and MVCC-check) the actual row — meaning even a covering-looking index scan
  can incur a heap visit unless the **visibility map** confirms the page is
  all-visible (this is the mechanism behind Index-Only Scans).
- **SQLite** indexes store the rowid as the "payload," and looking up a row by
  index means: descend the index B-tree to find the rowid, then descend the
  table B-tree using that rowid as the key. No separate heap fetch step exists
  because the table itself is the second tree.

### 3.4 Transaction Management and Concurrency Control

**PostgreSQL — true MVCC, many writers:**
Every tuple carries hidden `xmin`/`xmax` columns. An `UPDATE` is implemented as
an `INSERT` of a new tuple version plus marking the old version's `xmax`,
*never* an in-place overwrite. Old versions are not reclaimed immediately —
`VACUUM` does that later, once no active transaction can still need to see
them. This is what allows Postgres to support many simultaneous writers and
readers without blocking each other: readers always see a transactionally
consistent snapshot, writers create new versions rather than fighting over the
same bytes.

**SQLite — single active writer:**
SQLite uses either a rollback journal or, since 3.7, **WAL mode**. In WAL mode,
readers see a consistent snapshot taken at the start of their transaction (a
form of MVCC at the database-file level via the WAL), but **only one writer can
be active across the whole database file at a time** — there is no concept of
row-level locking or multiple uncommitted writer transactions interleaving.
This is a deliberate, defensible simplification: an embedded database almost
always has one logical owner process (or a small number) and doesn't need to
shard concurrency at the row level. Building full MVCC with vacuum,
transaction IDs, and visibility maps would add exactly the kind of operational
weight (background workers, bloat, tuning) that contradicts SQLite's
zero-administration design goal.

### 3.5 Durability

Both use **write-ahead logging**: write the log record describing the change,
fsync it, *then* the change is considered durable, and the actual data pages
can be flushed lazily afterward.
- PostgreSQL's WAL is a continuous, append-only, multi-segment log shared by
  the whole cluster, with checkpoints, archiving, and streaming replication
  built directly on top of it.
- SQLite's WAL (in WAL mode) is a single per-database file that is periodically
  "checkpointed" back into the main database file; in the older rollback-journal
  mode, durability instead comes from copying *original* pages into a journal
  before overwriting them, so a crash can be undone by replaying the journal
  backwards.

## 4. Design Trade-Offs

| Dimension | PostgreSQL | SQLite |
|---|---|---|
| Deployment | Requires a running server, network stack, OS user/process management | Zero administration — it's just a file |
| Concurrent writers | High — row-level MVCC | Low — one writer at a time per database file |
| Multi-host / remote access | Native | Not supported — file must be locally accessible |
| Footprint | Tens of MB+ resident, background processes | A few hundred KB, no background processes |
| Feature surface | Extensions, stored procedures, full-text search, JSONB, replication, foreign data wrappers | Deliberately minimal — small, predictable footprint |
| Failure isolation | Backend crash doesn't take down the server | Process crash takes the whole "database" connection down (since it's in-process) |
| Best fit | Multi-user backend services, OLTP at scale, analytics with many concurrent clients | Mobile apps, embedded devices, local caches, browsers, single-user desktop tools, testing |

**Why SQLite works well for mobile apps:** the device has one app instance
reading/writing its own data — there is no need for a network protocol or
multi-writer concurrency, and "no server to install" matters enormously when
the deployment target is millions of phones you don't administer.

**Why PostgreSQL is preferred for large multi-user systems:** many independent
clients need to commit overlapping transactions safely, query the same large
shared dataset, and the operational cost of running a server is justified by
the workload's scale and concurrency needs.

## Real-World Use Cases

### PostgreSQL

PostgreSQL is commonly used in:

- Banking systems
- E-commerce platforms
- ERP systems
- SaaS applications
- Analytics and reporting platforms

These environments require high concurrency, strong transactional guarantees, and support for many simultaneous users.

### SQLite

SQLite is commonly used in:

- Android applications
- iOS applications
- Browser storage engines
- Desktop applications
- IoT and embedded devices

These environments benefit from SQLite's lightweight deployment model and minimal resource requirements.

### Why SQLite Works Well for Mobile Applications

Mobile devices typically have a single application accessing the database locally. SQLite avoids the overhead of running a separate database server while still providing transactional guarantees and SQL support.

### Why PostgreSQL is Preferred for Large Multi-User Systems

Large applications often have hundreds or thousands of users accessing shared data simultaneously. PostgreSQL's MVCC implementation, client-server architecture, and sophisticated locking mechanisms allow it to handle these workloads efficiently.


## 5. Experiments / Observations

A simple way to *see* the single-writer constraint in SQLite versus
PostgreSQL's MVCC concurrency:

```sql
-- SQLite, two concurrent processes:
-- Process A:
BEGIN IMMEDIATE; UPDATE accounts SET balance = balance - 100 WHERE id = 1;
-- Process B (while A's transaction is still open):
BEGIN IMMEDIATE; UPDATE accounts SET balance = balance + 100 WHERE id = 2;
-- Result: Process B blocks (or returns SQLITE_BUSY) until A commits/rolls back,
-- even though A and B touch completely different rows.
```

```sql
-- PostgreSQL, the same scenario:
-- Session A:
BEGIN; UPDATE accounts SET balance = balance - 100 WHERE id = 1;
-- Session B (concurrently):
BEGIN; UPDATE accounts SET balance = balance + 100 WHERE id = 2;
-- Result: both proceed and commit independently — no blocking,
-- because MVCC + row-level locking only serializes conflicting rows.
```

This single experiment makes the architectural divergence concrete: SQLite's
writer lock is **database-file-granular**, Postgres's is **row-granular**.

## 6. Key Learnings

- Architecture follows deployment target, not the other way around. Every
  difference between Postgres and SQLite — process model, MVCC depth,
  durability mechanism — is explainable by asking "who is this database
  serving, and how many of them write concurrently?"
- "Simpler" is not "worse." SQLite's single-writer model is a deliberate,
  appropriate trade against the complexity of true multi-version concurrency,
  because its target workload rarely needs that complexity.
- Index structures must be understood in the context of *what they point to*:
  a Postgres index points to a heap tuple via TID; a SQLite index points to a
  table B-tree via rowid. The same B+-tree algorithm produces different lookup
  costs depending on what the leaf actually contains.
- The presence or absence of a background process model (autovacuum,
  checkpointer, WAL writer in Postgres vs. none in SQLite) is itself a
  durability/complexity trade-off, not an implementation detail.

## Architectural Lessons

The comparison between PostgreSQL and SQLite demonstrates that database architecture is driven primarily by workload requirements rather than by a universal notion of correctness.

PostgreSQL accepts additional operational complexity in exchange for scalability, concurrency, and advanced functionality. Features such as MVCC, shared memory management, WAL, and background maintenance processes allow it to support demanding enterprise workloads.

SQLite makes the opposite trade-off. By embedding the database directly into the application and simplifying concurrency control, it achieves exceptional portability and ease of deployment.

The most important lesson is that every database system represents a collection of engineering trade-offs. Understanding those trade-offs is often more valuable than understanding any individual implementation detail.


## References
- PostgreSQL source: `src/backend/access/heap/`, `src/backend/storage/`
- SQLite source and "Architecture of SQLite" design documents (sqlite.org/arch.html)
- "The Internals of PostgreSQL" — Hironobu Suzuki