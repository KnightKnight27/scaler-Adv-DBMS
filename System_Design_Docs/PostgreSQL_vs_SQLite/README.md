# PostgreSQL vs SQLite — Architecture Comparison

## 1. Problem Background

### Why Do Two Such Different Databases Exist?

Relational databases sit on a spectrum between **lightweight embeddability** and **enterprise-grade scalability**. PostgreSQL and SQLite occupy opposite ends of that spectrum, and neither is a "better" database — they are answers to fundamentally different questions.

**SQLite** was created by D. Richard Hipp in 2000 while working on a US Navy contract for guided-missile destroyers. The core problem: the existing Informix-based solution required a running database server, a DBA to maintain it, and would fail if the server process crashed. Hipp needed a database that **required zero administration, ran inside the application process, and stored everything in a single portable file**. The design goal was reliability in embedded, single-user, resource-constrained environments — a philosophy captured in SQLite's tagline: *"Small. Fast. Reliable. Choose any three."*

**PostgreSQL** traces its lineage to the POSTGRES project at UC Berkeley (1986), led by Michael Stonebraker. The problem it solved was different: how to build a **robust, extensible relational database** capable of handling concurrent access from many users, complex queries across large datasets, and full ACID guarantees without compromising on data integrity. PostgreSQL was designed from the start as a **multi-user, client-server system** where durability, correctness, and extensibility were non-negotiable.

| Aspect | SQLite | PostgreSQL |
|---|---|---|
| Birth year | 2000 | 1996 (open-source); 1986 (POSTGRES) |
| Original motivation | Eliminate server dependency in embedded systems | Extensible enterprise RDBMS |
| Design philosophy | Zero-config, serverless, single-file | Full-featured, multi-user, standards-compliant |

### Historical Context

SQLite emerged during the rise of **embedded computing** — PDAs, set-top boxes, and eventually smartphones. Today it is the most widely deployed database engine in the world (present in every Android phone, iOS device, web browser, and countless IoT systems). It is not competing with PostgreSQL; it is competing with `fopen()`.

PostgreSQL matured through the open-source movement of the late 1990s and 2000s. Its extensibility model (custom types, operators, index methods, procedural languages) made it the choice for organizations that outgrew MySQL's limitations but didn't want to pay Oracle licensing fees. Today it powers systems at Apple, Instagram, Spotify, and the U.S. Federal Aviation Administration.

---

## 2. Architecture Overview

### High-Level Architecture Comparison

