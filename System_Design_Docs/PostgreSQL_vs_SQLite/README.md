# PostgreSQL vs SQLite: Architecture Comparison

## 1. Problem Background

### Why Do Two SQL Databases Exist with Radically Different Architectures?

PostgreSQL and SQLite both implement the SQL standard and provide ACID-compliant transactional storage — yet they occupy entirely different points in the database design space. Understanding **why** requires looking at the problems each was designed to solve.

**PostgreSQL** originated from the POSTGRES project at UC Berkeley (1986), led by Michael Stonebraker. The goal was to build an extensible, enterprise-grade relational database that could handle complex queries, concurrent users, and large datasets. It evolved through Postgres95 (adding SQL support) into the modern PostgreSQL we know today. The design priority was **correctness, extensibility, and multi-user concurrency** — even at the cost of operational complexity.

**SQLite** was created by D. Richard Hipp in 2000, originally for the US Navy's guided missile destroyer program. The requirement was a database engine that could run reliably **without a DBA, without configuration, and without a separate server process**. The design priority was **simplicity, zero-administration, and reliability in embedded contexts**.

These opposing design goals — "serve many concurrent users across a network" vs. "run reliably inside a single application" — are what drive every architectural difference between them.

### The Core Tension

| Design Goal | PostgreSQL's Answer | SQLite's Answer |
|---|---|---|
| Multi-user access | Client-server with process-per-connection | Single-writer with file-level locking |
| Deployment | Dedicated server installation | Linked as a library (~600KB) |
| Data integrity | WAL + MVCC + crash recovery | Journaling + atomic commit |
| Scalability | Horizontal/vertical scaling | Bounded by single process |
| Administration | Requires DBA expertise | Zero configuration |

---

## 2. Architecture Overview

### PostgreSQL: Client-Server Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Client Applications                      │
│            (psql, pgAdmin, application code)                 │
└──────────┬──────────┬──────────┬───────────────────────────────┘
           │          │          │    TCP/IP or Unix Socket
           ▼          ▼          ▼
┌─────────────────────────────────────────────────────────────┐
│                      Postmaster                              │
│              (Main daemon / connection listener)              │
│                                                              │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                   │
│  │ Backend  │  │ Backend  │  │ Backend  │  (forked per       │
│  │ Process 1│  │ Process 2│  │ Process 3│   connection)      │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘                   │
│       │              │              │                         │
│  ┌────▼──────────────▼──────────────▼────┐                   │
│  │          Shared Memory                 │                   │
│  │  ┌─────────────┐ ┌────────────────┐   │                   │
│  │  │Shared Buffer│ │  WAL Buffers   │   │                   │
│  │  │    Pool     │ │                │   │                   │
│  │  └─────────────┘ └────────────────┘   │                   │
│  │  ┌─────────────┐ ┌────────────────┐   │                   │
│  │  │  Lock Table │ │  Proc Array    │   │                   │
│  │  └─────────────┘ └────────────────┘   │                   │
│  └───────────────────────────────────────┘                   │
│                                                              │
│  Background Workers:                                         │
│  ┌───────────┐ ┌──────────┐ ┌───────────┐ ┌──────────────┐  │
│  │ WAL Writer│ │Checkpointr│ │ Autovacuum│ │ BG Writer    │  │
│  └───────────┘ └──────────┘ └───────────┘ └──────────────┘  │
└──────────┬──────────────────────────────────────────────────┘
           │
           ▼
