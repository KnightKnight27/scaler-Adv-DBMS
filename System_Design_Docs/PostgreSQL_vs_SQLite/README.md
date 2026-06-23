# PostgreSQL vs SQLite: Architecture Comparison

## 1. Problem Background

### Why Two Such Different Databases Exist

The existence of both PostgreSQL and SQLite solves fundamentally different problems — and understanding *why* they were built the way they were is the key to understanding their architecture.

**SQLite** was created in 2000 by D. Richard Hipp for the U.S. Navy. The requirement was a database that could run on a destroyer without a DBA, without a server process, and without configuration. The insight was that for many applications, the database is just a smarter file format — not a shared service. Android, iOS, browsers (Firefox, Chrome), and countless embedded systems use SQLite today because the application *is* the sole client.

**PostgreSQL** traces its lineage to the INGRES project at UC Berkeley (1970s), evolving into POSTGRES by 1986, and reaching its current form in 1996. It was designed to handle enterprise workloads: multiple concurrent users, complex queries, large datasets, and strong consistency guarantees. The problem it solves is multi-tenancy and scalability — many applications sharing a single reliable database server.

The core tension: **SQLite optimizes for simplicity and embeddability; PostgreSQL optimizes for concurrency and correctness at scale.**

---

## 2. Architecture Overview

### PostgreSQL: Client-Server Architecture

```
┌──────────────────────────────────────────────────────────┐
│                     PostgreSQL Server                     │
│                                                           │
│  ┌─────────────┐     ┌──────────────────────────────┐    │
│  │  postmaster │────▶│  Backend Process (per client) │    │
│  │  (listener) │     │  - Query parser               │    │
│  └─────────────┘     │  - Planner/optimizer          │    │
│                      │  - Executor                   │    │
│  ┌───────────────┐   └──────────────┬───────────────┘    │
│  │  Shared Memory│◀─────────────────┘                    │
│  │  - shared_buffers (page cache)                         │
│  │  - WAL buffers                                        │
│  │  - Lock table                                         │
│  └───────────────┘                                       │
│                                                           │
│  Background Workers: WAL writer, checkpointer, autovacuum │
└──────────────────────────────────────────────────────────┘
         │
         ▼  (network / unix socket)
┌──────────────┐
│   Clients    │  (psql, application, pgAdmin)
└──────────────┘
```

Key architectural facts:
- One **backend process** is forked per connection (not a thread — a full OS process)
- All backend processes share a single **shared memory segment** for the buffer pool
- The `postmaster` is the supervisor; it monitors backends and handles crashes
- WAL writer, checkpointer, autovacuum are separate background processes

**Why process-per-connection?** PostgreSQL was designed in the pre-threading era of Unix. Process isolation means a crashed backend cannot corrupt shared state for other clients. The trade-off is higher memory overhead per connection (each process ~5-10 MB), which is why connection poolers like PgBouncer are common in production.

### SQLite: Embedded Library Architecture

```
┌────────────────────────────────────────────────────────┐
│                    Application Process                  │
│                                                         │
│  ┌───────────────────────────────────────────────────┐  │
│  │              SQLite Library (libsqlite3)           │  │
│  │                                                    │  │
│  │  SQL Parser → Query Planner → Bytecode VM          │  │
│  │                    │                               │  │
│  │              Page Cache (in-process)               │  │
│  │                    │                               │  │
│  └────────────────────┼───────────────────────────────┘  │
│                       │                                  │
│          OS File Locking (mandatory)                     │
└───────────────────────┼──────────────────────────────────┘
                        │
                        ▼
              ┌─────────────────┐
              │  Single .db file │  (all data, indexes, schema)
              └─────────────────┘
```

Key architectural facts:
- SQLite is a **library**, not a server. It links into the application at compile time.
- There is **no separate process** — the application IS the database client and server
- A database is a **single cross-platform file** on disk
- All concurrency is managed via OS file locks, not an internal lock manager
- The query engine uses a **bytecode virtual machine** (VDBE) to execute queries

---

## 3. Internal Design

### Storage Engine Architecture

| Aspect | PostgreSQL | SQLite |
|--------|-----------|--------|
| File layout | Multiple files per database (one per table/index) | Single file for entire database |
| Page size | 8 KB (compile-time default) | 4 KB (configurable 512B–65536B) |
| Table storage | Heap files (unordered rows) | B-Tree pages (rows stored in B-tree) |
| Index storage | Separate B-Tree files | B-Tree pages in the same file |
| Row format | Fixed header + variable-length attributes | Header + variable-length fields |

