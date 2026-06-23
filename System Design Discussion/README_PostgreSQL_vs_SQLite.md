# PostgreSQL vs SQLite: Architectural Comparison

## 1. Problem Background

### Why Do These Two Databases Exist?

SQLite and PostgreSQL were built to solve fundamentally different problems — and understanding that difference explains almost every architectural decision each system makes.

**SQLite** (created by D. Richard Hipp in 2000) was designed for situations where you need a database but don't want to manage one. The original use case was embedded systems on US Navy destroyers. The goal: zero configuration, zero administration, zero server. The entire database lives in a single file on disk. This was a radical idea — treat the database engine like a library you link into your application, not a service you connect to.

**PostgreSQL** (originating from INGRES at UC Berkeley in the 1980s, open-sourced in 1996) was designed to be a full-featured, enterprise-grade relational database for multi-user environments. The problem it solves is: how do you give hundreds of concurrent users safe, consistent access to shared data while supporting complex queries, constraints, and large datasets?

These different origins produce two systems that look similar on the surface (both are SQL databases) but are deeply different in architecture.

---

## 2. Architecture Overview

### High-Level Diagram

```
SQLite Architecture                    PostgreSQL Architecture
─────────────────────                  ──────────────────────────────────────

  [Application Process]                 [Client App]   [Client App]   [Client App]
         │                                   │               │               │
   ┌─────▼──────┐                            └───────────────┴───────────────┘
   │ SQLite Lib │  (linked in)                               │ TCP/IP (port 5432)
   └─────┬──────┘                                   ┌────────▼────────┐
         │                                           │  Postmaster     │ (listener)
   ┌─────▼──────┐                                   └────────┬────────┘
   │  SQL Parser│                                            │ fork()
   │  Optimizer │                                   ┌────────▼────────┐
   │  VM (VDBE) │                                   │ Backend Process │ (per client)
   └─────┬──────┘                                   │  ┌────────────┐ │
         │                                          │  │SQL Parser  │ │
   ┌─────▼──────┐                                   │  │Optimizer   │ │
   │  Pager     │  (page cache, locking)             │  │Executor    │ │
   └─────┬──────┘                                   │  └─────┬──────┘ │
         │                                          └────────┼────────┘
   ┌─────▼──────┐                                            │
   │  .db file  │  (single file on disk)            ┌────────▼────────┐
   └────────────┘                                   │  Shared Memory  │
                                                    │  (shared_buffers│
                                                    │   lock tables)  │
                                                    └────────┬────────┘
                                                             │
                                                    ┌────────▼────────┐
                                                    │  Data Directory │
                                                    │  (base/, pg_wal/│
                                                    │   pg_xact/...)  │
                                                    └─────────────────┘
```

### Core Architectural Difference: Embedded vs Client-Server

| Dimension | SQLite | PostgreSQL |
|---|---|---|
| Deployment model | Library embedded in app process | Separate server process |
| Communication | Function calls (in-process) | Network/socket (IPC) |
| Config needed | None | pg_hba.conf, postgresql.conf |
| Multi-user | Limited (file-level locking) | Full (row-level locking) |
| Data location | Single `.db` file | Directory of files |

---

## 3. Internal Design

### 3.1 Process Model

**SQLite** runs entirely within the application's process. There is no separate server. When your app opens a SQLite database, it links against the SQLite library and reads/writes the `.db` file directly. This has a profound implication: multiple processes can open the same file, but SQLite must coordinate them through OS-level file locks — which is slow and limited.

**PostgreSQL** uses a **process-per-connection** model (not threads). The `postmaster` is the master daemon. When a client connects, postmaster `fork()`s a new backend process dedicated to that connection. All backend processes share a common memory region (`shared_buffers`) for the page cache, and use kernel IPC for lock management.

Why fork instead of threads? PostgreSQL predates modern thread-safe libraries. Processes also provide better fault isolation: if one backend crashes, others survive. The downside is that forking is expensive, which is why connection poolers like PgBouncer exist in practice.

### 3.2 Storage Engine & File Organization

**SQLite File Layout:**

```
myapp.db
├── Page 1: Database header (100-byte header + B-tree root)
├── Page 2: sqlite_master table (schema)
├── Page N: Table B-tree pages
└── Page M: Index B-tree pages
```

Everything — schema, tables, indexes — is packed into a single file with a fixed page size (default 4096 bytes). The "Pager" module manages reading/writing pages and maintains a page cache. SQLite uses a B-tree for every table (a "table B-tree" where rows are stored at the leaves, keyed by rowid).

**PostgreSQL File Layout:**

```
$PGDATA/
├── base/
│   └── <db_oid>/
│       ├── <table_oid>        (heap file, 1GB segments)
│       ├── <table_oid>_fsm    (free space map)
│       ├── <table_oid>_vm     (visibility map)
│       └── <index_oid>        (index file)
├── pg_wal/                    (write-ahead log)
├── pg_xact/                   (transaction commit log)
└── postgresql.conf
```

