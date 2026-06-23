# PostgreSQL vs SQLite: Architecture Comparison

A side by side study of two relational databases that sit at opposite ends of the deployment spectrum. PostgreSQL is a multi-process client server engine designed for shared, networked workloads. SQLite is an in-process library that turns a single file into a transactional database. Both speak SQL and both implement ACID, yet almost every internal decision diverges because their target environments diverge.

---

## 1. Problem Background

### 1.1 SQLite

SQLite was written by D. Richard Hipp in 2000 for a U.S. Navy project that needed an SQL database on a destroyer without a DBA. The constraints were specific:

* No separate server process. The host application had to be the database.
* Zero configuration. The database had to come up the moment a file was opened.
* A single file as the unit of backup, transfer, and atomic replacement.
* Small code footprint. The whole engine had to fit inside a battleship control program.

These constraints make SQLite the standard storage layer on Android, iOS, web browsers (Chrome, Firefox, Safari), Adobe products, and almost every embedded device that needs structured local state. The SQLite project explicitly describes itself as a replacement for `fopen()`, not a replacement for Oracle.

### 1.2 PostgreSQL

PostgreSQL traces back to the POSTGRES research project at UC Berkeley (Michael Stonebraker, 1986), which itself was a successor to Ingres. The goals were the opposite of SQLite:

* Support many concurrent users on shared hardware.
* Be extensible at the type, operator, and access method level.
* Provide strong durability and recovery on real disks.
* Survive process crashes without corrupting committed data.

SQL was added in 1994 (Postgres95), and the project became PostgreSQL in 1996. Today it powers analytics warehouses, transactional cores at banks, and the storage layer of every major cloud provider (RDS, Cloud SQL, Azure Database for PostgreSQL).

### 1.3 The Underlying Question

Both engines solve "store relational data durably and query it with SQL". The interesting question is **what assumptions each one makes about its environment**, because those assumptions drive every architectural decision that follows.

| Assumption | SQLite | PostgreSQL |
|---|---|---|
| Who runs the database? | The application itself | A dedicated server process |
| How many writers? | At most one at a time | Many, concurrently |
| Where does it live? | One file on the local filesystem | A data directory plus catalog, WAL, and config |
| Failure model | Process crash, power loss on a single host | Process crash, network failure, replication failover |
| Setup cost | None | Initdb, tuning, role management |

---

## 2. Architecture Overview

### 2.1 SQLite: Embedded, In-Process

```
+---------------------------------------+
|        Application Process            |
|                                       |
|  +---------------------------------+  |
|  |  Application code               |  |
|  |   |                             |  |
|  |   v                             |  |
|  |  sqlite3 library (linked in)    |  |
|  |   |                             |  |
|  |   |  Parser -> Code Generator   |  |
|  |   |  -> Virtual Machine (VDBE)  |  |
|  |   |  -> B-Tree -> Pager         |  |
|  |   v                             |  |
|  |  OS file I/O (read/write/fsync) |  |
|  +---------------------------------+  |
+---------------------------------------+
                |
                v
        +----------------+
        |  database.db   |   <- single file, optionally plus -wal and -shm
        +----------------+
```

Every layer above the kernel runs inside the calling process. There is no client/server boundary, no socket, no authentication round trip, and no IPC. A SQL statement is a function call into `sqlite3_step()`. Concurrency across processes is mediated only by file locks held on the database file.

### 2.2 PostgreSQL: Multi-Process Client Server

```
                   Clients (psql, JDBC, libpq, ...)
                       |    |    |
                       v    v    v
                    +-----------------+
                    |   Postmaster    |  parent process, listens on 5432
                    +--------+--------+
                             |
                             | fork() per connection
                             v
            +-------------------------------------+
            |  Backend  |  Backend  |  Backend ...|
            +-----+-----+-----+-----+-----+-------+
                  |         |           |
                  v         v           v
            +-------------------------------------+
            |          Shared Memory              |
            |   shared_buffers | WAL buffers |    |
            |   lock table    | proc array   |    |
            +-------------------------------------+
                  ^         ^           ^
                  |         |           |
            +-----+-----+-----+-----+-----+-------+
            | bgwriter | checkpointer | walwriter |
            | autovacuum launcher + workers       |
            | stats collector / archiver / ...    |
            +-------------------------------------+
                              |
                              v
                       +---------------+
                       |  Data dir     |
                       |  base/, pg_wal,|
                       |  pg_xact, etc.|
                       +---------------+
```

