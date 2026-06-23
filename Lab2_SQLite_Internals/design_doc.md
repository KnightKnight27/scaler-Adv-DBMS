# Architectural Comparison: SQLite3 vs. PostgreSQL

This document provides a comparative analysis of the internal architectures of SQLite3 and PostgreSQL, focusing on memory-mapping, concurrency control, and storage layouts.

## 1. Architectural Comparison Matrix

| Feature | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| **Primary Architecture** | Serverless, Embedded (In-Process library) | Client-Server (Multi-process daemon) |
| **Memory Management** | Host memory mapping (`mmap`) & Page cache | Shared buffers pool (internal page manager) |
| **Concurrency Control** | Database-level lock (Reader/Writer WAL lock) | MVCC with fine-grained Row-level locks |
| **Storage Layout** | Single-file B-Tree (Clustered Data + Index) | Heap files for tables, separate B-Tree files for indexes |
| **Transactions** | WAL or Rollback journal | WAL with Write-Ahead Logging & MVCC engine |

---

## 2. Deep-Dive Design Analyses

### A. Memory Mapping Strategy

#### SQLite3 (`mmap`)
- SQLite can map the database file directly into the application process's address space using the host OS memory mapping system (`mmap_size` PRAGMA).
- **Pros**: Reading pages from a mapped file bypasses the OS page cache copying overhead (zero-copy reads). The OS manages page faulting and virtual memory allocation.
- **Cons**: Write operations cannot safely bypass locking, and memory mapping a large database on a 32-bit architecture is limited by address space constraints.

#### PostgreSQL (Shared Buffers)
- PostgreSQL manages its own memory buffer pool called **Shared Buffers** (usually configured to 25% of system RAM).
- It explicitly reads blocks of heap and index files into shared memory segments and manages page replacement internally (using a Clock-sweep derivative).
- **Pros**: Allows the database engine to implement custom dirty-page cleaning, lock-free lookups, and specialized eviction policies optimized for relational queries.
- **Cons**: Doubles caching overhead (double caching in both PostgreSQL Shared Buffers and the host OS Page Cache).

### B. Concurrency Control & Locking Models

#### SQLite3 (WAL Mode & Database Locks)
- In Write-Ahead Logging (WAL) mode, SQLite permits concurrent readers and a single writer. 
- Readers read from both the shared memory WAL index file (`-shm`) and the main database file without locking out the writer.
- Writes are appended to the WAL file (`-wal`). A database-wide write lock is acquired to prevent multiple processes from writing concurrently. 
- **Limitation**: Highly concurrent write-heavy workloads will experience lock contention (`SQLITE_BUSY`).

#### PostgreSQL (MVCC & Granular Locks)
- PostgreSQL uses Multi-Version Concurrency Control (MVCC) to provide high concurrency.
- Readers never block writers, and writers never block readers. Every update creates a new version of the row (tuple) in the heap page.
- Table-level locks are used for DDL, but DML uses row-level locks. Locking a row does not block readers because they see the version corresponding to their snapshot.
- PostgreSQL handles thousands of concurrent active connections using a multi-process architecture where each connection has a dedicated backend helper.

### C. Storage Engine & Layout

#### SQLite3 (Single-File B-Tree)
- A SQLite database is a single, contiguous file structured into fixed-size pages (e.g., 4096 bytes).
- Tables are stored as **B*Trees** (keys are row IDs, values are row data). Indexes are stored as **B-Trees** (keys are index columns + row ID, values are empty).
- Single-file design simplifies backup and transport but causes disk serialization bottleneck during page splits.

#### PostgreSQL (Heap Files + Separate Indexes)
- PostgreSQL stores tables as arrays of **Heap Pages** (non-sorted pages where rows are inserted wherever space is available).
- Indexes (like B-Tree, GIN, GiST) are stored in separate, dedicated files. Index leaf nodes contain a Tuple Identifier (TID), which is a physical pointer `(BlockNumber, OffsetNumber)` to the actual heap page where the row is located.
- **Heaps vs Index B-Trees**: This separation allows table rows to be appended rapidly without structural reshuffling, but requires index scans to do secondary lookups in the heap pages unless an Index-Only Scan is possible.
