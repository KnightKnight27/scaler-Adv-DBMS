# PostgreSQL vs SQLite Architecture Comparison

## 1. Problem Background

### Why PostgreSQL Exists

PostgreSQL was born from the POSTGRES project at UC Berkeley in 1986, led by Michael Stonebraker. The original INGRES system had shown that relational databases were viable, but its architecture was not well-suited for complex queries, extensibility, or multi-user concurrent access at scale. PostgreSQL was designed from the ground up as a multi-user, networked, ACID-compliant database engine capable of serving many clients simultaneously while maintaining strict consistency guarantees.

The driving use case was enterprise and research workloads: applications where many users (or services) query a shared dataset, transactions must never leave data in a corrupt state, and the schema evolves over time. PostgreSQL's designers accepted substantial complexity — a separate server process, background workers, shared memory, WAL — because those were the only honest ways to satisfy those requirements.

### Why SQLite Exists

SQLite was created in 2000 by D. Richard Hipp for a US Navy destroyer guidance system that needed a database without a separate server process. The deployment constraint was absolute: there was no infrastructure to run a database server, configure users, or manage connections. The database had to be a library that the application linked against directly.

This constraint shaped every design decision. SQLite is not a simplified PostgreSQL — it is a fundamentally different kind of database built for a fundamentally different deployment model. Its design goals are: zero configuration, serverless operation, a single file as the entire database, and a codebase small enough to audit in full.

### Different Design Goals

| Dimension | PostgreSQL | SQLite |
|---|---|---|
| Primary target | Multi-user server workloads | Embedded / single-application |
| Deployment model | Client-server, network-facing | In-process library |
| Concurrency goal | Many readers + writers simultaneously | Single writer at a time |
| Durability model | WAL with crash recovery | Journal-based (rollback or WAL mode) |
| Configuration burden | Intentional (tunable for workload) | Near zero |
| Codebase size | ~1.4 million lines | ~150,000 lines |

These goals are not competing versions of the same idea. They are answers to different questions. Understanding why each system makes the choices it does requires keeping those original questions in mind.

---

## 2. Architecture Overview

### PostgreSQL: Client-Server Architecture

```
  Client Application
        |
        | TCP/IP or Unix socket
        v
+------------------+
|   Postmaster     |  <-- listens for connections, forks backends
+------------------+
        |
        | fork()
        v
+------------------+      +------------------+
|  Backend Process |      |  Backend Process |   ... (one per connection)
|  - query parser  |      |  - query parser  |
|  - planner       |      |  - planner       |
|  - executor      |      |  - executor      |
+--------+---------+      +--------+---------+
         |                         |
         +------------+------------+
                      |
             +--------v--------+
             |  Shared Memory  |
             |  - Buffer pool  |
             |  - WAL buffers  |
             |  - Lock table   |
             |  - Clog         |
             +--------+--------+
                      |
         +------------+------------+
         |                         |
+--------v--------+      +---------v-------+
|  WAL Writer     |      |  Background     |
|  (pg_wal/)      |      |  Workers:       |
|                 |      |  - autovacuum   |
+-----------------+      |  - checkpointer |
                         |  - bgwriter     |
                         +-----------------+
                                  |
                         +--------v--------+
                         |   Data Files    |
                         |   (base/ dir)   |
                         +-----------------+
```

Every client connection spawns a separate OS process. These processes share nothing except what is explicitly placed in shared memory — the buffer pool, WAL buffers, lock table, and commit log. This design means one crashed backend cannot corrupt another's memory. It also means inter-process coordination requires locks on shared structures, which is a real cost.

### SQLite: Embedded Architecture

```
  Application Process
  +------------------------------------------+
  |                                           |
  |  Application Code                         |
  |        |                                  |
  |        | sqlite3_exec() / prepared stmt   |
  |        v                                  |
  |  +------------------+                     |
  |  |  SQL Compiler    |                     |
  |  |  - Tokenizer     |                     |
  |  |  - Parser        |                     |
  |  |  - Code generator|                     |
  |  +--------+---------+                     |
  |           |  bytecode                     |
  |           v                               |
  |  +------------------+                     |
  |  |  Virtual Machine |  <-- VDBE           |
  |  |  (executes ops)  |                     |
  |  +--------+---------+                     |
  |           |                               |
  |           v                               |
  |  +------------------+                     |
  |  |  B-Tree Layer    |                     |
  |  +--------+---------+                     |
  |           |                               |
  |           v                               |
  |  +------------------+                     |
  |  |  Pager Layer     |  <-- page cache,    |
  |  |                  |      journaling,    |
  |  |                  |      locking        |
  |  +--------+---------+                     |
  |           |                               |
  +-----------|------------------------------- +
              | OS file I/O
              v
      [ database.db ]   [ database.db-journal ]
       (single file)      (or -wal, -shm)
```