```
┌─────────────────────────────────────────────────────────────────────┐
│                     PostgreSQL Architecture                         │
│                                                                     │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐                        │
│  │ Client 1 │   │ Client 2 │   │ Client N │   (TCP/IP or Unix)     │
│  └────┬─────┘   └────┬─────┘   └────┬─────┘                        │
│       │              │              │                               │
│       ▼              ▼              ▼                               │
│  ┌─────────────────────────────────────────┐                        │
│  │          Postmaster (main process)       │                        │
│  │     Spawns one backend per connection    │                        │
│  └──────────────┬──────────────────────────┘                        │
│                 │                                                    │
│    ┌────────────┼────────────┐                                      │
│    ▼            ▼            ▼                                      │
│ ┌────────┐ ┌────────┐ ┌────────┐                                   │
│ │Backend │ │Backend │ │Backend │  (one per connection)              │
│ │Process │ │Process │ │Process │                                    │
│ └───┬────┘ └───┬────┘ └───┬────┘                                   │
│     │          │          │                                         │
│     ▼          ▼          ▼                                         │
│ ┌──────────────────────────────────┐  ┌──────────────────────┐      │
│ │       Shared Memory              │  │  Background Workers  │      │
│ │  ┌────────────────────────┐      │  │  ┌─────────────────┐ │      │
│ │  │   Shared Buffer Pool  │      │  │  │  WAL Writer      │ │      │
│ │  │   (default 128 MB)    │      │  │  │  Checkpointer    │ │      │
│ │  └────────────────────────┘      │  │  │  Autovacuum      │ │      │
│ │  ┌────────────────────────┐      │  │  │  BG Writer       │ │      │
│ │  │   WAL Buffers         │      │  │  │  Stats Collector │ │      │
│ │  └────────────────────────┘      │  │  └─────────────────┘ │      │
│ │  ┌────────────────────────┐      │  └──────────────────────┘      │
│ │  │   Lock Tables         │      │                                 │
│ │  └────────────────────────┘      │                                │
│ └──────────────────────────────────┘                                │
│                 │                                                    │
│                 ▼                                                    │
│ ┌──────────────────────────────────────────────┐                    │
│ │              Disk Storage                     │                    │
│ │  base/       pg_wal/       pg_xact/          │                    │
│ │  (heap files) (WAL segments) (commit status)  │                    │
│ └──────────────────────────────────────────────┘                    │
└─────────────────────────────────────────────────────────────────────┘


┌─────────────────────────────────────────────────────────────────────┐
│                      SQLite Architecture                            │
│                                                                     │
│  ┌──────────────────────────────────────┐                           │
│  │         Application Process          │                           │
│  │                                      │                           │
│  │  ┌──────────────────────────────┐    │                           │
│  │  │     SQLite Library           │    │                           │
│  │  │  ┌────────────────────────┐  │    │                           │
│  │  │  │   SQL Compiler         │  │    │  Tokenizer → Parser      │
│  │  │  │   (Front End)          │  │    │  → Code Generator         │
│  │  │  └─────────┬──────────────┘  │    │                           │
│  │  │            │                 │    │                           │
│  │  │  ┌─────────▼──────────────┐  │    │                           │
│  │  │  │   Virtual Machine      │  │    │  Executes bytecode        │
│  │  │  │   (VDBE)               │  │    │                           │
│  │  │  └─────────┬──────────────┘  │    │                           │
│  │  │            │                 │    │                           │
│  │  │  ┌─────────▼──────────────┐  │    │                           │
│  │  │  │   B-Tree Module        │  │    │  One B-tree per table     │
│  │  │  │                        │  │    │  and per index            │
│  │  │  └─────────┬──────────────┘  │    │                           │
│  │  │            │                 │    │                           │
│  │  │  ┌─────────▼──────────────┐  │    │                           │
│  │  │  │   Pager               │  │    │  Page cache + journal     │
│  │  │  │   (Page Cache)        │  │    │  management               │
│  │  │  └─────────┬──────────────┘  │    │                           │
│  │  │            │                 │    │                           │
│  │  │  ┌─────────▼──────────────┐  │    │                           │
│  │  │  │   OS Interface (VFS)  │  │    │  Platform abstraction     │
│  │  │  └─────────┬──────────────┘  │    │                           │
│  │  └────────────┼──────────────────┘    │                           │
│  └───────────────┼──────────────────────┘                           │
│                  │                                                   │
│                  ▼                                                   │
│  ┌──────────────────────────────────┐                               │
│  │   Single Database File (.db)     │  +  Journal/WAL file          │
│  │   Header │ Page 1 │ Page 2 │ ...│                                │
│  └──────────────────────────────────┘                               │
└─────────────────────────────────────────────────────────────────────┘
```

### Architectural Model Summary

| Component | PostgreSQL | SQLite |
|---|---|---|
| **Execution model** | Client-server (multi-process) | In-process library |
| **Process model** | One OS process per connection | Single thread in caller's process |
| **IPC mechanism** | Shared memory + semaphores | Not needed (no IPC) |
| **Network protocol** | Custom wire protocol over TCP/IP | Direct function calls (C API) |
| **Storage** | Multiple files (one per table/index relation) | Single file for entire database |
| **Concurrency** | MVCC with row-level locking | Database-level locking (readers-writer lock) |
| **Footprint** | ~100 MB+ installed; processes use ~5-10 MB each | ~600 KB library; minimal memory |

---

## 3. Internal Design

### 3.1 Process Model

**PostgreSQL** uses a **process-per-connection** model:

1. The **Postmaster** process listens for incoming connections.
2. For each client, it `fork()`s a new **backend process**.
3. Each backend has its own private memory (work_mem, temp_buffers) but shares the **shared buffer pool** via OS shared memory (`shmget` / `mmap`).
4. Background processes (WAL writer, autovacuum launcher, checkpointer, background writer, stats collector) run independently.

