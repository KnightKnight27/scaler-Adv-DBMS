# PostgreSQL vs SQLite: Architecture Comparison

**Course:** Advanced Database Management Systems  
**Student:** Vivek Anand Singh | 23BCS10172

---

## 1. Problem Background

### Why do two such different databases exist?

Both SQLite and PostgreSQL solve the same core problem — store, query, and retrieve structured data — but they were designed for completely different deployment contexts, and those contexts drove every architectural decision they made.

**SQLite** (2000) was created by D. Richard Hipp for use in a US Navy guided missile destroyer. The constraint was simple: no server process, no installation, no admin. The database had to be a library you linked into your application, and it had to work on embedded hardware. That constraint — *zero-administration, zero-server* — is still SQLite's entire design philosophy today.

**PostgreSQL** (originally POSTGRES, 1986, UC Berkeley) was designed as a research database to explore complex queries, user-defined types, and multi-user concurrency. It was built from the start to serve many simultaneous users with strict ACID guarantees, which requires a server process to coordinate access.

The key insight: **SQLite optimises for simplicity and portability; PostgreSQL optimises for concurrency and correctness at scale.**

---

## 2. Architecture Overview

### SQLite — Embedded, Serverless

```
Application Process
┌─────────────────────────────────────────────┐
│  Application Code                           │
│        │                                   │
│  ┌─────▼──────────────────────────────┐    │
│  │         SQLite Library             │    │
│  │  ┌──────────┐  ┌───────────────┐  │    │
│  │  │  Parser  │  │  Code Gen     │  │    │
│  │  └────┬─────┘  └──────┬────────┘  │    │
│  │       └──────┬─────────┘          │    │
│  │        ┌─────▼──────┐             │    │
│  │        │   VM/VDBE  │             │    │
│  │        └─────┬──────┘             │    │
│  │        ┌─────▼──────┐             │    │
│  │        │ B-Tree Mgr │             │    │
│  │        └─────┬──────┘             │    │
│  │        ┌─────▼──────┐             │    │
│  │        │  Pager     │  (page cache│    │
│  │        └─────┬──────┘   & WAL)   │    │
│  └──────────────┼────────────────────┘    │
│                 │                          │
│         Single .db file                    │
└─────────────────────────────────────────────┘
```

Everything runs inside the application process. There is no daemon, no network socket, no authentication handshake. The cost of a query is just CPU + I/O.

### PostgreSQL — Client/Server

```
Client Application
      │  TCP / Unix socket
      ▼
┌─────────────────────┐
│  Postmaster Process │  (listener / supervisor)
│  forks per connection│
└──────┬──────────────┘
       │ fork()
       ▼
┌─────────────────────────────────────────────┐
│  Backend Process (one per connection)       │
│  Parser → Rewriter → Planner → Executor     │
└────────────────┬────────────────────────────┘
                 │ shared memory
                 ▼
┌─────────────────────────────────────────────┐
│  Shared Buffers (default 128 MB)            │
│  WAL buffers  │  Lock table  │  CLOG        │
└─────────────────┬───────────────────────────┘
                  │
         Heap files  +  Index files
         (one file per table/index)
```

PostgreSQL uses a **process-per-connection** model (not threads). Each client gets its own backend process; they communicate only through shared memory. This isolates crashes but costs more RAM per connection.

---

## 3. Internal Design

### 3.1 Storage: File Layout

| | SQLite | PostgreSQL |
|---|---|---|
| Unit of storage | Single `.db` file | Directory of files (`$PGDATA`) |
| Page size | 4096 bytes (default) | 8192 bytes (default) |
| Max DB size | 281 TB | Unlimited (filesystem-bound) |
| Table storage | B-tree of records | Heap files (unordered) |

**SQLite packs everything into one file.** Each table is stored as a B-tree keyed on rowid, so every table is effectively a clustered index. This is great for locality but terrible for tables with many secondary indexes — each index is a separate B-tree within the same file.

**PostgreSQL uses heap files.** Tables are stored as unordered heaps of 8 KB pages. Indexes are separate files. This decouples the physical location of a row from its primary key, which gives more flexibility for updates (the row can move within the heap without updating every index).

### 3.2 Page Layout