┌─────────────────────────────────────────────────────────────┐
│                      Disk Storage                            │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐    │
│  │ Data Dir │  │ WAL Logs │  │ CLOG     │  │ pg_stat  │    │
│  │(base/)   │  │(pg_wal/) │  │(pg_xact/)│  │          │    │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘    │
└─────────────────────────────────────────────────────────────┘
```

**Why this architecture?** PostgreSQL uses a **process-per-connection** model (not threaded). Each client gets a dedicated OS process via `fork()`. This provides:
- **Fault isolation**: A crash in one backend doesn't bring down the server
- **OS-level scheduling**: The kernel handles process scheduling
- **Simplicity of shared state**: Shared memory is used for cross-process communication (buffer pool, lock tables)

The trade-off is **higher memory overhead per connection** (~5-10MB per backend), which is why connection poolers like PgBouncer exist.

### SQLite: Embedded Library Architecture

```
┌────────────────────────────────────────────────┐
│              Application Process               │
│                                                │
│  ┌──────────────────────────────────────────┐  │
│  │            Application Code              │  │
│  └──────────────┬───────────────────────────┘  │
│                 │  Function calls (in-process)  │
│  ┌──────────────▼───────────────────────────┐  │
│  │          SQLite Library                   │  │
│  │                                           │  │
│  │  ┌────────────┐    ┌──────────────────┐   │  │
│  │  │   SQL       │    │   Virtual        │   │  │
│  │  │  Compiler   │───▶│   Machine (VDBE) │   │  │
│  │  │  (Parser +  │    │   (Bytecode      │   │  │
│  │  │   Planner)  │    │    Execution)    │   │  │
│  │  └────────────┘    └───────┬──────────┘   │  │
│  │                            │               │  │
│  │  ┌─────────────────────────▼──────────┐   │  │
│  │  │           B-Tree Layer              │   │  │
│  │  │  (Table B-Trees + Index B-Trees)    │   │  │
│  │  └─────────────────────────┬──────────┘   │  │
│  │                            │               │  │
│  │  ┌─────────────────────────▼──────────┐   │  │
│  │  │            Pager                    │   │  │
│  │  │  (Page cache + Transaction mgmt)    │   │  │
│  │  └─────────────────────────┬──────────┘   │  │
│  │                            │               │  │
│  │  ┌─────────────────────────▼──────────┐   │  │
│  │  │        OS Interface (VFS)           │   │  │
│  │  │  (File I/O + Locking abstraction)   │   │  │
│  │  └────────────────────────────────────┘   │  │
│  └───────────────────────────────────────────┘  │
│                 │                                │
└─────────────────┼────────────────────────────────┘
                  │  File I/O (read/write/fsync)
                  ▼
         ┌────────────────┐
         │  Single DB File │  (+ journal or WAL file)
         │  (database.db)  │
         └────────────────┘