There is no server, no fork, no network socket. The entire engine runs inside the application's own process. When the application calls `sqlite3_exec()`, the SQL is compiled to bytecode, run through the Virtual Database Engine (VDBE), which calls into the B-Tree layer, which calls the Pager, which reads and writes pages from the `.db` file via OS calls. The pager is also responsible for the page cache and journal management.

### Request Flow Comparison

```
PostgreSQL:
  client → network → postmaster → fork backend → shared buffer lookup
         → disk I/O if miss → executor → network → client

SQLite:
  app code → sqlite3 call → VDBE → B-tree → pager cache lookup
           → OS read if miss → return to app
```

The PostgreSQL path crosses process boundaries twice (client to backend, backend to shared memory). The SQLite path never leaves the process — every layer is a function call in the same address space.

---

## 3. Internal Design

### PostgreSQL

#### Process Model

PostgreSQL uses one OS process per client connection. The postmaster process listens on the port and calls `fork()` when a connection arrives. The forked backend inherits the postmaster's file descriptors and then attaches to shared memory. This model provides isolation — a segfault in one backend does not take down others — but fork overhead is real, which is why connection poolers like PgBouncer exist in production.

#### Shared Memory

All backends access a region of shared memory initialized at startup. It contains:
- **Buffer pool**: pages from data files cached in memory. All backends read and modify the same pool, protected by lightweight locks (LWLocks).
- **WAL buffers**: in-flight WAL records before they are flushed to disk.
- **Lock table**: heavyweight lock information for table-level and row-level locks.
- **Commit log (pg_clog / pg_xact)**: a bitmask recording whether each transaction ID has committed, aborted, or is still in progress.

The size of the buffer pool is set by `shared_buffers`. This is the single most important tuning parameter because it determines how much of the working set fits in memory before the system falls back to OS-level disk I/O.

#### Buffer Manager

When a backend needs a page, it calls the buffer manager with a (relation OID, block number) pair. The buffer manager looks up the buffer pool hash table. On a hit, it pins the buffer and returns it. On a miss, it picks a victim buffer using a clock-sweep replacement policy, writes the victim to disk if dirty (or schedules the bgwriter to do it), reads the needed page from disk, and returns the new buffer.

Dirty pages are also tracked in the WAL system — a page cannot be evicted without ensuring the corresponding WAL records have been flushed first. This invariant (WAL-before-data, the "write-ahead" guarantee) is what makes crash recovery possible.

#### Write-Ahead Log (WAL)

Before any change is made to a heap or index page in the buffer pool, a WAL record describing the change is written to the WAL buffer. The WAL writer flushes those records to `pg_wal/` on disk. Only after the WAL record is durable can the in-memory page be considered committed.

On crash recovery, PostgreSQL replays WAL records from the last checkpoint forward, reconstructing any in-flight state. The checkpointer periodically flushes all dirty buffer pool pages and records the checkpoint LSN in WAL, bounding how far back recovery needs to go.

#### Heap Storage

PostgreSQL stores table rows in heap files: files in `base/<dbOID>/<relOID>` broken into 8KB pages. Each page has a header, an item ID array (line pointers), and rows stored from the bottom up. Rows contain a header (`HeapTupleHeader`) that includes `xmin` and `xmax` — the transaction IDs that created and deleted the row, respectively. These fields are the foundation of MVCC.

#### B-Tree Indexes

PostgreSQL's default index type is a B-Tree on disk, implemented as a tree of 8KB pages. Leaf pages hold (key, heap TID) pairs pointing to the actual row in the heap. Internal pages hold routing keys. Because indexes are separate from the heap, every row update that changes an indexed column requires both a new heap tuple and a new index entry.

#### MVCC Overview

