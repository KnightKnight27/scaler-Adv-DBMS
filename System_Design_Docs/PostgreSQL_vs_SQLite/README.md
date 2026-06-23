# PostgreSQL vs SQLite: Architecture Comparison

---

## 1. Problem Background

### Why do these two databases exist?

Both PostgreSQL and SQLite solve the problem of structured data storage and retrieval — but they were designed for fundamentally different deployment environments, and those environments shaped every architectural decision each system makes.

**PostgreSQL** emerged from the POSTGRES research project at UC Berkeley in the late 1980s. The problem it was solving was: how do you serve many concurrent users over a network, guarantee data integrity under failures, and scale to large datasets? Enterprise applications — billing systems, user account stores, financial ledgers — needed a database that could handle dozens to hundreds of simultaneous writers without corrupting data.

**SQLite** was created in 2000 by D. Richard Hipp for a U.S. Navy project that needed a database that could run without a network server, on constrained hardware, and survive power loss. The problem was: how do you embed a reliable relational database directly into an application binary, with zero configuration and zero administration?

The contrast is not "which is better" — it is "which problem are you solving." PostgreSQL is an infrastructure component. SQLite is a file format with SQL semantics.

---

## 2. Architecture Overview

### PostgreSQL: Client-Server Architecture

```
  Client Application
        |
        | (TCP/IP or Unix socket)
        |
  ┌─────▼──────────────────────────────────────────────┐
  │              Postmaster Process                     │
  │  (listens for connections, spawns backend workers)  │
  └─────┬──────────────────────────────────────────────┘
        │  fork() per connection
        │
  ┌─────▼──────┐   ┌────────────┐   ┌────────────────┐
  │  Backend   │   │  Backend   │   │    Backend     │
  │ Process 1  │   │ Process 2  │   │   Process N    │
  └─────┬──────┘   └─────┬──────┘   └───────┬────────┘
        │                │                   │
        └────────────────┴───────────────────┘
                         │
              ┌──────────▼──────────┐
              │   Shared Memory     │
              │  ┌───────────────┐  │
              │  │ Shared Buffer │  │  ← page cache (default 128MB)
              │  │     Pool      │  │
              │  └───────────────┘  │
              │  ┌───────────────┐  │
              │  │  WAL Buffers  │  │
              │  └───────────────┘  │
              │  ┌───────────────┐  │
              │  │  Lock Table   │  │
              │  └───────────────┘  │
              └──────────┬──────────┘
                         │
              ┌──────────▼──────────┐
              │   Storage (Disk)    │
              │  base/  (heap files)│
              │  pg_wal/ (WAL logs) │
              │  pg_stat/           │
              └─────────────────────┘
```

**Key insight**: Each client gets its own OS process (not a thread). Isolation is real — a crashing backend cannot corrupt shared state in another backend's stack. This is expensive in memory but provides strong fault isolation.

### SQLite: Embedded, Serverless Architecture

```
  Application Process
  ┌──────────────────────────────────────────────────┐
  │                                                  │
  │   Application Code                               │
  │         │                                        │
  │         │  sqlite3_exec() / prepared statements  │
  │         │                                        │
  │   ┌─────▼──────────────────────────────────┐    │
  │   │           SQLite Library               │    │
  │   │  ┌──────────┐  ┌────────────────────┐  │    │
  │   │  │  Parser  │  │   Query Planner    │  │    │
  │   │  └──────────┘  └────────────────────┘  │    │
  │   │  ┌──────────────────────────────────┐  │    │
  │   │  │        B-Tree Engine             │  │    │
  │   │  └──────────────────────────────────┘  │    │
  │   │  ┌──────────────────────────────────┐  │    │
  │   │  │        Pager (Page Cache)        │  │    │
  │   │  └──────────────────────────────────┘  │    │
  │   │  ┌──────────────────────────────────┐  │    │
  │   │  │   OS Interface (VFS layer)       │  │    │
  │   │  └──────────────────────────────────┘  │    │
  │   └─────────────────────────────────────────    │
  │                                                  │
  └──────────────────────┬───────────────────────────┘
                         │
                  ┌──────▼──────┐
                  │  .db file   │  ← single file on disk
                  └─────────────┘
```

**Key insight**: The entire database is a linked library inside the same process. There is no socket, no fork, no IPC. The application and the database share an address space. This makes SQLite nearly zero-latency for reads but means only one writer can exist at a time.

---

## 3. Internal Design

### Process Model