**Why this design?** Process isolation provides fault tolerance — a crash in one backend doesn't bring down the entire server. This was critical when PostgreSQL was being developed (1990s), as thread safety in C was difficult and thread libraries were unreliable. The downside is **higher per-connection overhead** (~5-10 MB per backend), which is why connection poolers like PgBouncer are commonly used in production.

**SQLite** has **no separate process** at all:

1. The application calls `sqlite3_open()`, which maps the library into the application's address space.
2. All query parsing, optimization, and execution happens in the same thread that called the API.
3. There is no background processing — no autovacuum daemon, no WAL writer. The calling application does all the work.

**Why this design?** Eliminating the client-server boundary removes an entire class of failure modes: network partitions, authentication issues, process crashes, and IPC bugs. For single-user, embedded applications, this simplicity is a feature, not a limitation.

### 3.2 Storage Engine Architecture

#### PostgreSQL: Heap-Based Storage

PostgreSQL stores each table as a **heap file** — an unordered collection of 8 KB pages.

```
Heap File Structure (per table):
┌──────────────┬──────────────┬──────────────┬─────┐
│   Page 0     │   Page 1     │   Page 2     │ ... │
│   (8 KB)     │   (8 KB)     │   (8 KB)     │     │
└──────┬───────┴──────────────┴──────────────┴─────┘
       │
       ▼
┌──────────────────────────────────────────────────┐
│  Page Layout (8192 bytes)                        │
│  ┌──────────────────────────────┐                │
│  │  Page Header (24 bytes)      │                │
│  │  - pd_lsn (WAL position)    │                │
│  │  - pd_lower (free space start)│               │
│  │  - pd_upper (free space end) │                │
│  │  - pd_special (special area) │                │
│  ├──────────────────────────────┤                │
│  │  Item Pointers (Line Pointers)│               │
│  │  ItemId[0] → offset, length  │                │
│  │  ItemId[1] → offset, length  │                │
│  │  ItemId[2] → ...             │                │
│  ├──────────────────────────────┤                │
│  │                              │                │
│  │     Free Space               │                │
│  │                              │                │
│  ├──────────────────────────────┤                │
│  │  Tuple Data (grows backward) │                │
│  │  ┌─────────────────────┐     │                │
│  │  │ HeapTupleHeader     │     │                │
│  │  │  - t_xmin (inserting txn) │                │
│  │  │  - t_xmax (deleting txn) ││                │
│  │  │  - t_ctid (current TID)  ││                │
│  │  │  - t_infomask (flags)    ││                │
│  │  ├─────────────────────┤     │                │
│  │  │  Column Data (row)  │     │                │
│  │  └─────────────────────┘     │                │
│  └──────────────────────────────┘                │
└──────────────────────────────────────────────────┘
```

**Key design decisions:**
- **No clustering by primary key.** Tuples are stored in insertion order. This means a primary key lookup requires an index scan followed by a heap fetch (two I/Os minimum).
- **Line pointers provide indirection.** Indexes point to `(page, offset)` pairs. When a tuple is updated, the line pointer can be redirected to the new tuple location, potentially avoiding full index updates (HOT updates — Heap-Only Tuples).
- **MVCC metadata is stored inline.** Every tuple carries `xmin` and `xmax` transaction IDs, which enable visibility checks without consulting a separate data structure.

#### SQLite: B-Tree-Based Storage

SQLite stores **everything** in B-trees:

- **Tables** use B+ trees keyed by `rowid` (an implicit 64-bit integer primary key). Leaf pages contain the actual row data. This is effectively a **clustered index**.
- **Indexes** use B-trees where leaf nodes contain the indexed columns plus the rowid (acting as a pointer back to the table B+ tree).
- **WITHOUT ROWID tables** (since 3.8.2) store the table data directly in the index B-tree, keyed by the declared PRIMARY KEY. This is analogous to InnoDB's clustered index.