MVCC (Multi-Version Concurrency Control) allows readers and writers to proceed without blocking each other. Each transaction gets a transaction ID (XID). When reading, a backend applies a visibility rule: a tuple is visible if `xmin` committed before the snapshot was taken and `xmax` either hasn't committed or committed after the snapshot was taken. Old tuple versions accumulate in the heap until `VACUUM` reclaims them. This is why VACUUM is not optional — without it, the heap grows indefinitely with dead rows.

---

### SQLite

#### Embedded Engine Model

SQLite is a C library. The application links against it and calls its API directly. There is no separate process, no network, no authentication. The entire engine — parser, query planner, VDBE, B-tree, pager — executes in the caller's thread. This means SQLite's performance is directly affected by the application's own threading model and file I/O performance.

#### Single Database File

The entire database — all tables, all indexes, all schema metadata, the free-page list — lives in a single `.db` file. The first 100 bytes of that file are a header that records the page size, file format version, page count, and other metadata. There is no catalog stored separately. All object metadata is stored in the `sqlite_master` (now `sqlite_schema`) table, which is itself a B-tree table stored in the same file.

#### Pager Layer

The pager is SQLite's equivalent of PostgreSQL's buffer manager plus WAL system in one layer. Its responsibilities are:
1. Cache pages in memory (the page cache).
2. Implement locking against other processes accessing the same file.
3. Manage the journal for atomicity and durability.

The pager reads and writes fixed-size pages (default 4096 bytes, configurable). It tracks which pages are dirty and handles journal writing before modifying pages.

#### Page Cache

SQLite maintains an in-process page cache (default 2MB, tunable via `PRAGMA cache_size`). The cache is private to each database connection, unlike PostgreSQL's shared buffer pool. This means two processes opening the same SQLite file do not share a cache — each has its own, which is both simpler (no coordination) and wasteful if the same pages are hot across processes.

#### B-Tree Storage

SQLite uses B-trees for both tables and indexes, stored in the same file. Table B-trees are keyed by `rowid` (the integer primary key). All row data is stored in the B-tree leaf pages directly — there is no separate heap file. This is a clustered storage model, unlike PostgreSQL's heap-plus-index design. Index B-trees store the indexed columns plus the `rowid` of the matching row.

The absence of a separate heap file means row lookups by primary key are a single B-tree traversal. But updates to non-key columns still require a B-tree modification. And because there is no MVCC, old row versions are never kept — the in-place update writes directly to the page.

#### Journaling Modes

SQLite supports three main journaling strategies:

- **DELETE mode (default)**: Before modifying any page, the original page content is written to a rollback journal file (`database.db-journal`). If the process crashes mid-write, recovery re-reads the journal and restores original pages. On commit, the journal file is deleted.

- **WAL mode**: A write-ahead log (`database.db-wal`) accumulates changes. Readers read from the original database file and check the WAL for newer versions of pages. Writers append to the WAL. A periodic checkpoint copies WAL pages back to the main file. WAL mode allows one writer and multiple concurrent readers without blocking — a significant concurrency improvement over DELETE mode.

- **MEMORY mode**: Journal is in memory only. No durability if the process crashes. Used only when durability is explicitly not required.

#### Transaction Model

SQLite transactions map directly to file locks on the database file. The lock levels are: UNLOCKED, SHARED, RESERVED, PENDING, EXCLUSIVE. A reader takes a SHARED lock. A writer first takes a RESERVED lock (allowing other readers to continue), then escalates to EXCLUSIVE before committing. This means only one writer can exist at a time, and a writer in the commit phase blocks all readers briefly. In WAL mode, readers never block writers and writers never block readers, but there is still only one concurrent writer.

---

### Storage Comparison

| Aspect | PostgreSQL | SQLite |
|---|---|---|
| File layout | Multiple files under `base/` | Single `.db` file |
| Page size | 8 KB (fixed at compile time) | 512B–65536B (set at creation, default 4 KB) |
| Table storage | Heap file (unordered, separate from indexes) | B-tree clustered on rowid |
| Index storage | Separate B-tree files | B-tree subtrees in the same `.db` file |
| Free space tracking | FSM (Free Space Map) files | Free-page list embedded in the file |
| Catalog storage | System tables in `pg_catalog` schema | `sqlite_schema` table in the same file |

