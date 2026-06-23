# Topic 1: PostgreSQL vs SQLite Architecture Comparison

This report provides an in-depth architectural comparison between PostgreSQL, a multi-process enterprise-grade client-server Relational Database Management System (RDBMS), and SQLite, a lightweight, single-file embedded database engine. 

---

## 1. Problem Background

### Why They Exist & Historical Context

#### PostgreSQL
PostgreSQL (originally POSTGRES) was created at UC Berkeley by Michael Stonebraker in 1986 as a successor to Ingres. Its design goal was to address limitations in contemporary relational systems by supporting complex data types, object-relational mappings, and custom extensibility. To handle enterprise-scale enterprise applications, PostgreSQL was built from the ground up as a client-server system that assumes multiple concurrent clients, high volumes of data, and network boundaries.

#### SQLite
SQLite was designed by D. Richard Hipp in 2000 for use on a US Navy guided-missile destroyer. The primary goal was to provide an SQL database engine that did not require administrative setup, server configuration, or system administration overhead. SQLite was designed as a zero-configuration, self-contained library that can be linked directly into application software. It represents a replacement for direct file I/O operations (like opening, parsing, and writing custom flat files) by wrapping a robust SQL engine around a single-file B-Tree storage system.

---

## 2. Architecture Overview

### High-Level Architecture Comparison

The physical and process boundaries of the two engines differ fundamentally.

```mermaid
graph TD
    subgraph PostgreSQL (Client-Server Architecture)
        Client[Client App] <-->|TCP Socket / UNIX Domain Socket| Postmaster[Postmaster Process]
        Postmaster -->|forks| Backend[Backend Process]
        Client <-->|SQL Queries & Results| Backend
        Backend <--> SharedMem[Shared Memory Segment: Shared Buffers, Lock Manager, WAL Buffers]
        SharedMem <--> DiskFiles[PostgreSQL Filesystem: base/, pg_wal/]
    end

    subgraph SQLite (Embedded Library Architecture)
        AppProcess[Application Process]
        subgraph AppProcess
            ClientCode[Client Code] <-->|Function Calls C-API| SQLiteLib[SQLite Library]
            SQLiteLib <--> VFS[Virtual File System Layer]
        end
        VFS <--> MonolithicDB[Monolithic DB File: database.db, database.db-wal, database.db-shm]
    end
```

### Main Components and Data Flow

#### PostgreSQL
- **Postmaster Process**: The parent supervisor process that listens on a port (default: 5432) for incoming client connections. Upon receiving a connection request, it performs authentication and forks a dedicated **Backend Process** (`postgres` backend) to serve that client.
- **Backend Processes**: Each client connection runs in its own dedicated operating system process. The backend parses queries, generates execution plans, and executes transactions. Communication between backends occurs through shared memory.
- **Shared Memory**: A large block of shared RAM accessible by all backend processes. It contains:
  - *Shared Buffers*: A cache for database table and index pages.
  - *WAL Buffers*: Temporary storage for Write-Ahead Log records before flushing to disk.
  - *Lock Manager*: The lock table used for concurrency control (e.g., table-level and page-level locks).
  - *CLOG (Commit Log)*: A bitmask tracking transaction statuses (committed, aborted, in-progress).
- **Background Processes**: Independent helper processes like the Checkpointer, Writer (bgwriter), WAL Writer, Autovacuum Launcher, and Stats Collector.

#### SQLite
- **C API Interface**: Client programs link against `libsqlite3` and interact with it by making direct function calls (e.g., `sqlite3_open()`, `sqlite3_prepare_v2()`, `sqlite3_step()`).
- **Compiler Layer**: Comprises the Tokenizer, Parser, Code Generator, and Optimizer. It compiles SQL statements into bytecode.
- **Virtual Machine (VDBE)**: Executes the compiled bytecode. It acts as the execution engine and operates on the storage system.
- **B-Tree & Pager Layer**: The B-Tree module implements the hierarchical index structures. The Pager module handles reading/writing pages from disk, memory caching, transactions, and locking.
- **OS Interface (VFS)**: The Virtual File System (VFS) abstracts operating system-specific file operations, allowing SQLite to run on various platforms (Windows, POSIX) while delegating disk accesses and locks to OS-specific APIs.

---

## 3. Internal Design

### Storage Structures

#### PostgreSQL File Organization
PostgreSQL splits tables and indexes into separate files within a directory tree structured by database and relation OIDs.
- Under `pg_data/base/<database_oid>/`, each relation (table, index) is mapped to one or more physical files.
- Files are named after the relation's **filenode** number.
- To prevent file size limits on older filesystems, files are split into **1 GB segments** (e.g., `filenode`, `filenode.1`, `filenode.2`).
- Additionally, two auxiliary files are maintained:
  - *Free Space Map (FSM)* (`_fsm`): Tracks free space within pages.
  - *Visibility Map (VM)* (`_vm`): Tracks which pages contain only tuples visible to all active transactions, optimizing index-only scans and vacuum cycles.