```

**Why this architecture?** SQLite is a **library, not a server**. The entire database engine runs inside the application's process. There is no IPC, no network protocol, no authentication layer. This means:
- **Zero latency for queries**: Function call overhead only, no serialization/deserialization
- **Zero administration**: No daemon to manage, no ports to configure
- **Single-file database**: The entire database (schema + data + indexes) is one cross-platform file

The trade-off is **limited concurrency** — there's no server process to coordinate multiple writers.

---

## 3. Internal Design

### 3.1 Storage Engine Architecture

#### PostgreSQL: Heap-Based Storage

PostgreSQL stores table data in **heap files**. Each table is stored as a collection of 8KB pages (default).

**Page Layout (8KB):**
```
┌──────────────────────────────────────────┐
│  Page Header (24 bytes)                  │
│  - pd_lsn (WAL position for recovery)   │
│  - pd_checksum                           │
│  - pd_lower (free space start)           │
│  - pd_upper (free space end)             │
│  - pd_special (special space start)      │
├──────────────────────────────────────────┤
│  Item Pointers (Line Pointer Array)      │
│  [lp1] [lp2] [lp3] ... [lpN]            │
│  (4 bytes each, grows downward)          │
├──────────────────────────────────────────┤
│              Free Space                  │
│         (pd_lower → pd_upper)            │
├──────────────────────────────────────────┤
│  Tuple Data (Heap Tuples)                │
│  (grows upward from bottom of page)      │
│  ┌──────────────────────────────────┐    │
│  │ HeapTupleHeader                  │    │
│  │  - t_xmin (inserting txn ID)     │    │
│  │  - t_xmax (deleting txn ID)      │    │
│  │  - t_ctid (current tuple ID)     │    │
│  │  - t_infomask (status flags)     │    │
│  ├──────────────────────────────────┤    │
│  │ Tuple Data (column values)       │    │
│  └──────────────────────────────────┘    │
├──────────────────────────────────────────┤
│  Special Space (used by indexes)         │
└──────────────────────────────────────────┘
```

**Key Design Decision: Why Heap + Separate Indexes?**

PostgreSQL deliberately separates data storage (heap) from index structures. This means:
- The heap stores rows in **insertion order** (no particular sort order)
- Indexes contain pointers (TID = block number + offset) back to the heap
- A secondary index lookup requires an **additional heap fetch** to get the actual row

This is different from MySQL/InnoDB's clustered index approach. PostgreSQL chose this because:
1. **Updates don't require index key changes** when non-indexed columns are modified (except for HOT updates that can skip index updates entirely)
2. **Multiple indexes are all equal** — there's no "primary" vs "secondary" performance difference at the storage level
3. **MVCC is simpler** — old tuple versions live alongside new ones in the heap

#### SQLite: B-Tree Centric Storage

SQLite takes a fundamentally different approach: **everything is a B-tree**.

```
Database File Layout:
┌──────────────────────────────────────────┐
│  Page 1: File Header + Schema Table      │
│  (sqlite_master B-tree root)             │
├──────────────────────────────────────────┤
│  Page 2..N: Table B-Trees               │
│  (Each table = one B-tree, keyed by      │
│   rowid / INTEGER PRIMARY KEY)           │
├──────────────────────────────────────────┤
│  Page N+1..M: Index B-Trees             │
│  (Each index = separate B-tree)          │
├──────────────────────────────────────────┤
│  Free pages (freelist)                   │
└──────────────────────────────────────────┘
```

**SQLite Page Layout (default 4KB):**
```
┌──────────────────────────────────────┐
│  Page Header (8 or 12 bytes)         │
│  - Page type (leaf/interior/overflow) │
│  - First free block offset           │
│  - Number of cells                   │
│  - Cell content area offset          │
│  - Fragmented free bytes             │
├──────────────────────────────────────┤
│  Cell Pointer Array                  │
│  (2 bytes per cell, sorted order)    │
├──────────────────────────────────────┤
│         Unallocated Space            │
├──────────────────────────────────────┤
│  Cell Content Area                   │
│  (cells stored in no particular      │
│   order — pointers maintain sort)    │
├──────────────────────────────────────┤
│  Reserved Space (optional)           │
└──────────────────────────────────────┘
```

**Critical Difference**: In SQLite, a **table B-tree** stores the row data directly in its leaf nodes (keyed by `rowid`). This is conceptually similar to InnoDB's clustered index. Lookups by `rowid` are a single B-tree traversal — no separate heap fetch.

### 3.2 Index Implementation

| Aspect | PostgreSQL | SQLite |
|---|---|---|
| Primary structure | B-tree (default), also supports Hash, GiST, GIN, BRIN, SP-GiST | B-tree only |
| Table data location | Heap (separate from index) | In leaf nodes of table B-tree |
| Index entry | (key, TID) pointing to heap | (key, rowid) for index B-trees |
| Covering index | Possible via `INCLUDE` clause (Index-Only Scans) | Automatic for table B-tree |
| Multi-column support | Yes, up to 32 columns | Yes |
| Expression indexes | Yes | Yes (via computed columns) |

**Why PostgreSQL supports so many index types**: Different query patterns benefit from different data structures. GIN indexes excel at full-text search and array containment. GiST handles geometric/spatial queries. BRIN is extremely efficient for naturally-ordered data (like timestamps in append-only tables). This extensibility reflects PostgreSQL's design philosophy of being a general-purpose, extensible database.

**Why SQLite only needs B-trees**: SQLite targets embedded use cases where datasets are typically smaller and query patterns are simpler. A single, well-implemented B-tree covers the vast majority of use cases without the complexity of maintaining multiple index types.

### 3.3 Transaction Management & Concurrency Control

#### PostgreSQL: MVCC with Snapshot Isolation

PostgreSQL implements **Multi-Version Concurrency Control (MVCC)** by keeping multiple physical versions of each row in the heap.

```
UPDATE operation in PostgreSQL:

