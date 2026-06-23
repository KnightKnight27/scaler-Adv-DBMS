# MiniDB: System Architecture

This document describes the high-level architecture, module responsibilities, page memory layout, and data flow of **MiniDB** — an embedded relational database engine.

---

## 1. High-Level Architecture

MiniDB is designed as an embedded database library (similar to SQLite) that runs within the same process and address space as the host application. It is composed of six major subsystems:

```
              ┌────────────────────────────────────────┐
              │          SQL Query String              │
              └───────────────────┬────────────────────┘
                                  ▼
              ┌────────────────────────────────────────┐
              │          Parser (AST Gen)              │
              └───────────────────┬────────────────────┘
                                  ▼
              ┌────────────────────────────────────────┐
              │       Cost-Based Optimizer             │
              │  (Estimates: Table Scan vs Index Scan) │
              └───────────────────┬────────────────────┘
                                  ▼
              ┌────────────────────────────────────────┐
              │          Execution Engine              │
              │     (Volcano Iterator Interface)       │
              └─────────┬───────────────────┬──────────┘
                        │                   │
                        ▼                   ▼
              ┌──────────────────┐ ┌──────────────────┐
              │TransactionManager│ │  LockManager     │
              │ (MVCC & Snapshots│ │  (Strict 2PL +   │
              │   Visibility)    │ │ Waits-For Graph) │
              └─────────┬────────┘ └─────────┬────────┘
                        │                   │
                        ▼                   ▼
              ┌────────────────────────────────────────┐
              │         Buffer Pool Manager            │
              │    (Clock Sweep Eviction Policy)       │
              └───────────────────┬────────────────────┘
                                  ▼
              ┌────────────────────────────────────────┐
              │          Disk Storage Manager          │
              │       (Heap Files, Pages, WAL)         │
              └────────────────────────────────────────┘
```

---

## 2. Components & Responsibilities

### A. Disk Storage Manager (`DiskManager`)
*   **Physical I/O**: Reads and writes fixed-size physical blocks (4 KB pages) using low-level POSIX I/O calls (`open`, `pread`, `pwrite`) with cache bypassing flags (`O_DIRECT` on Linux, `F_NOCACHE` on macOS). Ensures user-space page buffers are page-aligned in memory (e.g., allocated using `aligned_alloc` or `posix_memalign`).
*   **Write-Ahead Log (WAL)**: Records transaction mutations sequentially to a log file (`minidb.log`) and uses `fsync()` to ensure durability on commits before returning.
*   **Recovery Management**: During startup, performs ARIES-based recovery consisting of a REDO pass and an UNDO pass. Utilizes `page_lsn` in the page headers to skip redundant REDO applications, guaranteeing recovery idempotency.

### B. Buffer Pool Manager (`BufferPoolManager`)
*   **Caching**: Manages an in-memory cache of page frames in RAM, preventing frequent disk access. In alignment with class notes, the buffer pool is instanced per connection, rather than globally shared.
*   **Clock Sweep Eviction**: Employs the Clock Sweep (page-replacement) algorithm to select victim pages for eviction. Tracks `usage_count` and `pin_count` (pinned pages cannot be evicted).
*   **Concurrency**: Access is serialized at the connection level via an internal connection-level mutex in RAM. Since buffer pools are not shared across database connections, cross-connection concurrent access on the buffer pool is avoided.

### C. Indexing Subsystem (`BTreeIndex`)
*   **Data Structure**: Implements an on-disk B+ Tree where nodes are stored within 4 KB pages, represented as packed structs (using `[[gnu::packed]]`) to prevent layout drift. Child pointers are represented as page IDs (`page_id_t`) rather than memory pointers.
*   **Degree Parameter ($t$)**: Node keys and child pointers are bounded by degree limits ($t - 1$ to $2t - 1$ keys).
*   **Fast Lookups**: Resolves queries like `WHERE id = X` in $O(\log_t N)$ page seeks, bypassing full table scans.
*   **Zero-Serialization**: Copying keys/values to and from raw page frames is done using `std::memcpy` into aligned variables to prevent strict-aliasing violations and memory alignment faults (bus errors) on CPU architectures like ARM64.
*   **Concurrency**: Following class material, the B+ Tree does not implement latch crabbing or internal node locks; thread safety is transitively guaranteed because indexing operations execute under the protection of the connection-level mutex or transaction row-level locks.

### D. Concurrency & Transaction Manager (`TransactionManager` & `LockManager`)
*   **Transaction Table**: Tracks transaction states (`ACTIVE`, `COMMITTED`, `ABORTED`) and issues transaction IDs ($TxID$).
*   **MVCC Visibility**: Enforces Snapshot Isolation. Every row write generates a new version containing `xmin` (creator ID) and `xmax` (deleter ID). Visibility check walks the version chain based on the reading transaction's snapshot.
*   **Lock Manager (Strict 2PL)**: Coordinates logical row-level Shared (S) and Exclusive (X) locks keyed on `RowKey` (logical record identifiers, as practiced in Lab 6) to serialize write-write conflicts. Readers read snapshots without locking (avoiding read-write contention).
*   **Deadlock Detection**: Builds a Waits-For graph of transaction blockages. Runs a depth-first search (DFS) cycle check, aborting the younger transaction on cycle detection.