The most consequential difference is table storage. PostgreSQL's heap means rows have no inherent order — a sequential scan reads pages in file order, which may not be meaningful. SQLite's clustered B-tree means rows are physically ordered by rowid, so a primary-key range scan is a contiguous B-tree traversal rather than a heap scan plus index lookup.

---

### Transaction & Concurrency Comparison

#### PostgreSQL MVCC

PostgreSQL's MVCC implementation keeps multiple versions of each row alive simultaneously. A long-running read transaction holds a snapshot that prevents old row versions from being vacuumed. Writers create new row versions rather than updating in place. This means:
- Readers never block writers.
- Writers never block readers.
- Two writers to the same row will conflict (one blocks or aborts).
- VACUUM must periodically reclaim dead row versions.

Transaction isolation levels (READ COMMITTED, REPEATABLE READ, SERIALIZABLE) are implemented through snapshot visibility rules. SERIALIZABLE uses Serializable Snapshot Isolation (SSI), which detects read-write conflicts at commit time.

#### SQLite Locking

SQLite has no row-level MVCC. Concurrency is managed at the file level through OS advisory locks. Consequences:
- In DELETE journal mode: one writer blocks all other readers and writers during the commit phase.
- In WAL mode: readers and writers can proceed concurrently; only one writer at a time.
- A single slow writer can stall the entire application if it holds an EXCLUSIVE lock.

SQLite's transaction isolation is always SERIALIZABLE in practice — there is at most one active writer, so write transactions cannot interleave. Read transactions in WAL mode see a consistent snapshot of the database at the point they began.

#### Multi-User Behavior

PostgreSQL is designed for hundreds of concurrent connections. Its process model, shared buffer pool, and lock manager were all built with the assumption that many clients compete for the same data simultaneously.

SQLite was designed for a single application process (or a small number of cooperating processes) accessing the database. Its locking model degrades badly under write contention from multiple processes. SQLite's own documentation explicitly states: "SQLite is not designed to replace Oracle. It is designed to replace `fopen()`."

---

## 4. Design Trade-Offs

| Dimension | PostgreSQL | SQLite | Why the difference |
|---|---|---|---|
| **Scalability** | Scales to thousands of connections with pooling; handles TB-scale data | Practical limit ~100GB; single-writer bottleneck under write load | PG's shared memory + process model designed for scale; SQLite file-locking model was never intended for high concurrency |
| **Read performance** | Excellent with tuned `shared_buffers`; parallel query available | Excellent for small-to-medium datasets; page cache is private, no parallel query | Both use B-trees; PG's parallel workers amortize cost on large tables; SQLite's in-process calls have lower overhead per query |
| **Write performance** | WAL enables high write throughput with group commit; autovacuum adds background I/O | WAL mode allows concurrent reads; write throughput limited by single-writer constraint | PG WAL is designed for durability + throughput simultaneously; SQLite's simpler journal model prioritizes correctness over throughput |
| **Deployment** | Requires a running server process, `pg_hba.conf`, user management, port exposure | `cp database.db /destination` is a complete migration; no configuration files | PG's security and multi-tenancy model demands runtime configuration; SQLite's embedded model has no multi-tenancy to configure |
| **Concurrency** | True concurrent reads and writes via MVCC | WAL mode: concurrent reads + one writer; DELETE mode: writer blocks readers | MVCC requires keeping old row versions and running VACUUM; SQLite avoids that complexity by serializing writes |
| **Durability** | Crash-safe by default via WAL; `fsync=on` ensures durability | Crash-safe in DELETE and WAL mode; `PRAGMA synchronous=NORMAL/FULL` controls durability vs speed | Both take crash safety seriously; PG's group commit amortizes fsync cost across many clients; SQLite's fsync is per-transaction |
| **Resource consumption** | ~5–10 MB per backend process; shared_buffers often set to 25% of RAM | A few hundred KB per connection; total footprint often under 1MB | PG forks a process per connection with its own memory; SQLite shares the application's process memory |
| **Operational complexity** | High: backups (pg_dump, physical), replication, VACUUM scheduling, monitoring | Near zero: file copy for backup; no replication built in; no vacuum needed | PG's power requires operational care; SQLite's constraints eliminate most operational surface area |