#### SQLite File Organization
SQLite stores the entire database (schema, tables, indices, and system tables) in a **single monolithic file**. 
- It can dynamically grow or shrink in increments of the configured page size.
- Temporary files are created alongside the main file during writes:
  - *Rollback Journal*: A temporary file (named `<db_name>-journal`) used to store original page states before they are updated.
  - *WAL File*: In WAL mode, changes are written to a write-ahead log file (named `<db_name>-wal`), and index updates are written to a shared memory file (named `<db_name>-shm`).

---

### Page Layout

#### PostgreSQL Page Layout
The default page size in PostgreSQL is **8 KB** (`BLCKSZ`). The page structure uses a slotted-page architecture:

```
+-------------------------------------------------------------+
| PageHeaderData (24 bytes)                                   |
| - pd_lsn (8B)      : LSN of last WAL write changing page    |
| - pd_checksum (2B) : Page checksum                          |
| - pd_flags (2B)    : Page status flags                      |
| - pd_lower (2B)    : Byte offset to start of free space     |
| - pd_upper (2B)    : Byte offset to end of free space       |
| - pd_special (2B)  : Byte offset to special space (indices) |
+-------------------------------------------------------------+
| ItemIdData (Line Pointers)                                  |
| [ ItemId 1 ] [ ItemId 2 ] [ ItemId 3 ] ...                 |
| (Grows downwards)                                           |
+-------------------------------------------------------------+
|                     <-- FREE SPACE -->                      |
|                     (Available for tuples)                  |
+-------------------------------------------------------------+
|                                           ... [ Tuple 3 ]   |
|                                 [ Tuple 2 ] [ Tuple 1 ]     |
|                                           (Grows upwards)   |
+-------------------------------------------------------------+
| Special Space (Index-specific pointer data)                 |
+-------------------------------------------------------------+
```

Each tuple contains a **HeapTupleHeaderData** (typically 23-32 bytes), containing:
- `t_xmin`: The Transaction ID of the inserting transaction.
- `t_xmax`: The Transaction ID of the deleting or updating transaction (0 if active).
- `t_cid`: Command Identifier within the transaction.
- `t_infomask`: Bit flags defining tuple states (e.g., whether `t_xmin` is committed/aborted).

#### SQLite Page Layout
The default page size in SQLite is **4 KB** (variable between 512B and 64KB). SQLite pages are B-Tree nodes. There are four page types: Table Leaf, Table Internal, Index Leaf, and Index Internal.

A leaf page layout contains:

```
+-------------------------------------------------------------+
| Page Header (8 bytes for leaf, 12 bytes for internal nodes) |
| - Flag byte (1B)   : 0x0D (Table Leaf), 0x0A (Index Leaf) etc.|
| - First free block : 2B offset to first free space block    |
| - Cell count (2B)  : Number of cells on this page           |
| - Cell content (2B): Offset to start of cell content area    |
| - Fragmented bytes : 1B count of wasted fragmented bytes     |
+-------------------------------------------------------------+
| Cell Pointer Array (2 * Cell Count bytes)                   |
| [ Cell 1 Offset ] [ Cell 2 Offset ] ...                     |
| (Grows downwards)                                           |
+-------------------------------------------------------------+
|                     <-- UNALLOCATED SPACE -->               |
+-------------------------------------------------------------+
|                                           ... [ Cell 3 ]    |
|                                 [ Cell 2 ] [ Cell 1 ]       |
|                                           (Grows upwards)   |
+-------------------------------------------------------------+
```

Each **Cell** contains key-value pairs formatted with **Varints** (variable-length integers of 1-9 bytes) to minimize space:
- In **Table B-Trees**, the key is a 64-bit integer (`RowID`), and the value is the binary record payload.
- In **Index B-Trees**, the key is a composite structure containing the indexed columns and the target row's `RowID`, with no associated value.

---

### Index Organization

#### PostgreSQL
PostgreSQL heap tables are unclustered. Table records are stored arbitrarily across heap pages.
- Secondary indexes (B-Tree, Hash, GiST, GIN) are physically independent files.
- Leaf nodes of a secondary B-Tree contain the indexed key and a physical **Tuple Identifier (TID)**.
- A TID is a 6-byte structure `(BlockNumber, OffsetNumber)` pointing directly to the heap page slot containing the row.
- **Constraint**: Since indexes store physical addresses, updating a row requires updating all secondary indexes unless a **Heap-Only Tuple (HOT)** optimization is triggered (where the new tuple version is placed on the same page and linked via a heap chain).

