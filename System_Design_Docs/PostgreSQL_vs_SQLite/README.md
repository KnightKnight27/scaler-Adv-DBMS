# System Design Discussion: PostgreSQL vs SQLite3 Architecture Comparison

**Name:** Sameer Khan S  
**Roll Number:** 24BCS10245  
**Course:** Advanced Database Management Systems (Advanced DBMS)  
**Submission Date:** 18 May 2026  

---

## 1. Problem Background

### SQLite: The Embedded Database
SQLite was designed in 2000 by D. Richard Hipp to address the need for a serverless, zero-configuration SQL database engine. Prior to SQLite, running a relational database required installing, configuring, and maintaining a server process (such as Oracle or PostgreSQL), which was highly impractical for:
*   Embedded systems and mobile devices.
*   Desktop applications requiring local file storage.
*   Testing and local development environments.

SQLite solves this by compiling directly into the host application, operating as an in-process library that reads and writes directly to a single file on disk.

### PostgreSQL: The Enterprise Object-Relational Database
PostgreSQL (originally POSTGRES) originated from UC Berkeley in 1986 under the direction of Michael Stonebraker. It was designed from day one to be an enterprise-class, extensible database server capable of handling highly concurrent workloads, complex transactions, and custom data types. 

PostgreSQL addresses the needs of distributed, multi-user applications where:
*   Multiple remote client processes write to a shared database simultaneously.
*   Data integrity, strict isolation levels, and comprehensive access control are mandatory.
*   The system must scale horizontally and vertically across high-performance servers.

---

## 2. Architecture Overview

### High-Level Component Comparison

```mermaid
graph TD
    subgraph Client-Server (PostgreSQL)
        Client[Client App/CLI] <-->|TCP/Unix Socket| PG_Server[PostgreSQL Server Daemon]
        PG_Server <--> SharedMemory[Shared Buffers / Mem]
        PG_Server <--> BackgroundWorkers[checkpointer / background writer / walwriter]
        PG_Server <--> DiskStore[Postgres Data Directory]
    end

    subgraph Embedded (SQLite)
        App[Client Application Process] -->|Direct Call| SQLiteLib[libsqlite3.so / In-Process]
        SQLiteLib <--> OS_VFS[OS File System / VFS]
        OS_VFS <--> SingleFile[Single .db File]
    end
```

### PostgreSQL Process Model
PostgreSQL operates as a **Process-per-Connection** client-server architecture:
*   A master daemon process (`postmaster`) listens on port 5432.
*   When a client connects, `postmaster` forks a dedicated `postgres` backend process for that connection.
*   These backend processes communicate using IPC via Shared Memory (e.g., Shared Buffers, Lock Manager).
*   Dedicated background utility processes (`checkpointer`, `walwriter`, `bgwriter`, `autovacuum launcher`) handle system-wide tasks asynchronously.

### SQLite Process Model
SQLite is **Serverless and In-Process**:
*   There are no daemon processes, background processes, or port listeners.
*   The entire database engine runs inside the host application's address space.
*   Database calls are direct C function calls inside the application process.
*   File locking APIs provided by the Operating System (VFS layer) coordinate concurrency between multiple application processes accessing the same database file.

---

## 3. Internal Design

### 3.1 Storage & File Organization
*   **SQLite:** The entire database—including tables, indexes, schema metadata, and B-Trees—is serialized into a **single file** on disk (e.g., `students.db`). 
*   **PostgreSQL:** Uses a **Data Directory** (`PGDATA`). Each database is mapped to a subdirectory, and each table and index is stored in one or more separate files (split into 1GB segments called "relation segments") inside a directory structure based on the relation's Object ID (OID).

### 3.2 Page & Disk Layout
*   **Page Sizes:** SQLite defaults to **4096 bytes** (matching the OS virtual memory page size). PostgreSQL defaults to **8192 bytes** (8KB blocks) to optimize throughput for larger disk reads.
*   **Page Layout:**
    *   **SQLite Pages:** Contain a page header followed by a cell pointer array growing downwards, and the cell payloads (tuple data) growing upwards from the bottom of the page.
    *   **PostgreSQL Pages:** Use a slotted-page architecture. The page header is at the top, followed by line pointers (`ItemIdData`) growing downwards, and tuple data (`HeapTupleHeader` + data payload) growing upwards from the end of the page.

### 3.3 Index Implementation
*   **SQLite:** Uses a **B-Tree** structure for table organization (tables are B-Trees keyed on the internal `rowid`) and a **B+-Tree** structure for secondary indexes (storing key value → `rowid`).
*   **PostgreSQL:** Uses a **Heap-file + B-Tree** structure. Tables are heap files where rows are placed arbitrarily. Indexes are separate B-Tree structures containing keys that point to tuple physical identifiers (`TID` / CTID) inside the heap.