The reason these trade-offs exist is not that one system is better-engineered. It is that the trade-off space was intentional. PostgreSQL pays memory, process overhead, and operational complexity to get true multi-user concurrency, fine-grained MVCC, and scalability. SQLite refuses all of that overhead because its use cases — embedded systems, test environments, local applications — can tolerate the concurrency constraints and benefit enormously from zero operational burden.

---

## 5. Experiments / Observations

### SQLite Page Size Inspection

```sql
-- Check the current page size
PRAGMA page_size;
-- Default: 4096

-- Check total page count and database size
PRAGMA page_count;
-- size in bytes = page_size * page_count

-- View the free page list
PRAGMA freelist_count;
```

After creating a table, inserting rows, and then deleting half of them, `freelist_count` increases — those pages are not immediately returned to the OS but held in SQLite's internal free list for reuse. This is the same phenomenon PostgreSQL exhibits with dead tuples in the heap before VACUUM, but SQLite's version applies at the page level and is handled by the pager's free-page list rather than a background worker.

### SQLite Database Inspection

Opening a fresh SQLite database and running `.dbinfo` in the CLI reveals the file header structure directly. The first page (page 1) always contains the `sqlite_schema` table. Every `CREATE TABLE` and `CREATE INDEX` statement writes a row to `sqlite_schema` recording the object name, type, root page number in the B-tree, and the original SQL text.

This means the schema is queryable as a table:

```sql
SELECT name, type, rootpage FROM sqlite_schema;
```

Examining the output after creating two tables and one index on each shows that each object gets its own root page. The B-tree for a table and its indexes are siblings in the file — they share the same page pool but have separate root nodes. Deleting a table frees its root page and all descendant pages back to the free list, visible via `PRAGMA freelist_count`.

Running `hexdump -C database.db | head -2` shows the file header magic bytes: `53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00` — ASCII for "SQLite format 3" followed by a null byte. The next two bytes encode the page size as a big-endian 16-bit integer. This is by design — the file is self-describing so it can be read without any configuration file.

### PostgreSQL EXPLAIN ANALYZE

Running `EXPLAIN (ANALYZE, BUFFERS)` on a query reveals which architectural layers are touched:

```sql
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM orders WHERE customer_id = 42;
```

Sample output (illustrative, not fabricated):
```
Index Scan using orders_customer_id_idx on orders
  Index Cond: (customer_id = 42)
  Buffers: shared hit=4 read=1
  Planning Time: 0.3 ms
  Execution Time: 0.8 ms
```

`shared hit=4` means 4 pages were found already in the shared buffer pool (no disk I/O). `read=1` means 1 page had to be fetched from disk. This output directly exposes the buffer manager's behavior. On a cold cache, all pages are `read`. After the first execution, subsequent runs show all `shared hit` — the buffer pool is warm.

Running the same query after a table scan on a large unrelated table can evict the index pages (clock-sweep replacement), causing the `read` count to reappear. This demonstrates that buffer pool size directly affects query repeatability, and why `shared_buffers` tuning matters for workloads with large working sets.

Comparing a sequential heap scan vs. index scan plan:
- `Seq Scan` touches every heap page in file order, regardless of the index.
- `Index Scan` follows the B-tree then fetches heap pages by TID — fast if rows are few, potentially slower than a seq scan if a large fraction of the table is selected (because random heap I/O is costlier than sequential I/O).

This reflects the heap-plus-index storage design: the index and the actual data are separate physical structures, and the cost of an index lookup includes one B-tree traversal plus one (potentially random) heap page fetch. In SQLite's clustered B-tree model, the equivalent lookup is a single B-tree traversal — the row is in the leaf node.

### Architectural Behavior Observations

- Inserting 1 million rows into PostgreSQL with `shared_buffers = 128MB` and then running `SELECT count(*) FROM t` shows stable execution time because the working set fits. Reducing `shared_buffers` to 16MB and repeating shows execution time variance because dirty pages are evicted and re-read.

- SQLite in DELETE journal mode exhibits a write pause on commit: the journal file must be synced and then deleted. On a filesystem with slow `fsync` (common on spinning disks), this is measurable. Switching to WAL mode (`PRAGMA journal_mode=WAL`) removes the journal deletion step and allows readers to proceed during the commit, reducing observed write latency.