**PostgreSQL Page Layout (8 KB page):**
```
┌──────────────────────────────────────────┐
│  Page Header (24 bytes)                  │
│  - LSN, flags, free space pointers       │
├──────────────────────────────────────────┤
│  Item Identifiers (array of 4-byte slots)│
│  - (offset, length) pairs pointing down  │
├──────────────────────────────────────────┤
│  Free Space (grows from both ends)       │
├──────────────────────────────────────────┤
│  Tuples (heap items, grow upward)        │
│  - (xmin, xmax, ctid, data...)           │
└──────────────────────────────────────────┘
```

The item identifier array at the top means row offsets within a page never change (important for index references using `ctid`). When a row is deleted, only the item identifier slot is marked dead — the tuple bytes remain until VACUUM.

**SQLite Page Layout:**
SQLite stores table rows in B-Tree leaf pages. Unlike PostgreSQL's heap model, *every table is a B-Tree indexed by rowid* (or the INTEGER PRIMARY KEY). This means SQLite has **no separate heap file** — it's closer to InnoDB's clustered index than to PostgreSQL's heap.

```
┌──────────────────────────────────────────┐
│  Page Header (8 or 12 bytes)             │
│  - page type, freeblock, cell count      │
├──────────────────────────────────────────┤
│  Cell Pointer Array                      │
├──────────────────────────────────────────┤
│  Unallocated Space                       │
├──────────────────────────────────────────┤
│  Cell Content Area (records / keys)      │
└──────────────────────────────────────────┘
```

### Concurrency Control

This is the most architecturally significant difference.

**PostgreSQL MVCC (Multi-Version Concurrency Control):**
- Every row stores `xmin` (transaction that inserted it) and `xmax` (transaction that deleted it)
- A query takes a **snapshot** at start and sees only rows where `xmin` is committed and `< snapshot_xid`
- Writers never block readers; readers never block writers
- Old row versions accumulate as **dead tuples** — VACUUM is needed to reclaim space
- Supports full Serializable Snapshot Isolation (SSI)

```
Timeline of a row update in PostgreSQL:
  xmin=100, xmax=0  →  row visible to txn 101, 102, ...
  UPDATE happens (txn 150):
  xmin=100, xmax=150  (old version — dead after snapshot passes)
  xmin=150, xmax=0   (new version — visible to txn ≥ 150)
```

**SQLite Locking:**
SQLite uses a much simpler locking model. It has 5 lock states:
```
UNLOCKED → SHARED → RESERVED → PENDING → EXCLUSIVE
```
- Multiple readers can hold SHARED locks simultaneously
- Only one writer can hold EXCLUSIVE lock at a time
- In default rollback journal mode: **writers block all readers**
- In WAL mode: readers and writers can coexist (readers read from the database file, writer appends to WAL)

SQLite WAL mode significantly improved concurrency but still only allows **one concurrent writer**. This is the fundamental scalability ceiling.

### Durability Mechanisms

**PostgreSQL WAL:**
- Every change is written to the Write-Ahead Log *before* the data page is modified
- On crash, PostgreSQL replays WAL records from the last checkpoint
- `fsync` ensures WAL records reach durable storage before acknowledging commits
- WAL enables streaming replication — replicas replay the same WAL stream

**SQLite Journal Modes:**
- **Rollback journal**: Before modifying a page, SQLite copies the original to a `-journal` file. On crash, it rolls back using the journal.
- **WAL mode**: Changes are appended to a `-wal` file. Readers use the main DB file; the WAL is checkpointed back periodically. More performant, better concurrency.

---

## 4. Design Trade-Offs

### Why SQLite Works for Mobile Applications

1. **Zero configuration**: No port, no user accounts, no pg_hba.conf. A `.db` file is a database.
2. **Single file portability**: Back up by copying one file. Deploy by shipping one file.
3. **Process model irrelevant**: A phone app is the sole user of its database. The single-writer limitation doesn't matter.
4. **Low memory footprint**: The in-process page cache can be tuned to kilobytes if needed.
5. **No network overhead**: Function call vs TCP round-trip for every query.

### Why PostgreSQL Is Preferred for Multi-User Systems

1. **True concurrent writers**: MVCC allows many transactions simultaneously without blocking reads.
2. **Isolation levels**: PostgreSQL supports Read Committed, Repeatable Read, and full Serializable isolation — critical for financial and inventory systems.
3. **Scale-out**: Streaming replication, logical replication, and tools like Citus enable horizontal scaling.
4. **Advanced types and extensions**: PostGIS (geospatial), pg_trgm (fuzzy search), TimescaleDB (time-series).
5. **VACUUM trade-off**: The cost of MVCC is that dead tuples accumulate and must be reclaimed. On write-heavy tables with VACUUM disabled, table bloat becomes a real operational concern.

