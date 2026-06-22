# PostgreSQL vs SQLite Architecture Comparison

## 1. Overview

PostgreSQL and SQLite represent two fundamentally different approaches to database architecture. While both are relational databases supporting SQL, they are designed for entirely different deployment models, concurrency requirements, and scalability targets. This document compares their architectures across multiple dimensions.

---

## 2. Overall Architecture

### PostgreSQL: Client-Server Architecture

PostgreSQL follows a **multi-process client-server model**:

- **Postmaster Process**: The main server process that listens for incoming connections on a TCP port (default 5432). It forks a new backend process for each client connection.
- **Backend Processes**: Each client gets a dedicated backend process with its own memory space. These processes execute queries and return results.
- **Background Workers**: Multiple auxiliary processes handle specific tasks:
  - **Writer Process**: Writes dirty pages from shared buffers to disk
  - **WAL Writer**: Flushes WAL records to disk
  - **Autovacuum Launcher/Workers**: Reclaim dead tuples and update statistics
  - **Checkpointer**: Performs checkpoint operations
  - **Stats Collector**: Gathers performance statistics
  - **WAL Archiver**: Archives WAL segments for PITR
- **Shared Memory**: All backend processes share a large memory segment containing:
  - **Shared Buffers**: Cache of recently accessed disk pages
  - **WAL Buffers**: Pending WAL records before disk flush
  - **CLOG Buffers**: Commit status of transactions
  - **Lock Manager**: Tracks table and row-level locks

**Why Client-Server?**
PostgreSQL is designed for multi-user, concurrent access over a network. The client-server model enables:
- **Connection pooling**: Multiple clients share a pool of backend processes
- **Resource isolation**: Each connection runs in its own process, preventing one bad query from crashing the entire server
- **Network accessibility**: Applications on different machines can connect via TCP/IP
- **Fine-grained access control**: Role-based authentication and authorization at the connection level
- **Scalability**: Can distribute read load across replicas

### SQLite: Embedded Architecture

SQLite is a **serverless, embedded database**:

- **Single Library**: The entire database engine is a C library (~1MB) linked directly into the application
- **No Separate Process**: The application process itself contains the database engine
- **Single File Storage**: The entire database (tables, indexes, views, triggers) lives in a single ordinary disk file
- **Zero Configuration**: No setup, no server to start, no user management
- **Direct File Access**: The application reads and writes the database file directly via the OS filesystem

**Why Embedded?**
SQLite is designed for simplicity and zero-administration deployment:
- **Zero overhead**: No inter-process communication, no network latency
- **Portability**: A database is just a file — copy it, email it, back it up with `cp`
- **Reliability**: No separate process to crash or run out of memory
- **Small footprint**: Ideal for mobile apps, IoT devices, browsers, and desktop software
- **Testing**: Can use in-memory databases (`:memory:`) for fast unit tests

---

## 3. Process Model Comparison

| Aspect | PostgreSQL | SQLite |
|--------|-----------|--------|
| **Processes** | Multi-process (1 backend per connection) | Single-process (embedded in app) |
| **Memory Model** | Shared memory across backends | Private to application process |
| **Connection Overhead** | ~5-10MB per backend process | Negligible (function call) |
| **Max Connections** | Configurable (typically 100-1000+) | Unlimited readers, 1 writer |
| **Crash Isolation** | One backend crash doesn't affect others | Application crash = database crash |
| **Startup Time** | Seconds (initialize shared memory, WAL) | Milliseconds (open file) |

### PostgreSQL Process Model Deep Dive

When a client connects:
1. Postmaster accepts the TCP connection
2. Postmaster forks a new backend process
3. Backend performs authentication
4. Backend allocates private memory for sort/hash operations
5. Client sends queries; backend parses, plans, executes, and returns results
6. On disconnect, backend process terminates

This model provides excellent isolation but has overhead. Connection pooling (PgBouncer) is essential for high-throughput applications to avoid the fork() overhead per connection.

### SQLite Process Model Deep Dive

When the application opens a database:
1. SQLite library opens the file with `fcntl()` file locks
2. Reads the 100-byte database header to validate format
3. Caches the schema in memory
4. Queries execute by traversing B-trees in the file
5. On close, all state is discarded (except the file itself)