Before UPDATE:                    After UPDATE:
┌────────────────────┐           ┌────────────────────┐
│ Tuple v1           │           │ Tuple v1           │
│ xmin=100, xmax=0   │           │ xmin=100, xmax=200 │ ← marked as "deleted by txn 200"
│ data: name='Alice' │           │ data: name='Alice' │
└────────────────────┘           ├────────────────────┤
                                 │ Tuple v2 (NEW)     │
                                 │ xmin=200, xmax=0   │
                                 │ data: name='Bob'   │ ← new version appended
                                 └────────────────────┘
```

**Visibility Rules**: Each transaction takes a **snapshot** of active transactions. When reading a tuple:
1. Is `xmin` committed and started before my snapshot? → Tuple **may** be visible
2. Is `xmax` not set, or set by an uncommitted/future transaction? → Tuple **is** visible
3. Otherwise → Tuple is **invisible** (deleted or not yet committed)

**Consequence: VACUUM is mandatory**. Dead tuples (old versions no longer visible to any transaction) accumulate and must be cleaned up. Without VACUUM:
- Table and index bloat grows unbounded
- Sequential scans slow down (scanning dead tuples)
- Transaction ID wraparound risk (32-bit XID space)

#### SQLite: File-Level Locking with Journal/WAL

SQLite's concurrency model is much simpler, reflecting its single-process design.

**Rollback Journal Mode (default):**
```
Write Transaction:
1. Acquire SHARED lock
2. Read data (other readers allowed)
3. Acquire RESERVED lock (other readers still allowed, no other writers)
4. Copy original pages to rollback journal
5. Modify pages in-place in the database file
6. Acquire EXCLUSIVE lock (no readers, no writers)
7. Flush changes to disk
8. Delete journal file (= commit)
9. Release all locks

Crash Recovery:
- If journal file exists on startup → rollback by copying journal pages back
```

**WAL Mode (Write-Ahead Logging):**
```
┌──────────┐     ┌──────────┐     ┌──────────┐
│ Database │     │ WAL File │     │ WAL Index│
│   File   │◄────│ (append- │     │ (shm)   │
│ (original│     │  only    │     │          │
│  pages)  │     │  log)    │     │          │
└──────────┘     └──────────┘     └──────────┘

Writers: append new page versions to WAL
Readers: check WAL first (via WAL index), fall back to DB file
Checkpoint: copy WAL pages back to database file
```

**WAL mode advantages:**
- Readers and writers can proceed concurrently (readers see the last committed state)
- Writes are sequential (appending to WAL is faster than random I/O)
- Still only one writer at a time

**Key Concurrency Comparison:**

| Aspect | PostgreSQL | SQLite |
|---|---|---|
| Concurrent readers | Unlimited | Unlimited |
| Concurrent writers | Yes (row-level locks) | One at a time |
| Read blocks write? | No (MVCC) | No (WAL mode) / Yes (journal mode) |
| Write blocks read? | No (MVCC) | No (WAL mode) / Yes (journal mode) |
| Isolation levels | Read Committed, Repeatable Read, Serializable | Serializable (effectively) |
| Deadlock detection | Yes (wait-for graph) | N/A (single writer) |

### 3.4 Durability Mechanisms

**PostgreSQL WAL:**
- Every data modification is first written to the WAL (Write-Ahead Log)
- WAL is a sequential append-only log of physical page changes
- `fsync` ensures WAL records are durable before acknowledging commit
- Crash recovery replays WAL from last checkpoint
- WAL also enables replication (streaming replication sends WAL records)

**SQLite Journaling:**
- In journal mode: original pages are backed up before modification
- In WAL mode: new page versions are appended to the WAL
- Both ensure atomic commit — either all changes apply or none do
- `fsync` barriers are placed at critical points to prevent partial writes

---

## 4. Design Trade-Offs

### The Fundamental Trade-Off: Complexity vs. Simplicity

```
                    PostgreSQL                          SQLite
                    ──────────                          ──────
