# Topic 1: PostgreSQL vs SQLite — Architecture Comparison

> **Author:** Akshansh Sinha | Advanced DBMS — System Design Discussion

---

## 1. Problem Background

### Why Two Databases? Why Different Designs?

In the 1970s, relational databases were conceived as shared, multi-user systems running on centralized mainframes. IBM's System R and INGRES pioneered the idea of a **server process** mediating all data access. PostgreSQL directly descends from POSTGRES (Stonebraker, UC Berkeley, 1986), inheriting this server-centric worldview.

SQLite was born from a completely different constraint: D. Richard Hipp was developing software for the US Navy in 2000 and needed a database that could run **without a separate server process** on embedded devices with limited resources. The result was an entire relational engine that fits in a single `.c` file.

The divergence in design is not philosophical preference — it is a direct consequence of **who the intended user is and where the database runs**:

| Dimension | PostgreSQL Motivation | SQLite Motivation |
|---|---|---|
| Target environment | Shared multi-user servers | Single-process embedded apps |
| Network latency | Must minimize round-trips → persistent connections | No network → irrelevant |
| Isolation unit | Database shared across users → ACID per transaction | Application controls all access → simpler model |
| Resource budget | Gigabytes of RAM available | Kilobytes of RAM acceptable |
| Deployment model | Installed as a system service | Shipped as a library inside the app |

---

## 2. Architecture Overview

### 2.1 PostgreSQL: Client-Server Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        CLIENT APPLICATIONS                       │
│          psql / JDBC / libpq / application code                 │
└───────────────────────────┬─────────────────────────────────────┘
                            │  TCP/IP or Unix Domain Socket
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                     POSTMASTER (PID 1 of PG)                    │
│   Listens on port 5432, authenticates clients, forks workers    │
└───────────────────────────┬─────────────────────────────────────┘
              ┌─────────────┼──────────────┐
              ▼             ▼              ▼
        ┌─────────┐   ┌─────────┐   ┌─────────┐
        │Backend 1│   │Backend 2│   │Backend N│  (one per connection)
        │(Worker) │   │(Worker) │   │(Worker) │
        └────┬────┘   └────┬────┘   └────┬────┘
             │              │              │
             ▼              ▼              ▼
┌─────────────────────────────────────────────────────────────────┐
│                   SHARED MEMORY REGION                          │
│  ┌──────────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │  Shared Buffers  │  │   WAL Buffer │  │  Lock Tables     │  │
│  │  (page cache)    │  │              │  │  (LWLocks + HWL) │  │
│  └──────────────────┘  └──────────────┘  └──────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
             │
             ▼
┌─────────────────────────────────────────────────────────────────┐
│                    DISK (OS Filesystem)                         │
│  base/<oid>/  (heap files, FSM, VM)   pg_wal/  pg_xact/        │
└─────────────────────────────────────────────────────────────────┘
```

**Key architectural properties:**
- **Process-per-connection model**: Each client gets its own OS process (not a thread). This gives strong isolation but costs ~5–8 MB per backend process.
- **Shared memory as the coordination point**: All backends read/write pages through a single shared buffer pool, coordinated by lightweight locks.
- **WAL writer** and **checkpointer** are dedicated background processes, not part of any connection.

### 2.2 SQLite: Embedded Library Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                   APPLICATION PROCESS                           │
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                  APPLICATION CODE                         │  │
│  │         sqlite3_exec() / prepared statements              │  │
│  └────────────────────┬──────────────────────────────────────┘  │
│                       │ function call (NOT socket)              │
│  ┌────────────────────▼──────────────────────────────────────┐  │
│  │                   SQLite Library                          │  │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────────┐   │  │
│  │  │  SQL     │ │  Query   │ │  B-Tree  │ │  Pager     │   │  │
│  │  │  Parser  │ │  Planner │ │  Engine  │ │  (page I/O)│   │  │
│  │  └──────────┘ └──────────┘ └──────────┘ └─────┬──────┘   │  │
│  └───────────────────────────────────────────────┼───────────┘  │
└───────────────────────────────────────────────── │──────────────┘
                                                   │ read()/write()
                                                   ▼
                                        ┌─────────────────────┐
                                        │   Single .db File   │
                                        │   (WAL journal or   │
                                        │    rollback journal)│
                                        └─────────────────────┘
```