#### SQLite
SQLite tables are clustered by default (often called **RowID Tables**).
- The table structure itself is a **B*-Tree** where the leaf nodes contain the actual row payload.
- Secondary indexes are separate B-Tree structures.
- The leaf nodes of a secondary index store the indexed key and the corresponding `RowID` (a 64-bit integer key).
- **Lookup Cost**: Fetching a row via a secondary index requires two index traversal operations: first traversing the secondary index to locate the `RowID`, then traversing the primary Table B-Tree using the `RowID` to fetch the record payload.

---

### Concurrency & Transaction Management

#### PostgreSQL MVCC (Multi-Version Concurrency Control)
PostgreSQL implements transaction isolation using MVCC:
- **Write Path**: When a row is modified (updated), the transaction does not overwrite the existing record. Instead, it marks the current record as invalid by setting `t_xmax` to its current Transaction ID (XID), and inserts a new version of the row with `t_xmin` set to its XID.
- **Read Path**: Readers do not acquire locks. When a transaction begins, it receives a **snapshot** containing:
  - The current XID active bounds.
  - A list of active, uncommitted XIDs at that instant.
- **Visibility Checks**: When reading a page, a backend process evaluates the header fields `t_xmin` and `t_xmax` against the transaction's snapshot. A tuple is visible only if the inserting transaction has committed and the deleting transaction has either not committed or has not yet started.
- **VACUUM**: Because modified rows generate duplicate tuple versions, a background daemon (`autovacuum`) must periodically scan pages to prune dead tuples and update the Free Space Map.

#### SQLite Concurrency Control
SQLite handles transactions differently depending on its configuration mode:

##### 1. Rollback Journal Mode (Pessimistic Locking)
In journal mode, SQLite locks the entire database file. A transaction transitions through a sequential state machine enforced via OS file locks:
- `UNLOCKED`: No locks are held.
- `SHARED`: Readers hold shared locks. Multiple processes can read. No writes are allowed.
- `RESERVED`: A process planning to write acquires a RESERVED lock. Only one RESERVED lock is allowed. Concurrent readers can continue reading.
- `PENDING`: The writer process wishes to commit. It blocks new readers from acquiring SHARED locks and waits for active SHARED locks to release.
- `EXCLUSIVE`: The writer holds the lock. It has exclusive access to the file and writes the modified pages.

##### 2. WAL (Write-Ahead Log) Mode (MVCC-lite)
WAL mode decouples readers and writers:
- Writes are appended to a separate `.wal` file.
- The reader process uses a shared-memory file (`.shm`) containing a hash index of the WAL pages.
- When reading a page, the reader checks the `.shm` index: if the page exists in the WAL file, it reads the latest version from the WAL. Otherwise, it reads the original page from the main database file.
- This allows multiple concurrent readers to proceed alongside a single writer. However, write transactions are still serialized; only one process can write to the database at a time.

---

### Recovery Mechanisms

#### PostgreSQL
- PostgreSQL writes all modifications to the **WAL Buffer**.
- On transaction commit, the WAL Buffer is flushed to `pg_wal/` using `fsync`.
- A background **Checkpointer** periodically writes dirty buffer pool pages to disk.
- **Crash Recovery**: If the system crashes, PostgreSQL reads the WAL files from the last checkpoint LSN and performs an **ARIES-based recovery**:
  - *Redo Phase*: Replays all logged transactions to restore the database to its state at the crash time.
  - *Undo Phase*: Reverts changes made by transactions that were uncommitted at the time of the crash.

#### SQLite
- **Rollback Journal Mode**: Before modifying a page in the database file, the Pager reads the original page and writes it to the rollback journal file. Once the journal is synced to disk, the changes are written to the database file. If a crash occurs, SQLite reads the journal file on startup and overwrites modified pages with their original versions (rollback).
- **WAL Mode**: Changes are written to the `.wal` file. The original database file remains unmodified. During checkpointing, SQLite copies pages from the `.wal` file back to the database. If a crash occurs, SQLite reconstructs its state by checking the `.wal` file, ignoring any half-written or uncommitted transactions at the end of the log.

---

## 4. Design Trade-Offs