**SQLite page (4 KB):**
```
┌────────────────────────────┐
│ Page header (8-12 bytes)   │
│  - page type               │
│  - first freeblock offset  │
│  - number of cells         │
│  - cell content area start │
├────────────────────────────┤
│ Cell pointer array (↓)     │  ← grows downward
├─────────────────  ─────────┤
│       free space           │
├────────────────────────────┤
│ Cell content area   (↑)    │  ← grows upward
└────────────────────────────┘
```

**PostgreSQL heap page (8 KB):**
```
┌────────────────────────────┐
│ PageHeaderData (24 bytes)  │
│  - LSN (for WAL)           │
│  - pd_lower / pd_upper     │
│  - pd_special              │
├────────────────────────────┤
│ ItemId array (4 bytes each)│  ← line pointer array
├─────────────────  ─────────┤
│       free space           │
├────────────────────────────┤
│ Tuple data (heap tuples)   │
│  - t_xmin, t_xmax (MVCC)  │
│  - t_ctid (row pointer)    │
│  - column data             │
├────────────────────────────┤
│ Special space               │
└────────────────────────────┘
```

The critical difference: every PostgreSQL tuple carries `xmin` and `xmax` — transaction IDs that mark when this version was created and deleted. This is the foundation of MVCC.

### 3.3 Concurrency Control

This is the sharpest architectural divergence between the two systems.

**SQLite — WAL mode with file locks:**
- Uses file-level locks (shared / reserved / exclusive)
- In WAL mode: one writer + multiple concurrent readers allowed
- Writer writes to a separate WAL file; readers read from the original DB file
- Serializable isolation — only one writer at a time, ever
- Simple, zero-overhead, perfect for low-concurrency use cases

**PostgreSQL — MVCC:**
- Each row version carries `xmin` (created by) and `xmax` (deleted by)
- When a transaction updates a row, PostgreSQL does **not** overwrite the old version — it marks the old tuple's `xmax` and writes a new tuple
- Each transaction has a snapshot of which transactions were committed at the time it started
- A tuple is visible if: `xmin` committed before the snapshot AND (`xmax` is 0 OR `xmax` was not committed before the snapshot)
- This means **readers never block writers and writers never block readers**
- The trade-off: dead tuples accumulate and must be cleaned up by `VACUUM`

### 3.4 Transaction Management & Durability

Both databases use Write-Ahead Logging (WAL) but for different reasons:

**SQLite WAL:**
- WAL file accumulates changes; periodically checkpointed back to the main DB file
- Provides atomicity: if a crash occurs mid-transaction, the incomplete WAL entries are ignored
- Readers read from the main file; if they need newer data, they check the WAL

**PostgreSQL WAL:**
- Every change is written to the WAL (`pg_wal/`) before it touches the actual data files
- The LSN (Log Sequence Number) in each page header ensures crash recovery can detect stale pages
- On crash recovery, PostgreSQL replays WAL records from the last checkpoint forward
- WAL also enables streaming replication — standby servers replay the same WAL stream

---

## 4. Design Trade-Offs

### Why PostgreSQL uses a client-server architecture