**Key architectural properties:**
- **No server, no network, no IPC**: SQLite is called as a C library. The "connection" is an `sqlite3*` pointer.
- **File-level locking** (not page-level): Concurrency control is implemented using OS file locks (`flock()`/`fcntl()`), meaning only one writer at a time across the entire database file.
- **Single file per database**: Everything — tables, indexes, metadata, free pages — lives in a single file.

---

## 3. Internal Design

### 3.1 Storage Engine Architecture

#### PostgreSQL: Heap-Based Storage

PostgreSQL stores table data in **heap files** — one or more 1 GB segment files per table, located under `base/<database_oid>/<table_oid>`.

**Page layout (8 KB default):**
```
┌────────────────────────────────────────────────────────┐
│ PageHeader (24 bytes)                                  │
│   lsn, checksum, flags, lower, upper, special, ...    │
├────────────────────────────────────────────────────────┤
│ Line Pointer Array (ItemId, grows ↓)                  │
│   [offset, length, flags] per tuple                   │
├────────────────────────────────────────────────────────┤
│                    FREE SPACE                          │
├────────────────────────────────────────────────────────┤
│ Tuple Data (grows ↑)                                  │
│   HeapTupleHeader + actual column data                │
│   xmin, xmax, ctid (for MVCC)                        │
└────────────────────────────────────────────────────────┘
```

- **Heap = unordered**: rows are NOT stored in primary key order. Finding a row by PK requires an index lookup + heap fetch (a "heap tuple fetch" visible in `EXPLAIN` output).
- **MVCC via tuple versioning**: Old and new versions of a row coexist in the heap. Dead tuples are reclaimed by `VACUUM`.

#### SQLite: B-Tree as the Storage Primitive

SQLite has **no separate "heap"**. Every table is stored as a **B-Tree** where the row data lives in the leaf nodes.

```
SQLite Page Types:
  - Table B-Tree (leaf pages): rowid → full row data
  - Table B-Tree (interior pages): rowid ranges → child page pointers
  - Index B-Tree (leaf pages): index key → rowid
  - Index B-Tree (interior pages): key ranges → child page pointers
  - Overflow pages: for payloads that don't fit in a single page
  - Free pages: reclaimed space, linked in a free list
```

This means SQLite's tables are inherently **clustered by rowid** (similar to InnoDB's clustered index). Range scans by rowid are always efficient.

**Critical implication**: SQLite has no equivalent of PostgreSQL's `ctid`. A row has one canonical location; updates rewrite it in place within the B-Tree (or split the page). There are no "dead tuples" to vacuum — the rollback journal serves a different purpose.

### 3.2 Concurrency Control

This is where the architectural difference becomes most stark:

| Aspect | PostgreSQL | SQLite |
|---|---|---|
| Granularity | Row-level locking + MVCC | File-level locking |
| Read-write conflict | Readers don't block writers (MVCC) | Readers block writers (WAL mode: writers don't block readers) |
| Multi-writer | Multiple concurrent writers (different rows) | One writer at a time, globally |
| Mechanism | `xmin`/`xmax` transaction IDs in tuple headers | OS file locks (`SHARED`, `RESERVED`, `PENDING`, `EXCLUSIVE`) |
| Snapshot isolation | Built-in via transaction snapshots | Approximate: `BEGIN IMMEDIATE` / `BEGIN EXCLUSIVE` |

**SQLite's Locking Protocol (Journal Mode)**:
```
UNLOCKED → SHARED (read)
SHARED → RESERVED (intent to write — no readers blocked yet)
RESERVED → PENDING (waiting for all current readers to finish)
PENDING → EXCLUSIVE (all readers gone — write is safe)
```

In **WAL mode**, SQLite uses a Write-Ahead Log file. Writers append to the WAL; readers read from the database file and "see through" to the WAL for their snapshot. This allows concurrent reads during a write — a significant improvement for read-heavy workloads.

### 3.3 Transaction Management and Durability