SQLite uses POSIX advisory locks (or Windows file locks) to coordinate between processes. In WAL mode, a shared-memory file (`-shm`) is used for lock management.

---

## 4. Storage Engine Architecture

### PostgreSQL: Heap-Organized Storage with MVCC

PostgreSQL uses a **heap-organized storage model** with Multi-Version Concurrency Control (MVCC):

- **Heap Files**: Table data is stored in heap files (`.heap` segments) as an unordered collection of tuples
- **Tuple Header**: Every tuple carries system columns:
  - `xmin`: Transaction ID that inserted the tuple
  - `xmax`: Transaction ID that deleted the tuple (0 = live)
  - `cmin`/`cmax`: Command IDs within a transaction
  - `ctid`: Physical location (block, offset) of the tuple
- **No In-Place Updates**: UPDATE creates a new tuple version; old version remains until VACUUM reclaims it
- **Visibility Rules**: Each transaction uses a snapshot to determine which tuple versions are visible

**Storage Layout:**
```
Table File (1GB segments)
├── Page 0 (8KB)
│   ├── Page Header (24 bytes)
│   ├── Line Pointer Array
│   ├── Free Space
│   └── Tuple Data (from bottom up)
├── Page 1
└── ...
```

### SQLite: B-Tree Storage Engine

SQLite uses a **single B-tree per table/index** stored in a single file:

- **Table B-Trees**: Store all row data in leaf pages; internal pages contain only keys and child pointers
- **Index B-Trees**: Store indexed columns; leaf pages contain the indexed values and rowids
- **Page Size**: Default 4096 bytes (configurable: 512-65536)
- **Single File**: Everything — schema, data, indexes, triggers — in one `.db` file

**Page Layout (every page):**
```
┌─────────────────────────────────────┐
│ Page Header (8-12 bytes)            │
│  - Page type (leaf/internal)        │
│  - Number of cells                  │
│  - Free space offset                │
├─────────────────────────────────────┤
│ Cell Pointer Array                  │
│  - Sorted offsets to cells          │
├─────────────────────────────────────┤
│ Free Space                          │
├─────────────────────────────────────┤
│ Cell Content Area (bottom-up)       │
│  - Variable-length records          │
└─────────────────────────────────────┘
```

**Key Differences:**
| Feature | PostgreSQL | SQLite |
|---------|-----------|--------|
| **Organization** | Heap files (unordered) | B-trees (ordered by key) |
| **Updates** | Append-only (MVCC) | In-place (B-tree rebalancing) |
| **File Count** | Multiple files per table | Single file for everything |
| **Page Size** | Fixed 8KB | Configurable (default 4KB) |
| **Tuple ID** | ctid (physical location) | rowid (64-bit integer key) |

---

## 5. Page Layout Comparison

### PostgreSQL Page Layout (8KB)

```
┌────────────────────────────────────────┐
│ Page Header (24 bytes)               │
│  - pd_lsn: WAL LSN of last change    │
│  - pd_checksum: Page checksum          │
│  - pd_flags: Page state flags        │
│  - pd_lower: Offset to free space    │
│  - pd_upper: Offset to tuple data    │
│  - pd_special: Offset to special area│
├────────────────────────────────────────┤
│ Line Pointer Array                    │
│  - Each entry: 4 bytes (offset, length)│
│  - Points to tuple versions          │
├────────────────────────────────────────┤
│ Free Space                            │
├────────────────────────────────────────┤
│ Tuple Data (grows upward from bottom) │
│  - Tuple Header (23+ bytes)          │
│  - Null Bitmap                       │
│  - User Data                         │
├────────────────────────────────────────┤
│ Special Space (index pages only)      │
└────────────────────────────────────────┘
```

**Key Features:**
- **Line Pointers**: Indirection layer allows tuple movement without updating indexes (HOT updates)
- **Tuple Header**: Contains xmin, xmax, t_ctid for MVCC
- **Free Space Map (FSM)**: Tracks available space per page for efficient INSERT placement
- **Visibility Map (VM)**: Bitmask indicating which pages contain only visible tuples (speeds up VACUUM and index-only scans)

### SQLite Page Layout