```
SQLite Database File:
┌─────────────────────────────────────────────────┐
│  File Header (first 100 bytes of page 1)        │
│  - Magic string: "SQLite format 3\000"          │
│  - Page size (default 4096 bytes)               │
│  - File format versions                         │
│  - Schema cookie                                │
│  - Free page count + trunk page pointer         │
├─────────────────────────────────────────────────┤
│  Page 1: Schema table (sqlite_master)           │
│  (B-tree containing table/index definitions)    │
├─────────────────────────────────────────────────┤
│  Page 2-N: Table and Index B-tree pages         │
│  ┌────────────────────────────────────┐         │
│  │  B-tree Page Layout               │         │
│  │  - Page header (8 or 12 bytes)    │         │
│  │  - Cell pointer array             │         │
│  │  - Unallocated space              │         │
│  │  - Cell content area              │         │
│  │  - Reserved region                │         │
│  └────────────────────────────────────┘         │
├─────────────────────────────────────────────────┤
│  Free pages (linked via trunk/leaf freelist)    │
└─────────────────────────────────────────────────┘
```

**Key design decision — why B-trees for everything?**

SQLite's single-file design means there's no filesystem-level mapping between tables and files. The B-tree structure provides:
1. **Efficient rowid lookups** (O(log n) for point queries).
2. **Ordered sequential scans** by rowid.
3. **Self-contained page management** — the B-tree module handles page allocation, splitting, and freelist management within the single file.

### 3.3 Page Layout Comparison

| Feature | PostgreSQL (8 KB page) | SQLite (default 4 KB page) |
|---|---|---|
| Header size | 24 bytes | 8 bytes (leaf) or 12 bytes (interior) |
| Row identification | Line pointer → tuple offset | Cell pointer → cell offset |
| Growth direction | Pointers grow forward, tuples grow backward | Cell pointers grow forward, cells grow backward |
| Overflow handling | TOAST (separate table for large values) | Overflow pages (linked list) |
| Free space tracking | pd_lower / pd_upper gap | Freeblock chain + fragment count |
| MVCC overhead | ~23 bytes per tuple (xmin, xmax, etc.) | None (single version per row) |

### 3.4 Index Implementation

**PostgreSQL B-Tree:**
- Standard Lehman-Yao B+ tree implementation (with right-link pointers for concurrent access).
- Index entries contain the key value and a TID (tuple identifier = block number + offset).
- Supports `CREATE INDEX CONCURRENTLY` (builds index without blocking writes).
- Also supports: Hash, GiST, SP-GiST, GIN, BRIN index types.

**SQLite B-Tree:**
- Pure B-tree (not B+) for index tables; B+ tree for table data.
- Interior pages store keys and child page pointers. Leaf pages store keys and rowids.
- No concurrent index builds — single-writer model makes this unnecessary.
- Only B-tree indexes (no hash, GiST, etc.).

### 3.5 Transaction Management

#### Concurrency Control

**PostgreSQL — MVCC (Multi-Version Concurrency Control):**

```
Transaction T1 (xid=100) inserts row:
  Tuple: [xmin=100, xmax=0, data="Alice"]

Transaction T2 (xid=200) updates the row:
  Old tuple: [xmin=100, xmax=200, data="Alice"]   ← marked as expired
  New tuple: [xmin=200, xmax=0,   data="Bob"]     ← new version

Transaction T3 (xid=150, snapshot=[100..199] committed):
  Sees: "Alice" (because xmin=100 is committed and visible,
         xmax=200 is not committed in T3's snapshot)
```

- **Readers never block writers; writers never block readers.**
- Each transaction sees a consistent snapshot of the database.
- Old tuple versions accumulate until `VACUUM` removes them.
- The `pg_xact` (formerly `pg_clog`) directory tracks commit/abort status of transactions.

**SQLite — Database-Level Locking:**

SQLite uses a **five-state locking protocol**:

```
  UNLOCKED  →  SHARED  →  RESERVED  →  PENDING  →  EXCLUSIVE
     ↑            ↑           ↑                        ↑
  (no access)  (reading)  (intend     (waiting for   (writing)
                            to write)   readers to
                                        finish)
```

- Multiple readers can hold SHARED locks simultaneously.
- Only one writer can exist at a time (EXCLUSIVE lock).
- **Writers block readers and readers block writers** when a write is in progress.
- WAL mode (introduced in 3.7.0) partially alleviates this: readers can proceed while one writer is active, because readers read from the main database file while the writer appends to the WAL file.

