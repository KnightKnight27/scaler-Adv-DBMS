# PostgreSQL vs SQLite: Architecture Comparison

**Course:** Advanced Database Management Systems
**Student:** Indrajeet Yadav | 23BCS10199

---

## 1. Problem Background

Both SQLite and PostgreSQL solve the same core problem — store, query, and retrieve structured data — but were designed for completely different deployment contexts.

**SQLite** (2000, D. Richard Hipp): No server process, no installation, no admin. A library linked into the application. The constraint — *zero-administration, zero-server* — is still SQLite's entire design philosophy today.

**PostgreSQL** (originally POSTGRES, 1986, UC Berkeley): Built to serve many simultaneous users with strict ACID guarantees, which requires a server process to coordinate access.

**Key insight:** SQLite optimises for simplicity and portability; PostgreSQL optimises for concurrency and correctness at scale.

---

## 2. Architecture Overview

### SQLite — Embedded, Serverless

```
Application Process
+---------------------------------------------+
|  Application Code                           |
|        |                                    |
|  +-----v----------------------------------+ |
|  |         SQLite Library                 | |
|  |  +----------+  +---------------+       | |
|  |  |  Parser  |  |  Code Gen     |       | |
|  |  +----+-----+  +------+--------+       | |
|  |       +----------+----+                | |
|  |                +-v----------+          | |
|  |                |   VM/VDBE  |          | |
|  |                +-+----------+          | |
|  |                +-v----------+          | |
|  |                | B-Tree Mgr |          | |
|  |                +-+----------+          | |
|  |                +-v----------+          | |
|  |                |  Pager     |  (page   | |
|  |                +-+----------+   cache) | |
|  +--------------------+------------------+ |
|                       | Single .db file     |
+---------------------------------------------+
```

Everything runs inside the application process. No daemon, no network socket, no authentication handshake.

### PostgreSQL — Client/Server

```
Client Application
      |  TCP / Unix socket
      v
+---------------------+
|  Postmaster Process |  (listener / supervisor)
+------+--------------+
       | fork() per connection
       v
+-----------------------------------------------------+
|  Backend Process (one per connection)               |
|  Parser -> Rewriter -> Planner -> Executor          |
+----------------+------------------------------------+
                 | shared memory
                 v
+-----------------------------------------------------+
|  Shared Buffers (default 128 MB)                    |
|  WAL buffers  |  Lock table  |  CLOG                |
+-------------------+-----------------------------------------+
                    | Heap files + Index files
```

PostgreSQL uses a **process-per-connection** model. Each client gets its own backend process; they communicate only through shared memory.

---

## 3. Internal Design

### 3.1 Storage: File Layout

| | SQLite | PostgreSQL |
|---|---|---|
| Unit of storage | Single `.db` file | Directory of files (`$PGDATA`) |
| Page size | 4096 bytes (default) | 8192 bytes (default) |
| Max DB size | 281 TB | Unlimited (filesystem-bound) |
| Table storage | B-tree of records (rowid) | Heap files (unordered) |

**SQLite packs everything into one file.** Each table is a B-tree keyed on rowid — effectively a clustered index for every table.

**PostgreSQL uses heap files.** Tables are unordered heaps; indexes are separate files. This decouples physical row location from the primary key.

### 3.2 Page Layout

**SQLite page (4 KB):**
```
+----------------------------+
| Page header (8-12 bytes)   |
|  - page type               |
|  - number of cells         |
|  - cell content area start |
+----------------------------+
| Cell pointer array (down)  |
+----------------------------+
|       free space           |
+----------------------------+
| Cell content area (up)     |
+----------------------------+
```

**PostgreSQL heap page (8 KB):**
```
+----------------------------+
| PageHeaderData (24 bytes)  |
|  - LSN (for WAL)           |
|  - pd_lower / pd_upper     |
+----------------------------+
| ItemId array (4 bytes each)|  <- line pointer array
+----------------------------+
|       free space           |
+----------------------------+
| Tuple data                 |
|  - t_xmin, t_xmax (MVCC)  |
|  - t_ctid                  |
|  - column data             |
+----------------------------+
```

Every PostgreSQL tuple carries `xmin` and `xmax` — the foundation of MVCC.

### 3.3 Concurrency Control

**SQLite — WAL mode with file locks:**
- File-level locks (shared / reserved / exclusive)
- In WAL mode: one writer + multiple concurrent readers
- Writer writes to a WAL file; readers read the original DB
- Only one writer at a time, ever

**PostgreSQL — MVCC:**
- Each row version carries `xmin` (created by) and `xmax` (deleted by)
- UPDATE does NOT overwrite — marks old tuple's `xmax`, writes new tuple
- **Readers never block writers and writers never block readers**
- Trade-off: dead tuples accumulate, must be cleaned by `VACUUM`