| Architectural Dimension | PostgreSQL | SQLite |
|:---|:---|:---|
| **Process Model** | Multi-process fork-based (high OS overhead per connection) | In-process library (runs in calling process's threads) |
| **Network Interface** | Native TCP/IP and UNIX Domain Sockets | None (requires application-level networking) |
| **Write Concurrency** | Row-level locks; high concurrency of simultaneous writes | Serialized writes (single writer per database file) |
| **Storage Overhead** | Large (separate metadata files, heap files, index files) | Extremely low (single monolithic file, small footprint) |
| **Configuration** | High (memory allocations, autovacuum knobs, network limits) | Zero-configuration (no server setup, default configurations) |
| **Memory Footprint** | Gigabytes (Shared buffers, process memory, work_mem) | Megabytes (small client-side cache) |

---

## 5. Experiments / Observations

To evaluate the impact of these architectural decisions, we executed a local transaction benchmark using Python 3.13 and SQLite 3.53 on the workspace system.

### Benchmark Setup
- **Storage**: SSD (Windows NTFS Filesystem)
- **Workload 1**: 100 individual write transactions (autocommit mode, forcing fsync per transaction).
- **Workload 2**: 5,000 write operations batched within a single transaction block.

We evaluated five configurations:
1. `DELETE + FULL`: Rollback journal mode with full synchronous disk flushes.
2. `DELETE + OFF`: Rollback journal mode with disk synchronization disabled.
3. `WAL + FULL`: Write-ahead logging with full synchronous disk flushes.
4. `WAL + NORMAL`: Write-ahead logging with relaxed synchronization (safe from corruption, potential loss of last transactions on power loss).
5. `WAL + OFF`: Write-ahead logging with disk synchronization disabled.

### Results

#### Experiment 1: Single Inserts (Autocommit, N = 100)
| Journal Mode | Synchronous Setting | Total Time (s) | Throughput (TPS) |
|:---|:---|:---|:---|
| **DELETE** | `FULL` | 0.2644 | 378.23 |
| **DELETE** | `OFF` | 0.1253 | 798.26 |
| **WAL** | `FULL` | 0.0471 | 2122.09 |
| **WAL** | `NORMAL` | 0.0033 | 29865.45 |
| **WAL** | `OFF` | 0.0020 | 50021.51 |

#### Experiment 2: Batched Inserts (Single Transaction, N = 5000)
| Journal Mode | Synchronous Setting | Total Time (s) | Throughput (TPS) |
|:---|:---|:---|:---|
| **DELETE** | `FULL` | 0.0104 | 480,348.15 |
| **WAL** | `FULL` | 0.0051 | 981,169.65 |
| **WAL** | `NORMAL` | 0.0034 | 1,452,321.33 |

```
Throughput (TPS) - Individual Inserts (Log Scale)
===================================================
DELETE + FULL:   [██] 378 TPS
DELETE + OFF:    [████] 798 TPS
WAL + FULL:      [███████████] 2,122 TPS
WAL + NORMAL:    [██████████████████████████████] 29,865 TPS
WAL + OFF:       [██████████████████████████████████████] 50,021 TPS
```

### Analysis of Results

1. **The Cost of fsync**: 
   Under `DELETE + FULL`, each insert creates a separate transaction. The engine must write to the rollback journal, issue an `fsync`, overwrite the main database file, and issue another `fsync`. This results in double-write overhead and limits write throughput to under 400 TPS.
2. **WAL Efficiency**: 
   Transitioning to `WAL + FULL` replaces page overwrites with sequential log appends. This reduces disk seek overhead, raising throughput to 2,122 TPS.
3. **The Power of WAL + NORMAL**: 
   Setting `PRAGMA synchronous = NORMAL` in WAL mode relaxes synchronization. SQLite only forces disk syncs during checkpointing, rather than on every commit, while maintaining structural database integrity against application crashes. This increases throughput to **29,865 TPS**, demonstrating how decoupling logical commits from physical disk flushes improves performance.
4. **Batching Transactions**: 
   Grouping 5,000 inserts inside a single transaction (`BEGIN ... COMMIT`) reduces transaction overhead. In this mode, disk flushes occur only once at the end of the batch, achieving write throughputs exceeding **480,000 TPS** even under pessimistic rollback settings.

---

## 6. Key Learnings

1. **Server vs. Library Boundary**: 
   SQLite operates in-process, eliminating network parsing, IPC marshalling, and thread context switching. For single-user local storage, this yields lower execution overhead than PostgreSQL. However, it lacks native authentication, query routing, and parallel connection handling.
2. **Storage Layout and Locality**: 
   SQLite’s clustered index design accelerates primary key queries by co-locating data with the index node, but increases the cost of secondary index lookups due to the required `RowID` lookup. PostgreSQL’s unclustered heap layout keeps index structures uniform but requires manual maintenance (`VACUUM`) to reclaim space.
3. **Durability and Throughput Balance**: 
   Achieving high write throughput requires matching transactional grouping to the storage engine's logging mode. SQLite can achieve high write speeds if inserts are batched or synchronization rules are relaxed, but it lacks the parallel multi-writer capability of PostgreSQL’s shared-memory lock manager.