Complexity:         High                                Low
   - 1.4M+ lines of C code                  - ~150K lines of C code
   - Dozens of background processes          - Single library
   - Shared memory management                - File-level operations
   - Complex MVCC implementation             - Simple locking model

Capability:         Maximum                             Sufficient
   - Unlimited concurrent writers            - Single writer
   - Petabyte-scale databases                - ~281 TB theoretical max
   - Advanced indexing (GIN, GiST, BRIN)     - B-tree only
   - Extensible type system                  - Fixed type affinity
   - Stored procedures, triggers, views      - Basic trigger/view support
   - Full-text search, JSON, PostGIS         - JSON1, FTS5 extensions

Operational Cost:   Significant                         Near-zero
   - Requires DBA knowledge                 - No configuration needed
   - Tuning: shared_buffers, work_mem,       - PRAGMA settings only
     effective_cache_size, etc.
   - Monitoring: pg_stat_*, logs, alerts     - No monitoring needed
   - Backup: pg_dump, pg_basebackup, PITR   - Copy the file
   - Upgrades: pg_upgrade, logical repl.    - Just update the library
```

### When Each Design Choice Wins

**PostgreSQL excels when:**
- Multiple applications/users access the same database simultaneously
- Write throughput must scale beyond what a single writer can handle
- Complex queries involving multiple joins, CTEs, window functions are common
- Data integrity constraints (foreign keys, CHECK, exclusion constraints) are critical
- Extensions are needed (PostGIS for spatial, pg_trgm for fuzzy search, TimescaleDB for time-series)

**SQLite excels when:**
- The database is embedded within an application (mobile apps, desktop software, IoT devices)
- The dataset is moderate (< 1TB practically, though the limit is much higher)
- Simplicity and reliability are more important than concurrent write throughput
- Zero-downtime deployment is required (no server to restart)
- Edge computing or situations where a DBA is not available

### Common Misconception: "SQLite is a toy database"

SQLite processes more transactions per day than all other database engines combined (considering every Android phone, iOS device, web browser, and embedded system using it). It is one of the most **tested** pieces of software in existence — with 100% branch coverage and billions of test cases. The limitation is not reliability; it's concurrency model.

---

## 5. Experiments / Observations

### Experiment 1: Concurrent Write Performance

**Setup**: Insert 10,000 rows from 10 concurrent connections.

**PostgreSQL:**
```sql
-- Each connection runs:
INSERT INTO test_table (data) 
SELECT md5(random()::text) 
FROM generate_series(1, 1000);
```
**Expected Result**: All 10 connections complete simultaneously. PostgreSQL uses row-level locking, so concurrent inserts to the same table are non-blocking. Total time ≈ time for single-connection insert (near-linear scaling).

**SQLite (WAL mode):**
```sql
-- Each connection runs:
PRAGMA journal_mode=WAL;
INSERT INTO test_table (data) VALUES (?);  -- 1000 times in a loop
```
**Expected Result**: Connections serialize on write. One writer proceeds while others wait (returning `SQLITE_BUSY`). Total time ≈ 10× single-connection time. This is the fundamental concurrency limitation.

### Experiment 2: Query Plan Comparison

**PostgreSQL:**
```sql
EXPLAIN ANALYZE 
SELECT o.order_id, c.name, p.product_name
FROM orders o
JOIN customers c ON o.customer_id = c.id
JOIN products p ON o.product_id = p.id
WHERE o.order_date > '2024-01-01'
  AND c.region = 'US';