**Why the difference?** PostgreSQL must support dozens or hundreds of concurrent transactions modifying different rows. Row-level MVCC is essential. SQLite targets single-user (or low-concurrency) scenarios where the simplicity of a single-writer lock is acceptable and the overhead of maintaining multiple tuple versions would be wasted.

#### Durability Mechanisms

**PostgreSQL — WAL (Write-Ahead Logging):**

1. Before any data page modification, a WAL record describing the change is written to the WAL buffer.
2. WAL records are flushed to disk (`pg_wal/` directory) before the transaction is reported as committed.
3. Dirty data pages are written back lazily by the background writer or checkpointer.
4. On crash recovery, PostgreSQL replays WAL records forward from the last checkpoint.

**SQLite — Rollback Journal or WAL:**

*Rollback Journal mode (default):*
1. Before modifying a page, SQLite copies the **original page content** to a separate journal file.
2. Changes are applied to the main database file.
3. On commit, the journal file is deleted (or truncated).
4. On crash, the journal file is used to restore the original pages (rollback).

*WAL mode:*
1. Changes are appended to a WAL file instead of modifying the main database.
2. Readers check the WAL for the most recent committed version of each page.
3. A checkpoint operation transfers WAL contents back to the main database file.
4. WAL mode enables concurrent reads during writes (one writer, many readers).

| Durability Feature | PostgreSQL | SQLite (Journal) | SQLite (WAL) |
|---|---|---|---|
| Recovery method | Redo (replay WAL forward) | Undo (restore original pages) | Redo (replay WAL) |
| Journal content | Change records (logical) | Original page images (physical) | Changed page images |
| Commit latency | WAL fsync | Journal fsync + DB fsync | WAL fsync |
| Crash recovery speed | Proportional to WAL since checkpoint | Proportional to journal size | Proportional to WAL size |

---

## 4. Design Trade-Offs

### 4.1 Client-Server vs. Embedded

| Trade-off | PostgreSQL (Client-Server) | SQLite (Embedded) |
|---|---|---|
| **Deployment complexity** | Requires installation, configuration, user management | Zero-config; just link the library |
| **Network overhead** | Query/result serialization over TCP; latency per round-trip | Zero network overhead; direct function calls |
| **Failure isolation** | Backend crash doesn't affect other clients | Library crash = application crash |
| **Concurrent access** | Designed for hundreds of concurrent connections | Best for single-writer scenarios |
| **Administration** | Needs monitoring, backups, VACUUM, tuning | Self-maintaining; no DBA needed |
| **Portability** | Data tied to server instance | Database file is cross-platform portable |

**Architectural insight:** PostgreSQL's client-server model introduces operational complexity but enables **horizontal separation of concerns** — the database can be backed up, monitored, and scaled independently of the application. SQLite's embedded model eliminates this boundary, which is an advantage when the "application" is a mobile app or an IoT device where no sysadmin exists.

### 4.2 Heap Storage vs. Clustered B-Tree

| Trade-off | PostgreSQL (Heap) | SQLite (B+ Tree / rowid) |
|---|---|---|
| **Insert performance** | O(1) append to end of heap | O(log n) B-tree insertion |
| **Primary key lookup** | Index scan + heap fetch (2 I/Os) | Single B-tree traversal (1 logical I/O) |
| **Sequential scan** | Fast (sequential disk reads) | Also fast (leaf page traversal) |
| **Update in place** | Not possible (append new version) | Possible for fixed-size changes |
| **Space overhead** | MVCC metadata per tuple (~23 bytes) | Minimal per-cell overhead |
| **VACUUM needed?** | Yes (to reclaim dead tuple space) | No (B-tree rebalancing handles it) |

**Why PostgreSQL chose heap storage:** The heap model decouples physical storage order from logical ordering, which simplifies MVCC implementation. New tuple versions can be appended without reorganizing the physical layout. The cost is dead tuple accumulation and the need for VACUUM.