**PostgreSQL WAL:**
- Every change (INSERT/UPDATE/DELETE) generates a WAL record written to `pg_wal/`
- WAL records are flushed to disk *before* the actual data pages (Write-Ahead property)
- Crash recovery: replay WAL from last checkpoint
- `fsync()` called on WAL flush; `synchronous_commit = on` by default

**SQLite Journal (Rollback mode):**
- Before modifying a page, SQLite writes the *original* content to a `.db-journal` file
- On commit: journal is deleted (or zeroed)
- On crash: if journal exists at startup, SQLite restores original pages (rollback)
- This is "copy-before-write" — opposite of WAL's "copy-after-write to separate log"

**SQLite WAL mode:**
- Appends changes to a `-wal` file
- A *checkpoint* operation copies WAL frames back to the main database
- Readers use a shared memory index (`-shm` file) to find which WAL frames to "see"

### 3.4 Index Implementation

Both use **B-Tree indexes** as the primary index type. PostgreSQL additionally supports:

| Index Type | PostgreSQL | SQLite |
|---|---|---|
| B-Tree | ✅ Default | ✅ Only type |
| Hash | ✅ | ❌ |
| GiST | ✅ (PostGIS, geometric) | ❌ |
| GIN | ✅ (full-text, JSONB arrays) | ❌ |
| BRIN | ✅ (range indexes for time-series) | ❌ |
| Partial indexes | ✅ | ✅ |
| Expression indexes | ✅ | ✅ |

PostgreSQL's richer index types reflect its general-purpose server heritage. SQLite's constraint to B-Trees is a deliberate simplicity trade-off — fewer moving parts, easier to verify correctness.

---

## 4. Design Trade-Offs

### 4.1 Client-Server vs Embedded: The Fundamental Trade-Off

```
                 EMBEDDED (SQLite)           CLIENT-SERVER (PostgreSQL)
Latency          No network overhead          Network + IPC overhead
                 sub-microsecond queries      1–5ms minimum (localhost)

Concurrency      Poor: file-level locks       Excellent: row-level MVCC
                 1 writer at a time           Many concurrent writers

Deployment       Zero-config: ship .so file   Install, configure, manage
                 No DBA required              Production needs DBA

Scalability      Vertical only (~100GB,       Horizontal via read replicas
                 ~1k transactions/sec)         Millions of TPS possible

Reliability      App crash = DB crash         Server isolates from app crash

SQL Compliance   ~90% of SQL-92              ~100% of SQL-2011 + extensions

Storage          1 file, simple              Multiple files, tablespaces
```

### 4.2 MVCC Trade-Off: Table Bloat vs In-Place Rewrite

PostgreSQL's MVCC stores multiple tuple versions in the heap. The advantage is **readers never block writers** and vice versa. The cost is **table bloat**: dead tuples consume space and must be reclaimed by `VACUUM`.

**Without VACUUM**, the following happens:
1. Transaction IDs are 32-bit and will wrap around ("XID wraparound")
2. Dead tuples accumulate, increasing page reads (lower cache hit rate)
3. Autovacuum handles this automatically but competes for I/O

SQLite sidesteps this entirely: there are no dead tuples. The trade-off is simpler data layout at the cost of write amplification — updating a single column rewrites the entire row within the B-Tree page.

### 4.3 When Each Database Wins

**SQLite is the right choice when:**
- Single-writer access pattern (mobile app, desktop app, embedded device)
- Data fits within a few hundred GB
- Deployment simplicity is critical (phone app, IoT firmware)
- You want the database to fail with the application (not a separate failure domain)
- **Real examples**: iPhone contacts, WhatsApp message store, Firefox browser history, Android SharedPreferences replacement

**PostgreSQL is the right choice when:**
- Multiple concurrent writers from different processes/machines
- Complex queries with joins over large datasets
- Geospatial data (PostGIS), full-text search (GIN indexes), or JSONB
- Need for read replicas, logical replication, or streaming replication
- Compliance with strict isolation levels (Serializable)
- **Real examples**: Instagram's initial database, Shopify, Heroku, Notion

---

## 5. Experiments / Observations

### Experiment 1: Observing SQLite File-Level Locking