### 3.4 Transaction Management & Durability

**SQLite WAL:**
- Changes accumulate in WAL file; periodically checkpointed to the main DB
- Readers read the main file + WAL if needed
- Provides atomicity: incomplete WAL entries are ignored on crash

**PostgreSQL WAL:**
- Every change written to `pg_wal/` before touching data files
- LSN in each page header enables crash recovery idempotency
- On recovery: replay WAL records from last checkpoint forward
- Also enables streaming replication — standby servers replay the same WAL

---

## 4. Design Trade-Offs

### Why PostgreSQL uses a client-server architecture

Multiple processes accessing the same database need a central coordinator to:
- Assign transaction IDs in order
- Maintain a shared buffer pool (per-client caches would diverge)
- Manage locks
- Run `VACUUM` and `autovacuum` in the background

SQLite bypasses this by serializing writes via OS file locks. Perfect for a single application; breaks down under concurrent write-heavy workloads.

### The MVCC trade-off

PostgreSQL's MVCC is elegant: readers and writers never conflict. But it creates **table bloat**: every UPDATE creates a new row version; old versions are not immediately reclaimed. `VACUUM` must periodically scan tables. If it falls behind (long-running transaction holding back the oldest xmin), the table grows indefinitely. SQLite has no equivalent concept.

### Page size choice

| | SQLite (4 KB) | PostgreSQL (8 KB) |
|---|---|---|
| Match with OS pages | Good (4 KB = OS page) | Sequential scan efficiency |
| Row size limit | ~1/2 page before overflow | ~2 KB before TOAST |
| Random I/O cost | Lower | Higher |
| Sequential scan | More I/Os for large tables | Fewer I/Os, better prefetch |

### Summary

| Property | SQLite | PostgreSQL |
|---|---|---|
| Concurrent writers | 1 (serialized) | Many (MVCC) |
| Reader/writer conflict | Blocked in exclusive mode | Never block each other |
| Setup required | None (library) | Server + config |
| Dead tuple cleanup | Not needed | VACUUM required |
| Replication | Not built-in | Streaming replication |
| Max connections | Effectively 1 writer | Hundreds (with pgbouncer: thousands) |

---

## 5. Experiments / Observations

### 5.1 Page Size Verification

**SQLite:**
```sql
PRAGMA page_size;   -- 4096
PRAGMA page_count;  -- 90
-- File size: ~360 KB
```

**PostgreSQL:**
```sql
SHOW block_size;    -- 8192
SELECT pg_size_pretty(pg_relation_size('users'));  -- 672 kB
SELECT pg_relation_size('users') /
       current_setting('block_size')::int AS pages;  -- 84 pages
```

PostgreSQL uses fewer pages (84 vs 90) but larger total footprint (672 KB vs 360 KB) because every tuple carries 23 bytes of MVCC header.

### 5.2 Query Performance: COUNT(*) on full table

```
SQLite  (without mmap): 0.000118s
SQLite  (with mmap):    0.000144s   <- mmap setup overhead > benefit for 360 KB
PostgreSQL:             5.156ms
```

SQLite is ~40x faster not because of superior execution but because there is **no network round-trip** — the query is a function call in the same process.

### 5.3 MVCC visibility: observing xmin/xmax

```sql
-- After inserting a row and then updating it:
SELECT xmin, xmax, id, name FROM users WHERE id = 1;
--  xmin | xmax | id | name
-- ------+------+----+-------
--   102 |  103 |  1 | Alice   <- old version, deleted by xid 103
--   103 |    0 |  1 | Alice2  <- new version, still live
```

---

## 6. Key Learnings

1. **Architecture follows deployment constraints.** SQLite's single-file, no-server design runs in places PostgreSQL can't: mobile apps, browsers, embedded systems. The right question is "which fits the deployment model," not "which is better."

2. **MVCC trades storage for concurrency.** Readers and writers never conflict — essential for high-concurrency OLTP. But the dead-tuple cost requires careful `autovacuum` tuning in production.

3. **Page size is a fundamental parameter.** SQLite's 4 KB matches the OS virtual memory page; PostgreSQL's 8 KB reduces I/O count on sequential scans. Both choices reflect different workload priorities.

4. **The IPC overhead of client-server is real but justified.** The 40x latency difference on COUNT(*) is entirely IPC/network overhead, not query execution quality. For thousands of concurrent clients, that overhead is irrelevant compared to the concurrency PostgreSQL enables.

5. **WAL solves different problems in each system.** In SQLite, WAL primarily enables concurrent reads alongside one writer. In PostgreSQL, WAL is the foundation of crash recovery, point-in-time recovery, and streaming replication.
