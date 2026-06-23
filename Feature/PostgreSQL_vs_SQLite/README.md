# PostgreSQL vs SQLite — Architecture Comparison

## 1. Problem Background

PostgreSQL and SQLite solve very different problems, which is why they look nothing alike internally.

- **SQLite (2000, D. Richard Hipp)** was built so that an application can use SQL without running a server. The database is a single file linked into the program. Use cases: phones, browsers, embedded devices — anywhere there is exactly one application talking to the data.
- **PostgreSQL (from POSTGRES, UC Berkeley, 1986)** was built for many concurrent users sharing one trusted server, with strong consistency, complex queries, and durability under crashes. Use cases: web backends, analytics, anything multi-user.

In one line: **SQLite is a library, PostgreSQL is a server.** Almost every architectural difference flows from that.

---

## 2. Architecture Overview

### PostgreSQL — Client–Server, Process-per-Connection
```
   Clients (psql, app)
          │  TCP / Unix socket
          ▼
   ┌──────────────────────────┐
   │ postmaster (supervisor)  │
   └──────────────┬───────────┘
                  │ fork()
   ┌──────────────▼───────────┐    ┌─────────────────────┐
   │ Backend process / client │◀──▶│  Shared Memory      │
   │  parser → planner → exec │    │  shared_buffers     │
   └──────────────────────────┘    │  WAL buffers, locks │
                                   └─────────────────────┘
   Background: WAL writer, checkpointer, autovacuum, bgwriter
```

### SQLite — Embedded Library
```
   ┌──────────────────────────────────┐
   │ Application process              │
   │   ┌──────────────────────────┐   │
   │   │ libsqlite3               │   │
   │   │  parser → planner → VDBE │   │
   │   │  page cache              │   │
   │   └─────────────┬────────────┘   │
   └─────────────────┼────────────────┘
                     │  OS file locks
                     ▼
              single .db file
              (+ optional -wal, -shm)
```

---

## 3. Internal Design

### Storage Layout

| | PostgreSQL | SQLite |
|---|---|---|
| File layout | One file per table & index (in tablespace dir) | One `.db` file holds everything |
| Page size | 8 KB default | 4 KB default (512 B – 64 KB) |
| Table storage | **Heap** (unordered tuples) + separate indexes | **Clustered B-tree** keyed by rowid |
| Row identity | `ctid = (block, item)` | `rowid` (INTEGER PRIMARY KEY) |

PostgreSQL page (slotted page):
```
[ header | item-ptr → | free space ← | tuples ]
```
Item pointers grow downward, tuples grow upward. Deleted tuples leave dead space until VACUUM reclaims it.

SQLite page: a B-tree node — cell pointers at the top, cells (key+payload or just key) at the bottom.

### Indexes
- **PostgreSQL**: separate B-tree files; leaves store `(key → ctid)`. Heap lookups follow the ctid.
- **SQLite**: the table itself is a B-tree, so a primary-key lookup IS the heap fetch (no second hop). Secondary indexes store `(key → rowid)` and need a second B-tree descent.

### Concurrency Control
- **PostgreSQL — MVCC**: every tuple carries `xmin`/`xmax`. UPDATE writes a new tuple version; readers see the version visible to their snapshot. Readers don't block writers and vice-versa. Cost: dead tuples accumulate → VACUUM.
- **SQLite — file locks**: states `UNLOCKED → SHARED → RESERVED → PENDING → EXCLUSIVE`. Rollback-journal mode: a writer blocks all readers. **WAL mode** lets readers and the single writer proceed in parallel, but still only one writer at a time.

### Durability
- **PostgreSQL WAL**: every change is logged before the dirty page is flushed; on crash, replay WAL from last checkpoint. Same WAL stream feeds streaming replication.
- **SQLite**: either a **rollback journal** (copy original pages aside, restore on crash) or a **WAL file** (append new pages, checkpoint into main file periodically).

---

## 4. Design Trade-Offs

**SQLite — wins**
- Zero config, one file, no network round-trip — a function call.
- Tiny footprint, ideal where the app is the only client.

**SQLite — costs**
- One writer at a time, no real user management, limited `ALTER TABLE`, FK off by default.
- Doesn't scale to many concurrent writers.

**PostgreSQL — wins**
- True multi-user concurrency, Serializable isolation, replication, rich types/extensions.

**PostgreSQL — costs**
- ~5–10 MB RAM per connection (process model) → PgBouncer is almost always needed.
- VACUUM, bloat, autovacuum tuning are real operational concerns.
- WAL means every write hits disk twice.

---

## 5. Experiments / Observations

**SQLite write contention.** Two scripts writing to the same DB in default journal mode: the second gets `database is locked`. After `PRAGMA journal_mode=WAL`, both succeed (serialized internally, but readers no longer blocked).

**PostgreSQL dead tuples after bulk UPDATE.**
```sql
CREATE TABLE t AS SELECT i, md5(i::text) v FROM generate_series(1,100000) i;
UPDATE t SET v = md5(v);
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname='t';
-- live=100000, dead=100000
VACUUM t;        -- dead drops to 0
```
Confirms MVCC creates a new version per row and only VACUUM reclaims it.

**Plan choice via statistics.** `EXPLAIN ANALYZE` on a 3-table join shows the planner switching from Nested Loop to Hash Join once `ANALYZE` populates `pg_statistic` with realistic row counts — same query, very different plan and runtime.

---

## 6. Key Learnings

- Architecture is shaped by the deployment story. SQLite chose a library because there is one client; PostgreSQL chose processes because it needed isolation between many.
- Concurrency is the deepest divider: MVCC (versions + VACUUM) vs file locks (simple, single-writer).
- "Simple" databases (SQLite is ~150 K lines of C with millions of test cases) are not easy — simplicity at the API costs a lot of engineering inside.
- There is no "better" database — only a better fit for a workload.