```

**Typical plan**: PostgreSQL's optimizer considers multiple strategies:
- Hash Join vs. Nested Loop vs. Merge Join
- Sequential Scan vs. Index Scan vs. Bitmap Index Scan
- Join ordering based on table statistics from `pg_statistic`

```
Hash Join  (cost=... rows=... actual time=...)
  Hash Cond: (o.customer_id = c.id)
  ->  Hash Join  (cost=... rows=...)
        Hash Cond: (o.product_id = p.id)
        ->  Index Scan using idx_orders_date on orders o
              Filter: (order_date > '2024-01-01')
        ->  Hash
              ->  Seq Scan on products p
  ->  Hash
        ->  Seq Scan on customers c
              Filter: (region = 'US')
Planning Time: 0.5ms
Execution Time: 12.3ms
```

**SQLite:**
```sql
EXPLAIN QUERY PLAN
SELECT o.order_id, c.name, p.product_name
FROM orders o
JOIN customers c ON o.customer_id = c.id
JOIN products p ON o.product_id = p.id
WHERE o.order_date > '2024-01-01'
  AND c.region = 'US';
```

SQLite's optimizer is simpler but effective for common query patterns. It uses a **next-generation query planner (NGQP)** that evaluates join orders but has fewer join strategies (primarily nested loop with index).

### Experiment 3: Storage Overhead Comparison

| Metric | PostgreSQL | SQLite |
|---|---|---|
| Minimum overhead per row | ~27 bytes (heap tuple header) | ~2-5 bytes (varint header in B-tree cell) |
| Page header size | 24 bytes per 8KB page | 8-12 bytes per 4KB page |
| MVCC overhead | Old row versions remain until VACUUM | None (in-place updates with journal) |
| Index overhead | Separate heap + index structures | Table B-tree stores data directly |

**Observation**: For a table with 1M small rows (10 bytes of actual data each), PostgreSQL's storage footprint can be 3-5× larger than SQLite's due to tuple headers and MVCC overhead.

---

## 6. Key Learnings

### 1. Architecture Follows Use Case
The single most important takeaway: PostgreSQL and SQLite are not competitors — they serve fundamentally different use cases. PostgreSQL is a **server**, SQLite is a **library**. Comparing them on raw performance misses the point; compare them on **fitness for purpose**.

### 2. Concurrency Models Have Cascading Effects
PostgreSQL's MVCC design enables concurrent readers and writers, but creates the need for VACUUM, tuple header overhead, and transaction ID management. SQLite's single-writer model eliminates all that complexity but caps write throughput. Every architectural decision creates downstream consequences.

### 3. The "Good Enough" Principle
SQLite's B-tree-only index strategy is "good enough" for its use cases. PostgreSQL's six index types reflect the demands of enterprise workloads. Neither is wrong — they're optimized for different points on the complexity/capability spectrum.

### 4. Storage Format Determines System Behavior
PostgreSQL's heap-based storage means updates create new tuples (append-only), which is great for MVCC but causes bloat. SQLite's B-tree storage with in-place updates (via journal) is more space-efficient but prevents concurrent writes. The storage format is not an implementation detail — it's the most consequential architectural decision.

### 5. Operational Complexity is a Valid Design Dimension
SQLite's "zero-admin" philosophy means an entire class of problems (configuration tuning, monitoring, backup strategies, upgrade procedures) simply doesn't exist. For many applications, this operational simplicity is more valuable than any performance advantage a server database might provide.

---

## References

1. PostgreSQL Documentation: https://www.postgresql.org/docs/current/
2. SQLite Documentation: https://www.sqlite.org/docs.html
3. "The Architecture of Open Source Applications: PostgreSQL" — https://aosabook.org/en/v1/postgresql.html
4. "SQLite: Past, Present, and Future" — VLDB 2022 Paper
5. PostgreSQL Source Code: `src/backend/storage/`, `src/backend/access/heap/`
6. SQLite Source Code: https://www.sqlite.org/src/
7. Michael Stonebraker, "The Design of POSTGRES" — 1986
8. D. Richard Hipp, "SQLite: A Global-Scale Database" — 2020 Talk