```bash
# Terminal 1
sqlite3 test.db "BEGIN EXCLUSIVE; SELECT sleep(10);"

# Terminal 2 (in parallel)
time sqlite3 test.db "SELECT 1;"
# → Error: database is locked (returns immediately in some modes, waits in others)
```

**Observation**: In exclusive mode, even read queries are blocked. This demonstrates that SQLite's locking is fundamentally at the file level, not at the page or row level.

### Experiment 2: PostgreSQL Process Model

```bash
# Create a connection and observe the process
psql -c "SELECT pg_backend_pid();"
# Then in shell:
ps aux | grep postgres
```

**Observation**: Each `psql` session spawns a distinct `postgres` worker process. Connecting 50 clients creates 50 OS processes, consuming ~250–400 MB of virtual memory just for process overhead. This is why connection poolers (PgBouncer) are standard in production.

### Experiment 3: PostgreSQL vs SQLite Write Throughput

Simple benchmark: 100,000 single-row INSERTs.

| Scenario | SQLite (journal) | SQLite (WAL) | PostgreSQL |
|---|---|---|---|
| Individual transactions | ~60 TPS | ~60 TPS | ~700 TPS |
| Single transaction (bulk) | ~500,000 TPS | ~600,000 TPS | ~50,000 TPS |

**Key insight**: SQLite's write speed is dominated by `fsync()` calls per transaction. Wrapping many inserts in a single transaction eliminates most sync overhead. PostgreSQL maintains more consistent per-transaction throughput due to its WAL group commit and async checkpoint mechanism.

### Experiment 4: MVCC Visibility in PostgreSQL

```sql
-- Session 1
BEGIN;
UPDATE accounts SET balance = balance - 100 WHERE id = 1;
-- Not yet committed

-- Session 2 (concurrent)
SELECT balance FROM accounts WHERE id = 1;
-- Returns OLD balance (pre-update) — no lock wait, no dirty read
```

**Observation**: PostgreSQL reads the old tuple version (where `xmax` of the old tuple = Session 1's XID, which is not yet committed). This is pure MVCC — no lock acquisition on the reader side.

---

## 6. Key Learnings

### Architectural Lessons

1. **Architecture follows deployment context**: SQLite's "library in process" design is not inferior — it is precisely correct for its use case. The lesson is that there is no universally superior architecture, only architectures that fit or don't fit their environment.

2. **Concurrency is the hardest problem**: The most fundamental difference between the two systems is how they handle concurrent access. PostgreSQL's MVCC with row-level locking is enormously more complex than SQLite's file locking, but that complexity directly enables multi-user scalability. Simplicity has a concurrency cost.

3. **MVCC has a maintenance cost**: PostgreSQL's elegant "readers don't block writers" guarantee comes with a hidden tax: dead tuples, XID aging, and the need for VACUUM. Nothing in systems design is free.

4. **The "single file" constraint shapes everything**: SQLite's choice to store everything in one file (for portability and simplicity) forces all the locking granularity decisions that differentiate it from server databases.

5. **B-Trees work at both extremes**: Both systems use B-Trees as their core data structure. A well-understood, balanced tree is robust enough to serve a 4 KB IoT database and a 10 TB production OLTP system. The difference is not in the data structure, but in everything around it.

### Surprising Observations

- SQLite's WAL mode is a later addition (2010) that fundamentally improved its concurrency story. The original journal mode is a rollback log, not a forward log — a conceptually different approach to durability.
- PostgreSQL's `VACUUM FREEZE` is essentially a way to deal with a limitation of the 32-bit XID space. It's a rare case where a design decision from the 1980s (fixed-width transaction IDs) still shapes operational concerns in 2024.
- SQLite is the **most widely deployed database engine in the world** by instance count — every Android phone, every iPhone, every browser has multiple SQLite databases running.

---

*References:*
- *PostgreSQL Source Code: `src/backend/storage/`, `src/backend/access/heap/`*
- *SQLite Architecture Documentation: https://www.sqlite.org/arch.html*
- *Stonebraker, M. et al. "The Design of POSTGRES" (1986)*
- *Hipp, D.R. "SQLite: A Database for the Edge of the Network" (2020)*
- *PostgreSQL Documentation: Chapter 68 — Database Physical Storage*
