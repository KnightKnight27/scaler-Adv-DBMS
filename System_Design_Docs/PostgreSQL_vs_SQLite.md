# PostgreSQL vs SQLite Architecture Comparison

## 1. Problem Background

### SQLite
SQLite was created in 2000 by D. Richard Hipp while working for the US Navy on board a guided-missile destroyer. The primary design goal was to create a database system that required zero administration ("zero-config"), could run directly inside an application program without a standalone server daemon, and would be extremely reliable even under power loss or system crashes. SQLite is designed as an **embedded database** to be integrated directly into applications.

### PostgreSQL
PostgreSQL (originally POSTGRES) was started at UC Berkeley by Professor Michael Stonebraker in 1986. The database was designed to address limitations of contemporary relational systems (such as Ingres). The objective was to support object-relational extensions, high concurrency, complex data types, and enterprise-grade reliability. PostgreSQL is designed as a **client-server database** to support many concurrent users querying a central repository over a network.

---

## 2. Architecture Overview

### High-Level Architecture Diagram

```mermaid
graph TD
    subgraph Client-Server (PostgreSQL)
        Client[Application Client] <-->|IPC / Network TCP/IP| Postmaster[Postgres Master Process]
        Postmaster -->|Forks| Backend[Backend Worker Process]
        Backend <-->|Shared RAM Buffer Pool| SharedBuffers[(Shared Buffers 128MB)]
        Backend <-->|Write-Ahead Logging| WAL[WAL Files]
        Backend <-->|Reads/Writes Pages| PGDisk[(Postgres Data Files)]
    end

    subgraph Embedded (SQLite)
        AppProcess[Application Process]
        subgraph SQLite Library
            AppLogic[App Logic] <--> SQLCompiler[SQL Parser/Compiler]
            SQLCompiler <--> VDBE[Virtual Database Engine]
            VDBE <--> Pager[Pager & Cache Manager]
            Pager <--> BTree[B-Tree Module]
        end
        AppProcess <-->|OS System Calls / mmap| SingleFile[(single-file database: sample.db)]
    end
```

### Main System Components

* **PostgreSQL:**
  - **Postmaster (Daemon Process):** Listens for incoming connections, performs authentication, and forks a new backend worker process for each connection.
  - **Backend Worker Processes:** Handle client queries, manage query optimization, planning, and lock acquisition.
  - **Shared Buffers:** PostgreSQL's own page cache in RAM.
  - **Background Writers (checkpointer, walwriter, autovacuum):** Dedicated processes running in the background to handle checkpoints, WAL logging, and vacuuming.
* **SQLite:**
  - **SQL Compiler (Tokenizer, Parser, Code Generator):** Compiles SQL queries into Virtual Machine bytecode.
  - **VDBE (Virtual Database Engine):** Executes the compiled bytecode to retrieve or mutate records.
  - **B-Tree & Pager Layer:** Organizes data pages as B-Tree structures and handles disk I/O, paging, and transactions (using rollback journal or WAL).

---

## 3. Internal Design

### Storage Structures & Page Layout
- **PostgreSQL:** Uses a default **8 KB page size** (configurable only during compile-time). Pages contain a header, a line pointer array (pointing to actual tuples), and the tuple data growing upwards from the bottom of the page. This structure supports MVCC by storing multiple tuple versions (with `xmin` and `xmax` fields) directly on the heap pages.
- **SQLite:** Uses a default **4 KB page size** (runtime configurable). The database file is structured as a sequence of pages, where Page 1 contains the file header. Tables are stored using B-Trees (payload in leaf nodes) or B+ Trees (data only in leaf nodes, interior nodes contain keys).

### Transaction Processing & Concurrency Control
- **PostgreSQL:** Fully implements Multi-Version Concurrency Control (MVCC). Readers do not block writers, and writers do not block readers. It supports standard SQL transaction levels (Read Committed by default, up to Serializable Snapshot Isolation).
- **SQLite:** Traditionally used a readers/writer lock on the single database file, meaning a single writer blocks all readers. In **WAL mode**, SQLite supports concurrent readers alongside a single writer. It achieves ACID compliance via a rollback journal or write-ahead logging (WAL).

---

## 4. Design Trade-Offs

| Trade-Off Vector | SQLite (Embedded) | PostgreSQL (Client-Server) |
| :--- | :--- | :--- |
| **Concurrency** | Single writer blocks others; limited write concurrency. | High concurrent read/write capacity via MVCC and fine-grained locking. |
| **Administration** | Zero administration, zero setup overhead. | Requires management (installation, user accounts, tuning, backups, vacuuming). |
| **Data Integrity** | Weak typing (manifest typing allows arbitrary data in columns). | Strong typing, strict check constraints, foreign keys, domains. |
| **Deployment** | Database resides inside the host application memory space. | Runs as a separate network-accessible host or cluster of machines. |
| **Query Engine** | Lightweight VM execution, optimized for fast simple lookups. | Heavyweight cost-based planner, supports advanced indexing (GIN, GiST, BRIN), parallel scans, JIT. |

---

## 5. Experiments / Observations

Below are practical observations collected on a macOS (Apple Silicon - aarch64) system using a sample dataset of **10,000 rows** (`users` table: ID, Name, Email, Age, City, Created_At).

### 5.1 Storage Footprint
- **SQLite:** 
  - Database file size: **680 KB**
  - Page size: **4,096 bytes** (170 pages total)
- **PostgreSQL:** 
  - Table size: **848 KB** (106 pages at 8,192 bytes page size)
  - Total size with primary key index: **1,120 KB**

*Observation:* PostgreSQL carries higher storage overhead (~65% more) because it stores tuple headers for MVCC, transaction state trackers, and indexes in separate files, whereas SQLite packs everything into one single file with minimal headers.

### 5.2 Query Latency Comparison (10K Rows)

| Query Type | SQLite3 | PostgreSQL (Client-Visible) | PostgreSQL (Internal Exec) |
| :--- | :--- | :--- | :--- |
| `SELECT * FROM users;` (Full Scan) | **7.0 ms** | **9.81 ms** | **0.50 ms** |
| `SELECT COUNT(*);` (Mumbai) | **1.0 ms** | **2.02 ms** | **0.25 ms** |
| `SELECT * WHERE id = 5000;` (PK Lookup) | **<1.0 ms** | **0.85 ms** | **0.17 ms** |

*Observation:* While PostgreSQL's internal execution times are extremely fast (e.g. 0.50 ms for full table scan), SQLite shows slightly lower client-visible latency for simple queries. This is due to **zero Inter-Process Communication (IPC)** overhead in SQLite—queries run directly in the application thread.

---

## 6. Key Learnings

1. **Architecture Dictates Overhead:** SQLite's direct function-call access is faster for small, single-application tasks because it completely bypasses networking and process context-switching.
2. **Page Size Strategy:** PostgreSQL uses a larger page size (8 KB) to reduce directory traversal overhead and optimize page alignments for disk controllers, while SQLite's 4 KB page size is optimal for low-memory environments (embedded, mobile).
3. **MVCC vs In-Place Updates:** PostgreSQL's MVCC design requires vacuuming to clean up dead rows but achieves massive concurrency, whereas SQLite uses rollback logs/WAL which are lightweight but restrict write scalability.