```
┌────────────────────────────────────────┐
│ Database Header (100 bytes, page 1)  │
│  - "SQLite format 3 " signature     │
│  - Page size, file format version     │
│  - Number of pages, schema cookie     │
│  - Free page list head                │
├────────────────────────────────────────┤
│ B-Tree Page Header (8-12 bytes)       │
│  - Page type flags                    │
│  - First free block offset            │
│  - Number of cells                    │
│  - Cell content area start            │
│  - Fragmented free bytes              │
│  - Rightmost child (internal only)    │
├────────────────────────────────────────┤
│ Cell Pointer Array (2 bytes each)       │
│  - Sorted by key value                │
├────────────────────────────────────────┤
│ Free Space                            │
├────────────────────────────────────────┤
│ Cell Content Area (bottom-up)         │
│  - Variable-length integer encoding   │
│  - Record format (header + body)      │
└────────────────────────────────────────┘
```

**Key Features:**
- **Variable-Length Integers**: SQLite uses a compact variable-length integer format (1-9 bytes) to minimize storage
- **Record Format**: Each cell contains a header (column types/sizes) and body (actual data)
- **Free Block List**: Free space within pages is tracked as a linked list for reuse
- **Overflow Pages**: Large records spill into chained overflow pages

---

## 6. Index Implementation

### PostgreSQL Indexes

PostgreSQL supports multiple index types, each optimized for different workloads:

**B-Tree (default):**
- Balanced tree structure with fan-out ~100-200
- Supports equality, range, and ordering operations
- Leaf pages contain `(key, ctid)` pairs
- Internal pages contain `(key, child_page)` pairs
- **Key Feature**: Uses "High-Key" optimization — internal pages store upper bounds, not exact keys

**Hash Indexes:**
- Store hash values of keys for fast equality lookups
- Rebuilt in PostgreSQL 10+ to be WAL-logged and crash-safe
- Smaller than B-trees for single-column equality

**GiST (Generalized Search Tree):**
- Framework for building custom index types
- Used for: full-text search, geometric data, nearest-neighbor queries
- Supports "lossy" indexes where the index returns a superset of matching rows

**GIN (Generalized Inverted Index):**
- Inverted index structure (like search engines)
- Optimized for composite values: arrays, JSONB, full-text documents
- Stores `(element, posting_list)` pairs
- Fast for `contains` queries, slower for insertions

**BRIN (Block Range Index):**
- Tiny indexes for very large, naturally ordered tables
- Stores min/max values per block range (e.g., 128 pages)
- Ideal for time-series data where recent data is appended

### SQLite Indexes

SQLite uses a **single B-tree index structure** for all indexes:

**Table B-Trees:**
- 64-bit signed integer `rowid` as the key
- All user data stored in leaf pages
- Internal pages contain only `rowid` ranges and child pointers
- If `WITHOUT ROWID`, the PRIMARY KEY becomes the B-tree key

**Index B-Trees:**
- Key = indexed column values + rowid
- No data stored in index leaves (must look up table for full row)
- Supports partial indexes (`WHERE` clause) and covering indexes

**Index Characteristics:**
- All indexes are B-trees (no hash, GiST, or GIN equivalents)
- Index entries are sorted by the indexed columns
- SQLite automatically uses indexes when the query planner determines they're beneficial
- `ANALYZE` command creates sqlite_stat1 table with index selectivity statistics

---

## 7. Transaction Management & Concurrency Control

### PostgreSQL: MVCC with Snapshot Isolation

PostgreSQL implements **Multi-Version Concurrency Control (MVCC)** using tuple versioning:

**Transaction Isolation:**
- **READ COMMITTED** (default): New snapshot per statement
- **REPEATABLE READ**: Snapshot at transaction start; detects serialization anomalies
- **SERIALIZABLE**: Full serializable isolation using predicate locking

**Concurrency Guarantees:**
- Readers never block writers
- Writers never block readers
- No read locks needed for SELECT
- Row-level locking for UPDATE/DELETE (via tuple hints)

**Locking Hierarchy:**
```
Table Locks (8 modes from ACCESS SHARE to ACCESS EXCLUSIVE)
  └── Row Locks (FOR UPDATE, FOR NO KEY UPDATE, FOR SHARE, FOR KEY SHARE)
      └── Predicate Locks (SERIALIZABLE only)
```