The moment you have multiple processes (or machines) accessing the same database, you need a central coordinator to:
- Assign transaction IDs in order
- Maintain the shared buffer pool (if every client had its own cache, they'd cache different versions of the same page)
- Manage locks
- Run `VACUUM` and `autovacuum` in the background

SQLite bypasses all of this by allowing only one process at a time to write. It delegates coordination to the OS file lock. This works perfectly when the database is embedded in a single application (mobile app, browser, desktop software) but falls apart under concurrent write-heavy workloads.

### The MVCC trade-off

PostgreSQL's MVCC is elegant: readers and writers never conflict. But it creates a problem that SQLite never has — **table bloat**. Every UPDATE creates a new row version; old versions are not immediately reclaimed. The `VACUUM` process must periodically scan tables to mark dead tuples as reusable. If `VACUUM` falls behind (e.g., a long-running transaction holds back the oldest xmin), the table grows indefinitely. PostgreSQL has entire documentation pages dedicated to tuning `autovacuum` — SQLite has no equivalent concept.

### Page size choice

| | SQLite (4 KB) | PostgreSQL (8 KB) |
|---|---|---|
| Match with OS pages | Good for embedded (4 KB = OS page) | Sequential scan efficiency |
| Row size limit | ~1/2 page before overflow | ~2 KB before TOAST |
| Random I/O cost | Lower (smaller read unit) | Higher (8 KB per access) |
| Sequential scan | More I/Os for large tables | Fewer I/Os, better prefetch |

SQLite's 4 KB page matches the typical OS virtual memory page, reducing the cost of mmap-based I/O. PostgreSQL's 8 KB page reduces the number of I/Os during sequential scans of large tables.

### Summary trade-off table

| Property | SQLite | PostgreSQL |
|---|---|---|
| Concurrent writers | 1 (serialized) | Many (MVCC) |
| Reader/writer conflict | Readers blocked in exclusive lock mode | Never block each other |
| Setup required | None (library) | Server + config |
| Crash recovery | WAL replay (fast) | WAL replay + checkpoint |
| Dead tuple cleanup | Not needed | VACUUM required |
| Replication | Not built-in | Streaming replication |
| Max connections | Effectively 1 writer | Hundreds (with pgbouncer: thousands) |

---

## 5. Experiments / Observations

These experiments were run on a `users` table with ~10,000 rows.

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
SELECT pg_relation_size('users') / current_setting('block_size')::int AS pages;  -- 84 pages
```

**Observation:** Despite PostgreSQL using fewer pages (84 vs 90), the total storage footprint is larger (672 KB vs 360 KB). This is because every PostgreSQL tuple carries 23 bytes of MVCC header (`t_xmin`, `t_xmax`, `t_ctid`, etc.) per row, while SQLite rows have minimal overhead.

### 5.2 Query Performance: COUNT(*) on full table

```
SQLite  (without mmap): 0.000118s
SQLite  (with mmap):    0.000144s   ← slightly slower (mmap setup overhead > benefit for 360 KB)
PostgreSQL:             5.156ms
```

**Analysis:** SQLite is ~40x faster for this query not because it's a better database, but because PostgreSQL includes **network round-trip + IPC overhead** between the client process and the backend server process. For an embedded SQLite, the "query" is a function call in the same process — there is no network, no serialization, no process context switch.

The mmap result is also instructive: for a 360 KB file that fits entirely in the OS page cache, setting up a memory mapping adds overhead without benefit. mmap becomes advantageous only when the file is too large for normal I/O caching.

### 5.3 MVCC visibility: observing xmin/xmax

```sql
-- After inserting a row and then updating it:
SELECT xmin, xmax, id, name FROM users WHERE id = 1;
--  xmin | xmax | id | name
-- ------+------+----+-------
--   102 |  103 |  1 | Alice   ← old version, deleted by xid 103
--   103 |    0 |  1 | Alice2  ← new version, still live
```

The old version (xmax=103) is invisible to transactions that started after xid 103 committed, but visible to transactions that started before. This is the MVCC snapshot mechanism in action — there is no lock, just visibility math.

---

## 6. Key Learnings

**1. Architecture follows deployment constraints, not the other way around.**  
SQLite's single-file, no-server design isn't a limitation — it's the entire point. It runs in places where PostgreSQL can't: a mobile app, a browser, a microcontroller. The right question isn't "which is better" but "which fits the deployment model."

**2. MVCC trades storage for concurrency.**  
PostgreSQL's append-only updates mean readers and writers never block each other, which is essential for high-concurrency workloads. But the cost — dead tuples requiring `VACUUM` — is real and non-trivial to tune in production.

**3. Page size is a fundamental parameter, not a detail.**  
The choice of 4 KB vs 8 KB affects I/O amplification, row size limits, and cache efficiency. SQLite chose 4 KB to match the OS virtual memory page; PostgreSQL chose 8 KB to reduce I/O count on sequential scans of large tables. Neither is wrong — they reflect different workload priorities.

**4. The IPC overhead of client-server is real but justified.**  
The 40x latency difference on a COUNT(*) query is entirely network/IPC overhead, not a difference in query execution quality. For applications making thousands of concurrent queries from different clients, that overhead is irrelevant compared to the concurrency and scalability PostgreSQL enables.

**5. WAL solves different problems in each system.**  
In SQLite, WAL primarily enables concurrent reads alongside a single writer. In PostgreSQL, WAL is the foundation of crash recovery, point-in-time recovery, and streaming replication — it's a much more central component of the architecture.