### E. Query Parser & Execution Engine (`Parser` & `Executor`)
*   **Parser**: Parses input SQL into an Abstract Syntax Tree (AST).
*   **Volcano Iterator**: Implements query execution operators (`SeqScan`, `IndexScan`, `Filter`, `Join`, `Insert`, `Delete`) using an iterator interface (`init()`, `next()`, `close()`).
*   **Cost-Based Optimizer**: Performs cardinality/selectivity estimation to select the most efficient access path (Index Scan vs. Table Scan) and determines join ordering.

---

## 3. Data Flows

### A. Read Operation (Query Scan)
1. Application calls a SQL query (e.g. `SELECT name FROM users WHERE id = 10;`).
2. Parser builds the AST. The Optimizer selects an `IndexScan` plan (via the B+ Tree index).
3. The Executor requests the index page from the `BufferPoolManager`.
4. The B+ Tree is traversed to fetch the target Tuple ID / RID `(Page ID, Slot ID)`.
5. The Executor requests the target heap page. The `TransactionManager` runs the **MVCC Visibility Check** on the row version chain, returning the version visible to the transaction snapshot.

### B. Write Operation (Journey of an Update)
1. Execution engine issues an update query.
2. The `LockManager` acquires an Exclusive (X) Lock on the target key.
3. The `TransactionManager` starts a new version, setting the older visible version's `xmax = TxID` and appending the new version with `xmin = TxID, xmax = 0` to the version chain.
4. The page is marked **dirty** in the `BufferPoolManager`.
5. A **physiological log record** (containing `Page ID`, `Slot ID`, `TxID`, and page diff bytes) is written to the WAL.
6. Upon transaction commit, the WAL is flushed to disk via `fsync()`, and the locks are released (Strict 2PL).

---

## 4. Database Page & Heap File Design

Every heap page inside the database file is exactly **4096 bytes** (4 KB) to align with standard OS page size, eliminating ARM64/x86 alignment overhead. 

```
┌─────────────────────────────────────────────────────────────────────────┐
│ PAGE HEADER (24 bytes)                                                  │
│ - Checksum (4B) | Slot Count (4B) | Free Space Pointer (4B) | LSN (8B)  │
├─────────────────────────────────────────────────────────────────────────┤
│ SLOT ARRAY (Grows downward)                                             │
│ - Slot[0]: Offset (2B) | Length (2B)                                    │
│ - Slot[1]: Offset (2B) | Length (2B)                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                         FREE SPACE REGION                               │
├─────────────────────────────────────────────────────────────────────────┤
│ ROW/RECORD STORAGE (Grows upward)                                       │
│ - Row 1 [xmin (8B) | xmax (8B) | prev_version_tid (8B) | data]          │
│ - Row 0 [xmin (8B) | xmax (8B) | prev_version_tid (8B) | data]          │
└─────────────────────────────────────────────────────────────────────────┘
```

*   **Page Header**: Tracks checksum, slot count, free space offset, and page Log Sequence Number (LSN).
*   **Slot Array**: Growing top-down, maps slot index to record offsets.
*   **Free Space**: Middle region between the slot array and record storage.
*   **Row Storage**: Growing bottom-up, holds the actual raw bytes of records prepended with version headers (`xmin`, `xmax`, and the version chain pointer).

---

## 5. Architectural Trade-offs

*   **Embedded library vs. Server Process**:
    *   *Decision*: Built as an embedded library (like SQLite) linked directly into the binary. Concurrency is handled at the connection level via connection-level mutexes, and each connection receives an independent page buffer pool in RAM.
    *   *Trade-off*: Eliminates TCP sockets, socket multiplexing, and network latency, significantly accelerating development. However, database lifetime is bound to the host process, and buffer pools are not shared between connections.
*   **Physiological ARIES WAL Logging**:
    *   *Decision*: Log records store physiological information combined with a page-level `page_lsn` in the page headers to ensure recovery idempotency.
    *   *Trade-off*: Guarantees crash recovery correctness even during recursive crashes during recovery, but requires extra byte tracking in the physical page header.
*   **MVCC + Logical 2PL write locking**:
    *   *Decision*: Readers read snapshots without acquiring locks. Write-write conflicts are resolved by acquiring Exclusive (X) locks on logical row keys (`RowKey`) under Strict 2PL.
    *   *Trade-off*: Eliminates read-write contention entirely. However, concurrent updates to the same logical row block and can trigger deadlocks, requiring Waits-For graph cycle resolution.