### The Hidden Cost of Simplicity (SQLite)

SQLite's simplicity creates surprising limitations:
- `ALTER TABLE` is extremely limited (no `DROP COLUMN` until SQLite 3.35)
- No user management or access control
- `FOREIGN KEY` constraints are disabled by default (have to `PRAGMA foreign_keys = ON`)
- Concurrent writes from multiple processes are serialized — not just slow, but literally sequential

### The Hidden Cost of Power (PostgreSQL)

- Connection overhead: each connection is a process (~5-10MB RAM). 500 connections = 5GB just for overhead.
- Operational complexity: you need to tune `shared_buffers`, `work_mem`, `wal_level`, run VACUUM, monitor bloat.
- Write amplification from WAL: every write is written twice (WAL + data page).

---

## 5. Experiments / Observations

### Observation 1: SQLite Write Contention

When two processes attempt concurrent writes to the same SQLite database:

```python
# Process 1 and Process 2 both execute:
conn = sqlite3.connect("test.db", timeout=5)
conn.execute("INSERT INTO logs VALUES (?)", (value,))
conn.commit()
```

With default journal mode, Process 2 gets `OperationalError: database is locked`. With WAL mode (`PRAGMA journal_mode=WAL`), concurrent writes still serialize at the OS level but with much lower contention since readers don't need to wait.

**Lesson**: WAL mode is almost always better for any non-trivial workload.

### Observation 2: PostgreSQL MVCC Dead Tuple Accumulation

```sql
-- Create a test table and observe dead tuples
CREATE TABLE bloat_test (id serial, val text);
INSERT INTO bloat_test SELECT i, md5(i::text) FROM generate_series(1, 100000) i;

-- Update all rows (creates 100k dead tuples)
UPDATE bloat_test SET val = md5(val);

-- Check dead tuples
SELECT relname, n_live_tup, n_dead_tup, last_autovacuum
FROM pg_stat_user_tables
WHERE relname = 'bloat_test';
```

Before VACUUM: `n_dead_tup = 100000`. After `VACUUM bloat_test`: `n_dead_tup = 0`.

This demonstrates that PostgreSQL's MVCC model requires garbage collection as a first-class operational concern.

### Observation 3: PostgreSQL Query Plan on a Join

```sql
EXPLAIN ANALYZE
SELECT o.id, c.name, SUM(oi.quantity * oi.price)
FROM orders o
JOIN customers c ON c.id = o.customer_id
JOIN order_items oi ON oi.order_id = o.id
GROUP BY o.id, c.name;
```

Sample output (simplified):
```
HashAggregate  (cost=8432.10..8534.12 rows=10202 width=52) (actual time=145.3..148.1 rows=9876)
  ->  Hash Join  (cost=1240.50..7982.00 rows=50012 width=36)
        Hash Cond: (oi.order_id = o.id)
        ->  Seq Scan on order_items  (cost=0..2100 rows=50012)
        ->  Hash  (cost=980..980 rows=10000)
              ->  Hash Join
                    ->  Seq Scan on orders
                    ->  Hash on customers
```

The planner chose Hash Join (not Nested Loop) because `pg_statistic` indicated both tables have ~10k rows — hash joins are O(n+m) vs nested loop's O(n*m). This decision comes directly from `ANALYZE`-collected statistics.

---

## 6. Key Learnings

**Architecture reflects deployment context, not just technical capability.**
SQLite's embedded model isn't a limitation — it's a deliberate fit for single-process use cases. PostgreSQL's process-per-connection model was a deliberate choice for isolation, not ignorance of threads.

**Concurrency is the deepest architectural driver.**
Almost every major difference between PostgreSQL and SQLite can be traced back to their concurrency models. MVCC in PostgreSQL enables high-throughput multi-user workloads but requires VACUUM. SQLite's file locking is simple and correct but caps concurrency.

**"Simple" databases are hard to build correctly.**
SQLite's source is ~150,000 lines of carefully written C. Its test suite is more than 90 million test cases. Simplicity from the user's perspective requires enormous engineering rigor underneath.

**Trade-offs are not flaws.**
SQLite can't handle 500 concurrent writers. That's not a bug — it's a consequence of a design optimized for different goals. Choosing the right database means understanding these trade-offs, not finding the "best" database.

---

*References: PostgreSQL Source Code (src/backend/), SQLite Internals documentation (sqlite.org/fileformat.html), "Architecture of a Database System" — Hellerstein, Stonebraker, Hamilton*