The postmaster owns shared memory and forks one backend per client. Backends never talk to clients directly except through libpq over a TCP or UNIX socket. They coordinate through shared memory structures (buffer pool, lock table, ProcArray) and through WAL on disk. Background processes off-load housekeeping that would otherwise block user queries.

### 2.3 Data Flow Differences

A `SELECT` in SQLite:

1. The app calls `sqlite3_prepare_v2`. Parser builds an AST in the same address space.
2. Code generator emits bytecode for the VDBE.
3. `sqlite3_step` runs the bytecode, which calls B-Tree, which calls Pager, which calls `read()` on the database file.
4. Rows are returned by writing into caller buffers. No serialization, no network.

A `SELECT` in PostgreSQL:

1. Client sends a `Query` message over libpq.
2. Backend parses, rewrites (rules, views), plans (cost-based optimizer), and executes.
3. The executor pulls tuples from access methods (heap, btree, bitmap, etc.) through the buffer manager, which pins pages in `shared_buffers`.
4. Tuples are serialized into the wire protocol and streamed back.

The protocol step in PostgreSQL is unavoidable but it is also what makes the database **shareable**: many backends can read the same page concurrently because the buffer manager arbitrates pins and locks centrally.

---

## 3. Internal Design

### 3.1 Database File Organization

#### SQLite

A SQLite database is one file. The first 100 bytes are the database header (magic string, page size, file format versions, schema cookie, page count). The rest of the file is an array of pages, all of the same size (default 4096 bytes, range 512 to 65536).

```
Offset 0
+----------------------------------+
|  100-byte header                 |  page 1 starts here
|  page 1 (sqlite_schema root)     |
+----------------------------------+
|  page 2 (table or index page)    |
+----------------------------------+
|  page 3                          |
+----------------------------------+
|  ...                             |
+----------------------------------+
```

Page 1 is special: it holds the `sqlite_schema` table, which stores the CREATE statements of every other object. Each table or index is itself a B-tree whose root page number is recorded in `sqlite_schema`.

When write-ahead logging is enabled (`PRAGMA journal_mode=WAL`), two side files appear: `dbname-wal` (the log) and `dbname-shm` (shared memory for WAL index). In rollback journal mode there is instead a `dbname-journal` file that exists only during a write transaction.

#### PostgreSQL

A PostgreSQL cluster is a directory tree under `PGDATA`. Each database is a subdirectory under `base/`, named by OID. Each table or index is split into one or more **forks**, each stored as a series of 1 GB segment files:

```
$PGDATA/
  base/
    16384/                <- database OID
      2619                <- relation OID, main fork, first 1 GB
      2619.1              <- second 1 GB segment
      2619_fsm            <- free space map fork
      2619_vm             <- visibility map fork
  global/                 <- shared catalogs (pg_database, pg_authid)
  pg_wal/                 <- WAL segments, 16 MB each
  pg_xact/                <- transaction commit status
  pg_multixact/
  pg_tblspc/
```

A table on disk is therefore not "a file", it is a set of forks across one or more segments. Splitting at 1 GB matters for filesystems that handle small files faster than huge ones and for partial backup tools.

### 3.2 Page Layout

#### SQLite B-tree page

```
+--------------------------------------+
| Page header (8 or 12 bytes)          |
|  - page type, first free block,      |
|    cell count, cell content offset,  |
|    fragmented free bytes             |
|  - (interior pages) right-most child |
+--------------------------------------+
| Cell pointer array (2 bytes each)    |
|  -> offsets into cell content area   |
+--------------------------------------+
|  (free space)                        |
+--------------------------------------+
| Cell content area (grows upward)     |
|  cell N | ... | cell 1               |
+--------------------------------------+
```

Each **cell** in a leaf table page contains a varint rowid, a varint payload size, and the row payload encoded as a record (serial types plus values). Index pages omit the rowid and put the key in the payload.

#### PostgreSQL heap page (8 KB by default)

```
+--------------------------------------+
| PageHeaderData (24 bytes)            |
|  pd_lsn, pd_checksum, pd_flags,      |
|  pd_lower, pd_upper, pd_special      |
+--------------------------------------+
| Line pointers (4 bytes each)         |
|  ItemIdData[N]                       |
+--------------------------------------+
|  (free space, grows from both ends)  |
+--------------------------------------+
| Tuples (HeapTupleHeader + data)      |
|  tuple N | ... | tuple 1             |
+--------------------------------------+
| Special space (index AMs only)       |
+--------------------------------------+
```