**Deadlock Detection:**
PostgreSQL automatically detects deadlocks by building a wait-for graph and aborts one transaction (the "victim") to break the cycle.

### SQLite: File-Level Locking with WAL Mode

SQLite has two concurrency models:

**Rollback Journal Mode (default):**
- Readers get SHARED locks on the database file
- Writers get RESERVED then EXCLUSIVE locks
- Only one writer at a time
- Simple but limited concurrency

**WAL Mode (Write-Ahead Logging):**
- Writers append changes to a separate `.wal` file
- Readers read from the original database file
- Multiple readers can coexist with one writer
- WAL file is periodically checkpointed (merged) into the main database
- **Checkpoint types**: PASSIVE (default), FULL, RESTART, TRUNCATE

**Concurrency in WAL Mode:**
```
Readers: Read from last checkpoint snapshot
Writer: Append to WAL file (no blocking of readers)
Checkpoint: Merge WAL into DB (brief pause for new readers)
```

**Limitations:**
- Still only **one writer** at a time (no concurrent writes)
- Best for read-heavy workloads with occasional writes
- WAL file can grow large if checkpoints are infrequent

---

## 8. Durability Mechanisms

### PostgreSQL: WAL (Write-Ahead Logging)

PostgreSQL uses WAL for durability and crash recovery:

**WAL Architecture:**
- Sequential append-only log files (16MB segments by default)
- Stored in `pg_wal/` directory
- Every data modification generates a WAL record before the data page is written

**WAL Record Structure:**
```
┌─────────────────────────────┐
│ Record Header (24 bytes)    │
│  - Total length             │
│  - Transaction ID           │
│  - Previous record pointer  │
│  - CRC checksum             │
├─────────────────────────────┤
│ Resource Manager Info       │
│  - Heap, Btree, Sequence... │
├─────────────────────────────┤
│ Block References            │
│  - Page number, fork        │
├─────────────────────────────┤
│ Main Data                   │
│  - Old values (undo info)   │
│  - New values (redo info)   │
└─────────────────────────────┘
```

**Crash Recovery:**
1. Read `pg_control` to find the last checkpoint
2. Replay WAL from the redo point forward
3. Apply committed transactions; skip uncommitted ones
4. Full Page Writes (FPW) handle torn pages after crashes

**Durability Levels:**
- `synchronous_commit = on`: WAL fsync before COMMIT returns (safest)
- `synchronous_commit = off`: WAL flushed periodically (faster, risk of 1-3s data loss)
- `synchronous_commit = local`: Same as on but no wait for replicas

### SQLite: Atomic Commit via Journal

SQLite guarantees atomic commits using a journal mechanism:

**Rollback Journal:**
1. Acquire EXCLUSIVE lock on database
2. Copy original pages to `.journal` file
3. Write changes to database pages
4. Flush changes to disk (fsync)
5. Delete journal file (commit complete)

**WAL Mode Durability:**
1. Append changes to `.wal` file
2. fsync WAL file
3. COMMIT returns (durable)
4. Later: checkpoint merges WAL into DB file

**Advantages:**
- Simpler than PostgreSQL's WAL
- No separate background processes
- Atomic even on systems with unreliable fsync()

---

## 9. Scalability Implications

### PostgreSQL Scalability

**Vertical Scaling:**
- Efficient use of multi-core CPUs (parallel queries since PG 9.6)
- Large shared buffers (can use 25% of RAM, up to hundreds of GB)
- Parallel index builds, vacuum, and queries

**Horizontal Scaling:**
- **Streaming Replication**: Physical WAL replication to standbys (async or sync)
- **Logical Replication**: Row-level replication for selective data distribution
- **Partitioning**: Native table partitioning (declarative since PG 10)
- **Connection Pooling**: PgBouncer, pgpool-II for managing thousands of connections

**Limitations:**
- Single-writer bottleneck for a single table (no sharding in core)
- Vacuum overhead on high-churn tables
- Connection overhead limits raw connection count

### SQLite Scalability