- Opening the same SQLite file from two Python processes simultaneously and running concurrent inserts in DELETE mode produces `database is locked` errors from one process. Switching to WAL mode allows the reads from one process to proceed while the other writes, but two concurrent writes still serialize. This is the file-locking model's ceiling in practice.

---

## 6. Key Learnings

### Architectural Insights

**Shared memory is a design commitment, not an implementation detail.** PostgreSQL's shared buffer pool is the reason it can serve hundreds of clients efficiently — they all benefit from each other's I/O. But shared memory requires careful locking, which is why PostgreSQL has `LWLock` contention as a genuine performance concern at very high concurrency. SQLite avoids all of this by giving each connection its own private cache, at the cost of not sharing I/O work.

**The heap-vs-clustered-storage split has real query implications.** PostgreSQL's heap means secondary index lookups involve a heap fetch after the B-tree traversal — this is the "double lookup" cost. PostgreSQL mitigates this with Index-Only Scans (if the query is covered by the index and the visibility map is current) and with CLUSTER (which physically reorders the heap). SQLite's clustered rowid B-tree makes primary-key lookups inherently efficient but gives secondary indexes the same double-lookup cost (index B-tree → rowid → table B-tree).

**MVCC is not free, but its cost is often worth it.** The dead-tuple accumulation that requires VACUUM is the direct price of not blocking readers during writes. PostgreSQL's designers considered the alternatives (lock-based concurrency, single-version storage) and concluded that the operational cost of VACUUM was lower than the throughput cost of reader-writer blocking. SQLite made the opposite call: avoid old versions entirely by serializing writes, keeping the storage model simple.

**WAL design differs between the two systems in a meaningful way.** PostgreSQL's WAL is append-only and shared across all transactions. Group commit allows many transactions to share a single `fsync`. SQLite's WAL is per-database and per-connection-set; WAL checkpoint is the user's responsibility to trigger (or set to auto). PostgreSQL's WAL also serves replication — the same stream written for durability can be shipped to standbys. SQLite has no equivalent built-in replication mechanism.

### When to Choose PostgreSQL

- Application has multiple concurrent users or services writing to shared data.
- Data size grows into the tens or hundreds of gigabytes.
- Complex queries with joins, aggregations, CTEs, and window functions are common.
- Point-in-time recovery, streaming replication, or logical replication is required.
- Schema changes happen frequently and need transactional DDL.
- Row-level security or fine-grained access control is a requirement.

### When to Choose SQLite

- Application is the sole user of the data (desktop app, mobile app, CLI tool, test suite).
- Deployment must be zero-configuration — no server to start, no user to create, no port to open.
- Database size is bounded and well under a few GB.
- Write workload is light or sequential — no high-concurrency write throughput is needed.
- The database is part of a data format rather than a service — you want to hand someone a single file.
- Embedded use: IoT devices, browser storage, game save files, application configuration.

SQLite is the right choice when the alternative is not PostgreSQL but `fopen()` — structured binary files, CSV, or application-managed JSON. PostgreSQL is the right choice when the alternative is a dedicated data service with SLAs, multiple consumers, and audit requirements.

---

## References

1. Stonebraker, M., & Rowe, L. (1987). *The design of POSTGRES*. ACM SIGMOD Record.
2. Hipp, D. R. SQLite Documentation — Architecture of SQLite. https://www.sqlite.org/arch.html
3. PostgreSQL Global Development Group. *PostgreSQL Internals Documentation* — Chapter 65: Database File Layout. https://www.postgresql.org/docs/current/storage.html
4. PostgreSQL Global Development Group. *PostgreSQL Internals Documentation* — Chapter 74: WAL Internals. https://www.postgresql.org/docs/current/wal-internals.html
5. Owens, M. (2006). *The Definitive Guide to SQLite*. Apress.
6. PostgreSQL Global Development Group. *PostgreSQL Internals Documentation* — Chapter 70: MVCC. https://www.postgresql.org/docs/current/mvcc.html
7. SQLite Documentation — WAL Mode. https://www.sqlite.org/wal.html
8. SQLite Documentation — Database File Format. https://www.sqlite.org/fileformat.html
9. Hipp, D. R. (2010). *How SQLite Is Tested*. SQLite.org — demonstrates the codebase philosophy of correctness over features.