**Why SQLite chose clustered B-trees:** With a single-writer model and no MVCC, in-place updates and B-tree rebalancing are safe. The clustered layout ensures that primary key lookups are fast — critical for mobile apps doing frequent key-value lookups.

### 4.3 MVCC vs. Lock-Based Concurrency

| Aspect | PostgreSQL MVCC | SQLite Locking |
|---|---|---|
| **Read-write contention** | None (readers see snapshots) | Writers block readers (journal mode) |
| **Implementation complexity** | High (visibility rules, VACUUM, snapshot management) | Low (simple file locks) |
| **Storage overhead** | Multiple versions of each row | Single version |
| **Long-running queries** | Safe (snapshot isolation) | Can block writers |
| **Write throughput** | High (concurrent writers to different rows) | Single writer at a time |

### 4.4 Scalability Implications

**PostgreSQL scales up:**
- Can utilize multiple CPU cores (parallel query execution since v9.6).
- Shared buffer pool can be sized to hundreds of GB.
- Supports streaming replication for read scaling.
- Logical replication for selective data distribution.
- Handles databases in the terabyte range routinely.

**SQLite scales down:**
- Runs on devices with 32 KB of RAM (with careful configuration).
- The library is ~600 KB — smaller than most TCP/IP stacks.
- No background threads; no memory allocator fragmentation.
- Database files under 281 TB theoretical limit; practical limit around 1 TB.
- Performance degrades with more than ~5 concurrent writers.

---

## 5. Experiments / Observations

### 5.1 Observing SQLite's Locking Behavior

```sql
-- Terminal 1: Start a write transaction
BEGIN IMMEDIATE;
INSERT INTO test VALUES (1, 'hello');
-- Do NOT commit yet

-- Terminal 2: Try to read
SELECT * FROM test;
-- In JOURNAL mode: SQLITE_BUSY (blocked)
-- In WAL mode: Returns old data (snapshot)

-- Terminal 2: Try to write
INSERT INTO test VALUES (2, 'world');
-- SQLITE_BUSY in both modes (only one writer allowed)
```

**Observation:** WAL mode significantly improves read concurrency but does not enable concurrent writes. This demonstrates the fundamental design constraint: SQLite's single-file architecture makes true concurrent writes impossible without risking data corruption.

### 5.2 PostgreSQL MVCC Visibility

```sql
-- Session 1:
BEGIN;
SELECT txid_current();  -- Returns 1000
UPDATE users SET name = 'Bob' WHERE id = 1;
-- Do NOT commit

-- Session 2:
BEGIN;
SELECT txid_current();  -- Returns 1001
SELECT * FROM users WHERE id = 1;
-- Still sees old value ("Alice") because xid 1000 is not committed

-- Session 1:
COMMIT;

-- Session 2 (still in same transaction):
SELECT * FROM users WHERE id = 1;
-- STILL sees "Alice" under REPEATABLE READ
-- Sees "Bob" under READ COMMITTED (new snapshot per statement)
```

**Observation:** PostgreSQL's snapshot isolation is visible in practice. The session-level vs. statement-level snapshot difference between REPEATABLE READ and READ COMMITTED directly maps to the `xmin`/`xmax` visibility rules in the heap tuple headers.

### 5.3 Storage Overhead Comparison

For a table with 1 million rows of (INT, VARCHAR(50)):

| Metric | PostgreSQL | SQLite |
|---|---|---|
| Table size on disk | ~90 MB (heap + TOAST if needed) | ~52 MB (B-tree pages) |
| Per-row overhead | ~27 bytes (tuple header) | ~5-8 bytes (cell header) |
| After 500K updates | ~180 MB (dead tuples) | ~52 MB (in-place updates) |
| After VACUUM | ~90 MB | N/A |
| Index size (PK) | ~22 MB (separate B-tree) | 0 MB (clustered; table IS the index) |

**Observation:** PostgreSQL's MVCC approach roughly doubles storage after heavy update workloads until VACUUM runs. SQLite's in-place update model maintains stable storage size. This trade-off is acceptable for PostgreSQL because its target workloads value concurrency over storage efficiency.

### 5.4 Query Plan Comparison