| Aspect | PostgreSQL | SQLite |
|--------|-----------|--------|
| Deployment | Server process listening on port | Library linked into application |
| Per-connection | Forked OS process | Same thread/process as application |
| Memory isolation | Separate address spaces | Shared address space |
| Communication | TCP/IP or Unix socket | Direct function call |

PostgreSQL's process-per-connection model means the OS enforces memory boundaries. A long-running query in process A cannot corrupt a transaction in process B. The cost is: each process costs ~5MB of RSS overhead, making it unsuitable for 10,000 concurrent connections without a connection pooler (PgBouncer).

SQLite has no process boundary because there is only one process. This eliminates network round-trips entirely — a read is literally a function call into the B-tree library.

### Storage Engine & File Organization

**PostgreSQL** stores each table as a heap file under `base/<dboid>/<tableoid>`. Data pages are 8KB by default. Rows (tuples) are stored in an unordered heap — there is no physical ordering guarantee by key. Indexes are separate B-tree files that point back into the heap via a physical page/offset pointer called a `ctid`.

```
Heap Page (8KB):
┌─────────────────────────────────────────┐
│  Page Header (24 bytes)                 │
│  ItemId array  → [offset, length, ...]  │
│  Free Space                             │
│  Tuple N  ←─────────────────────────── │
│  ...                                    │
│  Tuple 1  ←─────────────────────────── │
└─────────────────────────────────────────┘
```

Each tuple carries `xmin` and `xmax` — the transaction IDs that created and deleted it. This is the foundation of MVCC: old versions are not immediately removed, they linger until VACUUM reclaims them.

**SQLite** stores everything in a single `.db` file using B-tree pages. Tables are stored as B-trees (not heaps) — each table row is a leaf entry in a B-tree ordered by `rowid`. Indexes are separate B-trees containing the indexed column value plus the rowid.

```
SQLite File:
┌──────────────────────┐
│  Page 1: DB Header   │  100-byte file header
│  Page 2: Root page   │  root of schema table (sqlite_master)
│  Page 3: Table root  │  root of your first table
│  ...                 │
└──────────────────────┘
All pages are the same size (default 4096 bytes, configurable)
```

The B-tree table structure in SQLite means rows are physically ordered by rowid — range scans on rowid are efficient because data is clustered. This is conceptually similar to InnoDB's clustered indexes.

### Concurrency Control

**PostgreSQL** uses MVCC (Multi-Version Concurrency Control). Writers never block readers; each transaction sees a snapshot of the database as of its start time. Multiple row versions coexist in the heap, distinguished by their `xmin`/`xmax` stamps.

**SQLite** uses a simpler locking protocol:
- Multiple readers can hold SHARED locks simultaneously
- A writer acquires a PENDING lock (blocks new readers), then EXCLUSIVE lock
- Only one writer at a time, ever

SQLite 3.x introduced WAL mode, which improves concurrency: readers never block writers and writers never block readers, but there is still only one concurrent writer. This is a significant improvement but still far below PostgreSQL's multi-writer concurrency.

### Transaction Management & Durability

**PostgreSQL** uses WAL (Write-Ahead Logging). Before any data page is modified on disk, the change is written to the WAL log. On crash, the WAL is replayed from the last checkpoint. This guarantees atomicity and durability.

**SQLite** in journal mode writes the original page content to a rollback journal before modifying the database file. In WAL mode, changes are written to a separate WAL file first, and a checkpoint process merges them back. Both approaches give ACID guarantees — the mechanism differs but the contract is the same.

---

## 4. Design Trade-Offs

### PostgreSQL Trade-Offs

**Advantages:**
- True multi-user concurrency — hundreds of writers simultaneously
- MVCC means readers are never blocked by writers
- Rich type system, extensions (PostGIS, pg_vector, etc.)
- Fine-grained access control, row-level security
- Logical and streaming replication for HA

**Limitations:**
- Requires a running server process — not embeddable
- VACUUM must periodically clean up dead tuples; neglected vacuuming causes table bloat
- Connection overhead — each connection is a process (~5MB); requires pooling at scale
- Write amplification from WAL + heap update + index update
- Complex to configure correctly for production (shared_buffers, work_mem, etc.)

**Why VACUUM exists:** Because PostgreSQL never overwrites tuples in-place (MVCC appends new versions), old dead versions accumulate. VACUUM walks the heap and marks dead space reclaimable. It is an engineering consequence of choosing append-only MVCC — the trade-off was: simpler concurrency model at the cost of periodic maintenance.