PostgreSQL separates heap files (for table rows) from index files. The heap is an unordered collection of 8KB pages. Rows ("tuples") are inserted anywhere there is free space. Indexes are separate B-tree files that point back into the heap. This separation is fundamental to PostgreSQL's MVCC design.

### 3.3 Page Layout

**SQLite B-tree page (4KB default):**
```
[Page Header: 8-12 bytes]
[Cell Pointer Array: 2 bytes per cell, grows downward from header]
[Unallocated space in the middle]
[Cell content: grows upward from end of page]
```
Each cell on a leaf page contains: the rowid + the full row data. SQLite tables ARE B-trees.

**PostgreSQL heap page (8KB):**
```
[PageHeaderData: 24 bytes - LSN, checksum, flags, free space pointers]
[ItemIdData array: 4 bytes per item, line pointer array]
[Free space]
[Tuple data (grows from end)]
[Special space: 0 bytes for heap, used by indexes]
```
Each tuple in a heap page has its own header carrying `xmin`, `xmax`, `ctid` (physical location), infomask flags — critical metadata for MVCC.

### 3.4 Index Implementation

Both systems use B+ trees for indexes, but the implementation differs.

**SQLite**: Table rows live inside the B-tree (like InnoDB's clustered index). Secondary indexes store the rowid and a copy of the indexed column. Lookups go: secondary index → rowid → table B-tree root-to-leaf traversal.

**PostgreSQL**: B-tree indexes store index keys + `ctid` (page number + slot within page). A lookup goes: index → ctid → direct heap page fetch. This is very efficient for point lookups but means index-only scans need the visibility map to verify tuple visibility without touching the heap.

PostgreSQL also supports index types SQLite doesn't: Hash, GiST, SP-GiST, GIN, BRIN. This extensibility is a major architectural feature.

### 3.5 Transaction Management & Concurrency

This is where the architectures diverge most dramatically.

**SQLite Locking (WAL mode off):**
```
UNLOCKED → SHARED (read) → RESERVED (intent to write)
         → PENDING (blocks new readers) → EXCLUSIVE (write)
```
Only one writer at a time. Readers block writers, writers block readers (in older journal mode). SQLite added WAL mode in 2010 which allows one writer + multiple concurrent readers, but still only one concurrent writer ever.

**PostgreSQL MVCC (Multi-Version Concurrency Control):**

PostgreSQL never overwrites tuples. When a row is updated:
1. The old tuple is marked with `xmax = current_txn_id` (logically deleted)
2. A new tuple is inserted with `xmin = current_txn_id`

```
Heap Page (simplified):
┌─────────────────────────────────────────┐
│ Tuple v1: xmin=100, xmax=200, data="A" │  ← visible to txn < 200
│ Tuple v2: xmin=200, xmax=0,   data="B" │  ← visible to txn >= 200
└─────────────────────────────────────────┘
```

Each transaction gets a **snapshot** at start time. It can see any tuple where `xmin ≤ snapshot_xmin AND xmax = 0 OR xmax > snapshot_xmax`. Readers never block writers and writers never block readers. This is the fundamental MVCC guarantee.

The cost: old tuple versions accumulate. **VACUUM** is the background process that reclaims these dead tuples. This is unique to PostgreSQL's model — SQLite has no equivalent because it doesn't do multi-version heap storage.

### 3.6 Durability: WAL / Journaling

**SQLite Journal Mode (default rollback journal):**
Before modifying a page, SQLite copies the original page to a journal file. If a crash occurs, it replays the journal to restore the original. On commit, the journal is deleted.

**SQLite WAL Mode:**
Instead of a journal, changes go to a WAL file. Readers read from the main db file + WAL. A background checkpoint process eventually writes WAL changes back to the main file. Better concurrency, but the WAL must be checkpointed periodically.

**PostgreSQL WAL:**
Every change is first written to the WAL (pg_wal/) before modifying the actual heap/index pages. On commit, the WAL record is flushed to disk (fsync). On crash, PostgreSQL replays WAL records from the last checkpoint to bring the database to a consistent state. WAL also enables:
- Point-in-time recovery (PITR)
- Streaming replication (ship WAL to standby servers)
- Logical replication

This is significantly more powerful than SQLite's journaling.

---

## 4. Design Trade-offs

### SQLite Trade-offs

**Advantages:**
- Zero configuration — works out of the box
- Serverless — no network hop, no IPC — extremely fast for single-user workloads
- Single file — easy backup (just copy the file), easy deployment
- Portable — identical behavior across platforms
- Great for testing — spin up an in-memory database in milliseconds
- Surprisingly high throughput for read-heavy or serialized-write workloads

**Limitations:**
- Write concurrency is fundamentally limited (one writer at a time)
- No network access — can't be shared across machines without extra tooling
- No user/role management, no fine-grained permissions
- Limited SQL features (e.g., no FULL OUTER JOIN until recently, no RIGHT JOIN)
- Not suitable for high-concurrency write workloads
- File locking issues over network filesystems (NFS, Samba) are notorious

**The core trade-off:** SQLite sacrifices concurrency and networked access to gain simplicity and deployability. This is the right trade-off for embedded/mobile/local use.

### PostgreSQL Trade-offs

**Advantages:**
- True multi-user concurrency via MVCC — readers never block writers
- Network-accessible — can serve many applications across machines
- Rich feature set: foreign keys, triggers, stored procedures, CTEs, window functions, custom types, extensions
- Horizontal read scaling via streaming replication
- VACUUM allows reclamation of dead tuple space
- WAL enables point-in-time recovery and replication

**Limitations:**
- Requires a running server process — operational overhead
- VACUUM must run regularly — table bloat is a real operational concern
- Table bloat from dead tuples if VACUUM can't keep up
- Connection overhead — each connection forks a process; connection poolers (PgBouncer) are essentially mandatory at scale
- More complex to configure correctly (shared_buffers, work_mem, autovacuum tuning, etc.)
- Overkill for simple single-user applications

**The core trade-off:** PostgreSQL sacrifices simplicity and zero-config deployability to gain concurrency, scalability, and rich features. This is the right trade-off for production multi-user systems.

### Head-to-Head Comparison

| Scenario | Winner | Reason |
|---|---|---|
| Mobile app local storage | SQLite | No server needed, single file, offline-first |
| Web app with 100+ concurrent users | PostgreSQL | MVCC, row-level locking, network access |
| Unit testing with a DB | SQLite | In-memory mode, zero setup |
| Financial system with complex transactions | PostgreSQL | SERIALIZABLE isolation, row-level locks |
| Config storage in desktop app | SQLite | Embedded, no admin overhead |
| Read replica scaling | PostgreSQL | WAL streaming replication |
| IoT sensor data collection | SQLite | Works offline, embedded in device |
| Multi-tenant SaaS platform | PostgreSQL | Schema isolation, role management |

---

## 5. Experiments / Observations

### Observation 1: SQLite's Single-Writer Bottleneck

Running concurrent write threads against SQLite (default journal mode):
```python
# Thread 1 and Thread 2 both do:
conn.execute("INSERT INTO events VALUES (?)", (data,))
```
Result: `sqlite3.OperationalError: database is locked`

SQLite returns this error when a writer tries to acquire an EXCLUSIVE lock while another writer holds it. Applications must handle retries. PostgreSQL's row-level locking means concurrent writers to different rows proceed without blocking.

### Observation 2: PostgreSQL EXPLAIN ANALYZE

```sql
EXPLAIN ANALYZE SELECT * FROM orders o JOIN customers c ON o.customer_id = c.id
WHERE c.country = 'IN';
```

Typical output shows:
- **Hash Join** chosen when one table fits in `work_mem`
- **Seq Scan** on `customers` with filter (if no index on `country`)
- **Index Scan** on `orders` using FK index

The planner uses `pg_statistic` (populated by ANALYZE) to estimate row counts. If statistics are stale, the planner may choose a bad plan. SQLite's query planner is much simpler — it lacks a cost-based optimizer of this sophistication.

### Observation 3: Dead Tuple Accumulation

Running repeated UPDATEs on a PostgreSQL table without VACUUM:
```sql
UPDATE accounts SET balance = balance + 1 WHERE id = 1;
-- Repeated 10,000 times
```
Each UPDATE inserts a new tuple version. `pgstattuple` will show thousands of dead tuples consuming space. Running `VACUUM accounts` reclaims them. SQLite's in-place update model (via the journal) doesn't suffer from this — updates overwrite rows directly.

---

## 6. Key Learnings

**Architecture follows purpose.** SQLite's "one file, no server" design isn't a limitation — it's the entire point. Every constraint (single writer, no network, no roles) is a consequence of a deliberate decision to eliminate operational complexity. PostgreSQL's complexity (VACUUM, connection pooling, WAL tuning) is the cost of its power.

**MVCC is not free.** PostgreSQL's elegant "readers never block writers" guarantee comes at the cost of dead tuple accumulation, VACUUM overhead, and table bloat. Understanding this helps explain why VACUUM tuning is a real DBA skill and why very high UPDATE workloads can surprise engineers coming from other databases.

**The embedded vs client-server distinction matters operationally.** SQLite requires no devops. PostgreSQL requires monitoring, backup strategies (WAL archiving), connection pool management, and autovacuum tuning. The right choice depends entirely on operational context.

**File format as API.** SQLite's single-file format is so stable that it's recommended as an archival format. PostgreSQL's directory structure is an implementation detail — you never directly manipulate it. This reflects different philosophical priorities: SQLite optimizes for data portability, PostgreSQL for operational power.

**Write concurrency is the clearest differentiator.** If your application has >1 concurrent writer, PostgreSQL's MVCC model is architecturally superior. If it doesn't, SQLite's simplicity wins on almost every other dimension.

---

*References: SQLite documentation (sqlite.org), PostgreSQL documentation (postgresql.org), "SQLite File Format" spec, PostgreSQL source code (github.com/postgres/postgres)*