**Vertical Scaling:**
- Single-threaded writer limits CPU utilization
- Entire database must fit in application memory for best performance
- No parallel query execution

**Horizontal Scaling:**
- **Not natively supported**: Single file = single node
- **Emerging solutions**: LiteFS, Turso, rqlite provide distributed SQLite
- **Read Replicas**: Can copy the file to read replicas, but writes must go to primary

**Limitations:**
- Single writer is the fundamental bottleneck
- No built-in replication or clustering
- Database size limited by filesystem (theoretical max: 281 TB)

---

## 10. Real-World Use Cases

### When to Choose PostgreSQL

- **Web Applications**: Multi-user SaaS platforms, APIs, e-commerce
- **Analytics**: Complex queries, window functions, CTEs, large datasets
- **Geographic Data**: PostGIS extension for spatial queries
- **Full-Text Search**: Advanced text search with ranking and highlighting
- **Financial Systems**: ACID compliance with strict durability requirements
- **Multi-Tenant Applications**: Row-level security, schema-per-tenant
- **High Concurrency**: Thousands of concurrent connections with mixed read/write

**Notable Users**: Instagram, Reddit, Spotify, Uber, Netflix

### When to Choose SQLite

- **Mobile Applications**: iOS (Core Data), Android (Room), embedded systems
- **Desktop Software**: Applications needing local data storage
- **Browser Storage**: Web SQL Database (deprecated but influential)
- **IoT Devices**: Resource-constrained environments
- **Testing/Development**: Fast in-memory databases for unit tests
- **Data Transfer**: Single-file databases for easy sharing
- **Edge Computing**: Local data processing with minimal overhead
- **Application File Format**: Replace custom binary formats with SQL

**Notable Users**: Android, iOS, Chrome, Firefox, macOS, WhatsApp, Expensify

---

## 11. Trade-offs Summary

| Dimension | PostgreSQL | SQLite |
|-----------|-----------|--------|
| **Architecture** | Client-server, multi-process | Embedded, single-file |
| **Setup Complexity** | High (install, configure, tune) | Zero (link library, open file) |
| **Concurrency** | MVCC, thousands of connections | File locks, one writer |
| **Memory Usage** | 25-50% of RAM for shared buffers | Minimal (application-dependent) |
| **Query Complexity** | Full SQL, complex analytics | Standard SQL, simpler queries |
| **Data Types** | Rich (arrays, JSONB, geometric) | Dynamic typing (5 storage classes) |
| **Extensibility** | Extensions, custom types, PL/pgSQL | Limited (loadable extensions) |
| **Replication** | Streaming, logical, built-in | None (external tools available) |
| **Backup** | pg_dump, pg_basebackup, WAL archiving | File copy (`cp`, `rsync`) |
| **Performance Profile** | Optimized for concurrent OLTP | Optimized for single-user reads |
| **Operational Overhead** | High (monitoring, vacuum, tuning) | Virtually zero |

---

## 12. Key Architectural Insights

1. **PostgreSQL trades simplicity for power**: The client-server model, MVCC, and background processes add complexity but enable enterprise-grade features.

2. **SQLite trades concurrency for simplicity**: The single-file, embedded design eliminates operational overhead but caps write concurrency.

3. **MVCC vs. Locking**: PostgreSQL's MVCC allows readers to see consistent snapshots without locking, while SQLite's WAL mode provides snapshot isolation for readers but serializes writers.

4. **Durability Philosophy**: PostgreSQL uses WAL for both durability and replication; SQLite uses simpler journal mechanisms focused on atomicity.

5. **The Convergence Trend**: Tools like Litestream, LiteFS, and Turso are bringing SQLite closer to server use cases, while PostgreSQL continues to add features (JSONB, partitioning) that encroach on NoSQL territory.

---

## 13. References

- PostgreSQL Documentation: https://www.postgresql.org/docs/current/
- SQLite File Format: https://www.sqlite.org/fileformat.html
- "SQLite vs PostgreSQL" - Algoroq: https://algoroq.io/compare-tech/sqlite-vs-postgresql/
- "PostgreSQL vs SQLite" - Dev.to: https://dev.to/lovestaco/postgresql-vs-sqlite-dive-into-two-very-different-databases-5a90