### 3.4 Concurrency Control & Transactions
*   **SQLite (Locking-based):** SQLite uses file-level locking. In standard rollback journal mode, it allows multiple concurrent readers but blocks all readers when a writer is active. In Write-Ahead Log (WAL) mode, it allows **one writer and multiple concurrent readers** simultaneously.
*   **PostgreSQL (MVCC-based):** PostgreSQL uses Multi-Version Concurrency Control. Writers do not block readers, and readers do not block writers. Every update writes a new version of the tuple (an append-only design), and older versions are garbage-collected via the `VACUUM` daemon.

---

## 4. Design Trade-Offs

| Design Dimension | SQLite (Embedded) | PostgreSQL (Client-Server) |
| :--- | :--- | :--- |
| **Write Concurrency** | Low (File-level locking restricts writes to a single concurrent process). | High (Row-level locking and MVCC support thousands of simultaneous writes). |
| **Setup & Overhead** | Zero configuration; lightweight; zero resource footprint when idle. | High setup overhead; requires system resources (RAM, process limits) constantly. |
| **Network Latency** | Zero (in-memory/local disk access via direct function calls). | Network roundtrip overhead (TCP packets exchanged for every query execution). |
| **Data Integrity** | Basic filesystem security permissions only. | Enterprise roles, SSL connection encryption, row-level security (RLS). |

---

## 5. Experiments & Observations

To evaluate how these architectural differences manifest in execution performance, the following experiments were conducted on an Ubuntu environment running inside WSL2.

### 5.1 SQLite3 Exploration & mmap Testing
A database `students.db` was populated with user data. The default page size and page count were audited using PRAGMA:

```sql
PRAGMA page_size;   -- Output: 4096 (4KB page size)
PRAGMA page_count;  -- Output: 2 (Total DB size = 8KB)
```

The database was queried under two memory-mapping profiles:
1.  **Direct Read I/O (Default):** `PRAGMA mmap_size = 0;`
2.  **Memory-Mapped I/O:** `PRAGMA mmap_size = 30000000;` (30MB virtual space allocation)

#### Execution Timings:
*   **Standard I/O Execution:**
    ```bash
    time sqlite3 students.db "SELECT * FROM users;"
    # Output: real 0.003s, user 0.000s, sys 0.003s
    ```
*   **mmap-enabled Execution:**
    ```bash
    time sqlite3 students.db "PRAGMA mmap_size=30000000; SELECT * FROM users;"
    # Output: real 0.002s, user 0.002s, sys 0.001s
    ```

**Observation:** Memory-mapping bypasses read syscall context-switching. For extremely small workloads, the execution time decreases marginally (from `3ms` to `2ms`), but the system CPU overhead (`sys`) decreases from `3ms` to `1ms`, proving that mmap shifts the I/O burden from kernel-space syscalls to user-space page faults managed by the OS virtual memory manager.

### 5.2 PostgreSQL Execution & Architecture Inspection
In PostgreSQL, a table `users` was analyzed:

```sql
SHOW block_size; -- Output: 8192 (8KB block size)
```

Unlike SQLite, querying the system catalogs for page layout metrics returns estimates until statistics are refreshed:

```sql
SELECT relpages FROM pg_class WHERE relname='users'; -- Output: 0
ANALYZE users;
SELECT relpages FROM pg_class WHERE relname='users'; -- Output: 1
```

#### Process Footprint Observation:
Inspecting running processes during PostgreSQL execution exposes the full daemon-based process model:
```bash
ps aux | grep postgres
```
**Utility Daemons Observed:**
*   `postgres: checkpointer` (forces dirty buffers to disk at checkpoints)
*   `postgres: walwriter` (writes WAL logs sequentially from memory)
*   `postgres: autovacuum launcher` (runs asynchronous vacuum tasks to clean dead MVCC tuples)

---

## 6. Key Learnings

1.  **Architecture Governs Use-Case:** SQLite's serverless design eliminates network overhead, making it exceptionally fast for single-user scenarios (e.g. mobile apps, IoT devices, desktop storage), while PostgreSQL's client-server architecture is optimized to sustain concurrent workloads through dedicated lock managers and utility processes.
2.  **Stat Gathering Differences:** PostgreSQL's planner relies heavily on system-level statistics collected via `ANALYZE` (visible in `pg_statistic`), whereas SQLite utilizes a simpler, on-the-fly approach for its query planner.
3.  **MVCC Cleanup Overhead:** PostgreSQL's MVCC creates dead tuples on every update/delete, which requires the active overhead of `VACUUM` processes to reclaim space, whereas SQLite's standard rollback journal mode relies on in-place file modifications with write-blocking locks.