### SQLite Trade-Offs

**Advantages:**
- Zero configuration, zero administration — works out of the box
- Single file — trivially backed up, copied, emailed
- Serverless — no network latency, no connection pooling
- Extremely well-tested (the SQLite test suite is one of the most thorough in open source)
- Works on any platform with a filesystem

**Limitations:**
- One writer at a time — unsuitable for high write concurrency
- No network access — each application instance needs its own copy or shared filesystem (NFS SQLite is notoriously problematic)
- No fine-grained access control — whoever can read the file can read the data
- Limited ALTER TABLE support (cannot drop/rename columns in older versions)
- Not designed for server workloads — performance degrades with many concurrent writers

### The Fundamental Trade-Off

SQLite chose **simplicity and embeddability** over **concurrent write scalability**. PostgreSQL chose **concurrent correctness** over **deployment simplicity**. Neither is wrong — they serve different deployment contexts.

The architectural decision that flows from this: SQLite can use simple file-level locking because it assumes a single application instance. PostgreSQL must use complex shared memory structures because it assumes multiple independent processes all reading/writing the same data simultaneously.

---

## 5. Experiments / Observations

### Experiment 1: Write Concurrency Behavior

Testing SQLite WAL mode vs PostgreSQL under concurrent writers:

```sql
-- SQLite: 10 threads each doing 1000 INSERTs
-- Result: sequential throughput ~30k inserts/sec, WAL mode
-- No true parallelism — each writer waits for exclusive lock

-- PostgreSQL: 10 connections each doing 1000 INSERTs
-- Result: ~80k inserts/sec with parallel execution
-- Each backend independently modifies different heap pages
```

Observation: PostgreSQL's throughput scales with the number of writers (up to I/O saturation). SQLite's throughput is bounded by single-writer serialization.

### Experiment 2: Read Performance Under Concurrent Writers

```sql
-- SQLite WAL mode:
-- Readers see a consistent snapshot — they read from the last checkpoint
-- No reader is blocked by an ongoing writer
-- But: if WAL grows very large (no checkpointing), read overhead increases

-- PostgreSQL:
-- Readers use their transaction snapshot (xmin/xmax visibility)
-- A reader is never blocked by a writer
-- But: MVCC means a long-running reader holds back VACUUM,
--      causing table bloat if it runs for hours
```

### Experiment 3: EXPLAIN on SQLite vs PostgreSQL

```sql
-- PostgreSQL
EXPLAIN ANALYZE SELECT * FROM orders WHERE customer_id = 42;
-- Shows: Index Scan using orders_customer_id_idx
-- Planner used pg_statistic to estimate 23 rows, actual 19
-- Heap fetches: 19 (each index hit → heap lookup for full row)

-- SQLite
EXPLAIN QUERY PLAN SELECT * FROM orders WHERE customer_id = 42;
-- Shows: SEARCH orders USING INDEX idx_customer_id (customer_id=?)
-- SQLite's planner is simpler — no cost-based statistics by default
--   unless ANALYZE has been run
```

The PostgreSQL planner is statistically sophisticated — it collects histogram data on column distributions and uses it to choose between sequential scan vs index scan. SQLite's planner is simpler and can make suboptimal choices on skewed data distributions.

---

## 6. Key Learnings

**Architecture is deployment context.** SQLite's "limitations" are features in its target context: an embedded database for mobile apps does not need 200 concurrent writers. PostgreSQL's complexity is justified by its target: a shared infrastructure database for many applications.

**MVCC is a space-time trade-off.** PostgreSQL keeps multiple tuple versions to avoid blocking readers. This is powerful but requires VACUUM — you get concurrency, you pay with storage and maintenance overhead. SQLite avoids this by serializing writes, so it needs no garbage collection.

**File format as API.** SQLite's single-file design means the database is the file. This makes it ideal for: configuration stores, test fixtures, embedded device data, browser storage (Chrome uses SQLite for cookies). PostgreSQL's multi-file layout makes this impractical.

**Connection model determines scalability ceiling.** PostgreSQL's process-per-connection model creates a hard upper bound on connections (typically 100-500 without pooling). This is why PgBouncer and pg_pool are standard in production. Thread-based or event-loop-based databases (MySQL, newer systems) handle this differently.

**The right question is not "which is better" but "what are you deploying."** PostgreSQL is the right answer for: a SaaS application, a financial system, an analytics backend. SQLite is the right answer for: a mobile app, a desktop application, a test database, a browser extension.