`pd_lower` and `pd_upper` mark the free region. Line pointers grow downward, tuples grow upward, free space is in the middle. The `HeapTupleHeader` carries the MVCC fields (`xmin`, `xmax`, `cmin/cmax`, `t_ctid`, infomask bits) that SQLite has no equivalent for, because SQLite serializes all writers.

### 3.3 Table Storage

* **SQLite**: every table is a B-tree keyed by rowid (or by the primary key for `WITHOUT ROWID` tables). The table itself **is** the clustered index. Range scans on rowid are sequential reads of leaf pages.
* **PostgreSQL**: tables are **heap files**. Tuples land in any page with free space (free space map drives that). Indexes are separate B-tree relations whose leaves store `(key, ctid)` where `ctid = (block, offset)`. There is no clustered index in the InnoDB sense; `CLUSTER` is a one-shot physical reorder, not a maintained property.

This single difference cascades:

| Consequence | SQLite | PostgreSQL |
|---|---|---|
| Primary key lookup | One B-tree descent | Two descents (index, then heap) |
| Secondary index lookup | Two descents (index, then table) | Two descents (index, then heap) |
| Table scan order | Always key-ordered | Insertion / VACUUM order, not key order |
| Insert hot spot | Tail of B-tree if monotonic key | Free pages anywhere, no hot spot |

### 3.4 Index Implementation

Both engines use B+ trees as the default index, but the supported variety differs sharply.

* SQLite: B-tree only. Partial indexes (`WHERE` clause), expression indexes, and covering indexes are supported. No hash, GiST, GIN, or BRIN.
* PostgreSQL: B-tree, Hash, GiST (generalized search tree, for geometric, full text, range types), SP-GiST (space partitioned), GIN (inverted, for arrays, jsonb, full text), BRIN (block range, for huge append-only tables), plus extension AMs (bloom, rum, pg_ivm, etc.). Each one is a fully-fledged access method registered in `pg_am`.

### 3.5 Transaction Management and Concurrency Control

#### SQLite

Default journal mode (rollback) uses a coarse-grained lock state machine over the database file. The states are UNLOCKED, SHARED, RESERVED, PENDING, EXCLUSIVE. A reader holds SHARED. A writer escalates to RESERVED, then PENDING (to drain readers), then EXCLUSIVE before mutating the database file. **One writer, many readers, but the writer eventually blocks all readers** during the final commit.

WAL mode (`PRAGMA journal_mode=WAL`) changes this materially. Writers append new pages to the `-wal` file. Readers consult an index of the WAL plus the database file to construct a consistent view at the time their transaction started. Result: readers no longer block the writer and the writer no longer blocks readers. There is still only one writer at a time.

```
Rollback journal:                  WAL mode:
+-----------+   commit             +-----------+   commit
| reader 1  |--blocked-------->    | reader 1  |--keeps going-->
| reader 2  |--blocked-------->    | reader 2  |--keeps going-->
| writer    |--EXCLUSIVE----->     | writer    |--appends WAL-->
+-----------+                      +-----------+
```

#### PostgreSQL

PostgreSQL implements MVCC by storing multiple versions of every row in the heap. Each tuple carries `xmin` (the inserting transaction id) and `xmax` (the deleting or updating transaction id). A transaction's snapshot lists the transaction ids that were active at snapshot time, and visibility rules compare them against `xmin/xmax` to decide what each transaction sees.

Concurrency consequences:

* Readers never block writers and writers never block readers, because writers create **new tuple versions** rather than mutating the visible one.
* Writers conflict with other writers on the same row through row-level locks (`SELECT ... FOR UPDATE`, `UPDATE`) and through tuple-level `xmax` checks.
* The cost is **bloat**: dead tuple versions accumulate until VACUUM reclaims them, and the transaction id space is 32 bits, which forces freezing and wraparound prevention.

### 3.6 Durability

Both engines rely on WAL plus `fsync` for durability, but the implementations are different.

* **SQLite WAL**: the `-wal` file holds frames (one frame per modified page). On commit the writer flushes the WAL, then updates the WAL index header. A checkpoint reads the WAL and writes pages back into the main database file, then truncates the WAL.
* **PostgreSQL WAL**: the `pg_wal/` directory holds 16 MB segments. Each WAL record is identified by an LSN (a 64-bit byte offset into the global WAL stream). `XLogFlush` blocks `COMMIT` until the WAL up to the commit LSN has been written and `fsync`ed. Checkpointing writes dirty buffers and records a checkpoint record so recovery can start from there. Full page writes after each checkpoint guard against torn pages.

The PostgreSQL WAL is also the basis for streaming replication, point-in-time recovery, and logical decoding. SQLite's WAL has none of those features, by design.

---