```sql
-- PostgreSQL:
EXPLAIN ANALYZE SELECT * FROM orders WHERE customer_id = 42;
-- Index Scan using idx_orders_customer on orders
--   Index Cond: (customer_id = 42)
--   Rows Removed by Filter: 0
--   Planning Time: 0.15 ms
--   Execution Time: 0.08 ms

-- SQLite:
EXPLAIN QUERY PLAN SELECT * FROM orders WHERE customer_id = 42;
-- SEARCH TABLE orders USING INDEX idx_orders_customer (customer_id=?)
```

**Observation:** PostgreSQL's planner is significantly more sophisticated — it considers join ordering, parallel execution, different scan types (index scan, bitmap scan, index-only scan), and hash vs. merge joins. SQLite uses a simpler **next-generation query planner (NGQP)** that focuses on choosing the best index for each table access. This difference reflects their target complexity: PostgreSQL handles 20-table joins in analytics; SQLite handles simple OLTP queries on mobile.

---

## 6. Key Learnings

### Architectural Lessons

1. **There is no universally "better" database.** PostgreSQL and SQLite solve different problems. Comparing them is like comparing a cargo ship to a canoe — both are optimized for their environments.

2. **The process model is an architectural foundation, not a detail.** PostgreSQL's multi-process model enables fault isolation and concurrent writes but adds overhead. SQLite's in-process model eliminates communication costs but couples the database lifetime to the application. This choice cascades through every other design decision.

3. **MVCC is a trade-off, not a free lunch.** PostgreSQL gains concurrent read/write access at the cost of dead tuple accumulation and VACUUM overhead. SQLite avoids this complexity entirely because its single-writer model doesn't need it.

4. **Clustered vs. heap storage has profound performance implications.** SQLite's clustered B-tree layout means primary key lookups are single-I/O operations. PostgreSQL's heap layout means every index lookup requires an additional heap fetch — but it decouples physical layout from logical ordering, which simplifies concurrency.

5. **The single-file design is both SQLite's greatest strength and its fundamental limitation.** It enables portability and zero-config deployment, but it means all concurrency control must happen through file-level locking. There's no way to lock individual pages or rows at the filesystem level without a separate server process.

### Surprising Observations

- SQLite's test suite has **over 100 times more test code than application code** (~150,000 test lines vs. ~150,000 library lines). This extreme testing discipline is what makes it reliable enough for use in aircraft flight software.
- PostgreSQL's `VACUUM` was initially considered a fatal design flaw by critics. In practice, autovacuum and HOT updates have made it a manageable operational concern rather than a showstopper.
- SQLite in WAL mode with `PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;` achieves surprisingly high write throughput for single-writer workloads — often within 2x of PostgreSQL for simple INSERT operations.

### Practical Takeaways

| Use Case | Recommended DB | Reasoning |
|---|---|---|
| Mobile app local storage | SQLite | Zero-config, embedded, single-user |
| Web application backend | PostgreSQL | Concurrent access, ACID, complex queries |
| IoT edge data collection | SQLite | Minimal footprint, no server needed |
| Multi-user SaaS platform | PostgreSQL | Row-level concurrency, replication |
| Browser local storage | SQLite | Already embedded in all browsers |
| Data warehouse / analytics | PostgreSQL | Parallel queries, extensions (Citus) |
| Configuration file replacement | SQLite | Structured, queryable, atomic updates |
| Geospatial applications | PostgreSQL | PostGIS extension support |

---

## References

1. PostgreSQL Documentation — [https://www.postgresql.org/docs/current/](https://www.postgresql.org/docs/current/)
2. SQLite Documentation — [https://www.sqlite.org/docs.html](https://www.sqlite.org/docs.html)
3. Stonebraker, M., & Rowe, L. (1986). "The Design of POSTGRES." ACM SIGMOD.
4. Hipp, D. R. (2004). "SQLite: A Self-Contained SQL Database Engine."
5. PostgreSQL Source: `src/backend/storage/buffer/`, `src/backend/access/heap/`
6. SQLite Source: `src/btree.c`, `src/pager.c`, `src/vdbe.c`
7. Hellerstein, J. M., Stonebraker, M., & Hamilton, J. (2007). "Architecture of a Database System." Foundations and Trends in Databases.