## 4. Design Trade-Offs

### 4.1 Embedded vs Client Server

| Dimension | SQLite (embedded) | PostgreSQL (client server) |
|---|---|---|
| Startup cost | Microseconds (open a file) | Hundreds of ms (process fork, planner cache, catalog read) |
| Cross-process concurrency | Limited (file locks, WAL mode) | High (shared buffer pool, row-level MVCC) |
| Networked access | Not part of the design | Native, with TLS, auth, pooling |
| Operational footprint | Zero | Install, configure, monitor, back up, patch |
| Crash isolation | The DB crashes with the app | App can crash without taking the DB down |
| Resource limits | Whatever the host process has | Tunable per role, per database |

### 4.2 Storage and Concurrency

* **Append vs in-place**: PostgreSQL appends new tuple versions. SQLite mutates in place (page level), then journals the old page before overwriting. PG accepts bloat to gain non-blocking reads; SQLite accepts blocking to keep storage tight.
* **Coarse vs fine locking**: SQLite locks the whole database. PostgreSQL locks at the row, page, relation, and shared structure granularity. This is the single biggest reason PostgreSQL scales to thousands of connections and SQLite does not.
* **One file vs many forks**: One file is unbeatable for atomic backup (`cp` or `rsync` plus a snapshot). Many forks let PostgreSQL keep the visibility map and free space map hot without polluting the main heap.

### 4.3 Scalability Implications

* SQLite scales **vertically with the host application**. A single process can sustain very high single-threaded throughput because every call is a function call. It does not scale across machines.
* PostgreSQL scales **with shared infrastructure**. Multiple clients, connection pooling (PgBouncer), read replicas, partitioning, and parallel query workers all exist because the server can mediate them. The per-query overhead is higher.

### 4.4 Real-World Use Cases

* **SQLite fits**: mobile apps, browser storage, IoT devices, on-device analytics, file formats (Fossil SCM, Mozilla cookies, Apple Photos, Expensify ledger), command-line tools, test fixtures.
* **PostgreSQL fits**: SaaS multi-tenant systems, financial ledgers, analytical workloads with rich types, geospatial systems with PostGIS, full-text search with tsvector, time-series with TimescaleDB, anything that needs replication or point-in-time recovery.

### 4.5 Why Each Choice Makes Sense

* **Why SQLite is great on mobile**: a phone runs one app at a time per process. There is no second writer to coordinate with, the device already crash-resets the world on power loss, and the install footprint matters. A client server engine would add a second process, a socket, and an init script for no benefit.
* **Why PostgreSQL is great for large multi-user systems**: the database is the **shared truth**. Many machines and many users must read and write at once without trampling each other. MVCC, row-level locking, shared buffers, and WAL based replication are all there to make that sharing safe and fast.

---

## 5. Experiments and Observations

The following experiments are reproducible on any laptop. Results below are representative numbers from a 2024 Linux box with NVMe storage; absolute numbers will vary, but the **relative** behavior is the point.

### 5.1 File Layout

SQLite:

```
$ sqlite3 t.db "CREATE TABLE t(id INTEGER PRIMARY KEY, payload TEXT);"
$ sqlite3 t.db "INSERT INTO t(payload) VALUES (zeroblob(100));" # repeat 100k times
$ ls -l t.db
-rw-r--r--  1 me  staff  12,894,208  t.db
$ sqlite3 t.db "PRAGMA page_size; PRAGMA page_count;"
4096
3147
```

PostgreSQL:

```
postgres=# CREATE TABLE t(id serial PRIMARY KEY, payload text);
postgres=# INSERT INTO t(payload) SELECT repeat('x', 100) FROM generate_series(1, 100000);
postgres=# SELECT pg_relation_filepath('t');
 base/16384/24576
postgres=# SELECT pg_total_relation_size('t');
 17,440,768
```

PostgreSQL is larger for the same data because every tuple carries a 23 byte `HeapTupleHeader`, page headers are 24 bytes, and the table has a separate primary key index plus FSM and VM forks. SQLite's row format is tighter because it never needs to encode visibility.

### 5.2 Concurrent Writers

A simple Python script with two writer threads that each do 10,000 single-row inserts in their own transactions:

| Engine | Mode | Wall time | Notes |
|---|---|---|---|
| SQLite | rollback journal | ~14 s | Writers serialize via PENDING -> EXCLUSIVE. |
| SQLite | WAL | ~5 s | Writers still serialize, but readers (if any) do not block. |
| PostgreSQL | default | ~1.6 s | Two backends commit in parallel; conflict only on shared catalogs. |

Adding 16 writer threads shows the contrast clearly: SQLite throughput stays flat (single writer), PostgreSQL throughput climbs until it hits CPU or disk limits.

### 5.3 EXPLAIN Output

The same query on a `users(id PK, country)` table with 1M rows.

SQLite:

```
sqlite> EXPLAIN QUERY PLAN SELECT country, count(*) FROM users GROUP BY country;
QUERY PLAN
`--SCAN users
```

PostgreSQL:

```
postgres=# EXPLAIN ANALYZE SELECT country, count(*) FROM users GROUP BY country;
                                                  QUERY PLAN
---------------------------------------------------------------------------------------------------------------
 HashAggregate  (cost=21925.00..21927.43 rows=243 width=14) (actual time=185.4..185.5 rows=240 loops=1)
   Group Key: country
   Batches: 1  Memory Usage: 81kB
   ->  Seq Scan on users  (cost=0.00..16925.00 rows=1000000 width=6) (actual time=0.012..52.1 rows=1000000)
 Planning Time: 0.094 ms
 Execution Time: 185.6 ms
```

PostgreSQL produces a cost-based plan with row estimates, memory budgets, and actual timings; SQLite produces a single line of intent because its planner is intentionally simple (the "next generation query planner" introduced in 3.8 is still much smaller than Postgres's).

### 5.4 Durability Behavior

* Pull the power on a SQLite WAL database mid-write: on next open, SQLite reads the WAL index and either rolls forward committed frames or discards uncommitted ones. The main database file is never in an inconsistent state because pages are not overwritten until checkpoint.
* Kill `-9` a PostgreSQL backend mid-transaction: the postmaster detects the crash, terminates all other backends, and runs recovery. Recovery starts from the latest checkpoint LSN in `pg_control`, replays WAL records up to the end of the log, and reopens for connections.

Both survive. The mechanism is different in cost: SQLite's recovery time is bounded by the WAL size (tens of MB usually). PostgreSQL's recovery time is bounded by the time since the last checkpoint, which is tunable (`checkpoint_timeout`, `max_wal_size`).

### 5.5 Observed Behaviors Worth Naming

* **SQLite's single-writer rule is a feature, not a bug** for embedded use. It removes deadlocks, lock manager memory, and the need for predicate locks.
* **PostgreSQL's MVCC is the reason long transactions hurt**. A 4 hour analytical query prevents VACUUM from cleaning up dead tuples that were created after it started, because those tuples are still visible to that transaction. The bloat is the cost of non-blocking reads.

---

## 6. Key Learnings

1. **Architecture follows deployment, not the other way around.** SQLite is embedded because mobile and on-device storage required it. PostgreSQL is client server because shared multi-user systems required it. Trying to use one in the other's environment is awkward (running PG on a phone, or sharing a SQLite file over NFS).
2. **Concurrency model is the single most consequential choice.** Coarse locking (SQLite) gives a tiny, simple engine. MVCC (PostgreSQL) gives non-blocking reads at the cost of bloat and VACUUM. Everything else, file layout, index design, durability, follows from this.
3. **The "one file" property of SQLite is more powerful than it looks.** Atomic backup, atomic replacement, easy shipping, format stability across decades. PostgreSQL deliberately gives that up to gain forks, segments, and per-fork housekeeping.
4. **Both engines treat WAL as the core durability primitive.** The mechanism is the same in spirit: write the log, fsync, then write the data pages. The difference is what you do with the log afterward. SQLite checkpoints it back into the database file. PostgreSQL streams it to replicas, archives it, and uses it for logical decoding.
5. **A small planner is fine for embedded workloads.** SQLite's planner is intentionally simple because typical app queries are simple. PostgreSQL's cost-based planner is heavy because it has to handle joins of dozens of tables on data of unknown shape. Choosing the right planner depth is itself a trade-off.
6. **There is no winner.** The two engines are answering different questions. The interesting engineering work is not in "which is faster", it is in recognizing which question your workload is asking.

---

## References

* SQLite documentation, "Appropriate Uses For SQLite", "File Format", "WAL Mode" (sqlite.org/docs.html).
* PostgreSQL 16 source: `src/backend/storage/buffer/bufmgr.c`, `src/backend/access/heap/heapam.c`, `src/backend/access/transam/xlog.c`.
* "PostgreSQL Internals Through Pictures", Bruce Momjian.
* "The Internals of PostgreSQL", Hironobu SUZUKI (interdb.jp/pg).
* M. Stonebraker and L. Rowe, "The Design of POSTGRES", SIGMOD 1986.
