# PostgreSQL vs SQLite: Architecture Comparison

## 1. Problem Background

### Why the Database Systems Exist
*   **PostgreSQL:** Designed to be a highly extensible, standards-compliant, and robust object-relational database management system. Its purpose is to handle complex workloads, high concurrency, and massive datasets reliably across a network.
*   **SQLite:** Created as a lightweight, serverless database engine. Its goal is to provide local data storage for applications without the administrative overhead of a standalone database server. It is the most widely deployed database engine globally, running on billions of mobile devices and embedded systems.

### What Problems They Were Designed to Solve
*   **PostgreSQL** solves the problem of coordinating simultaneous access by thousands of clients to a shared dataset securely, ensuring ACID guarantees even under intense concurrent writes.
*   **SQLite** solves the problem of storing structured data locally within an application footprint. It eliminates the need to configure network ports, manage user permissions, and run separate daemon processes just to save application state or user preferences.

---

## 2. Architecture Overview

### PostgreSQL: Client-Server Architecture
*   **Design:** A multi-process model. A master `postmaster` daemon listens on a network port. When a client connects, it forks a new, dedicated backend process to handle that client's requests.
*   **Data Flow:** Client -> Network Interface -> Postmaster -> Backend Process -> Shared Memory (Buffers) -> Disk.

### SQLite: Embedded Database Architecture
*   **Design:** An in-process library. There is no separate database process. The SQLite library is linked directly into the host application.
*   **Data Flow:** Application Function Call -> SQLite Library -> Virtual File System (VFS) -> Host OS File System.

---

## 3. Internal Design

### Process Model and Storage Engine
*   **PostgreSQL:** Uses a heavyweight process-per-connection model. The storage engine relies heavily on an internal Shared Buffer Pool, writing data pages asynchronously via background processes, and ensuring durability via Write-Ahead Logging (WAL).
*   **SQLite:** Shares the process space of the application. Its storage engine interacts directly with the OS file system cache. The entire database is typically stored as a single cross-platform file on disk.

### File Organization and Layout
*   **PostgreSQL:** Distributes a single database across many files and directories. Tables and indexes are stored in separate files (with files splitting when they reach 1GB).
*   **SQLite:** A single database corresponds to a single file on disk (e.g., `app.db`), formatted as a sequence of pages containing B-Trees for both tables and indexes.

### Transaction Management and Concurrency Control
*   **PostgreSQL (MVCC):** Implements Multi-Version Concurrency Control. Readers don't block writers, and writers don't block readers. This makes it ideal for mixed read/write workloads at scale.
*   **SQLite (File Locks / WAL):** Historically used OS-level file locking (exclusive locks for writes, shared locks for reads) meaning a write blocks all reads. Modern SQLite supports a WAL mode that allows concurrent reads while one write is happening, but it remains fundamentally limited to a single concurrent writer.

---

## 4. Design Trade-Offs

### Advantages
**PostgreSQL:**
*   Limitless concurrent read/write scalability.
*   Advanced security, roles, and network access control.
*   Support for complex analytics, JSONB, and PostGIS.

**SQLite:**
*   Zero configuration and administration.
*   Extremely small footprint; database is a single portable file.
*   Faster for simple queries since there is no inter-process communication (IPC) or network latency.

### Limitations
**PostgreSQL:**
*   High overhead. Requires dedicated memory and CPU resources.
*   Complex administration (tuning `postgresql.conf`, managing connections).

**SQLite:**
*   Single concurrent writer limitation.
*   No built-in network access; clients must access the file directly.
*   Limited complex analytical capabilities and no stored procedures.

### Why SQLite works well for mobile applications:
Mobile apps run in constrained, isolated environments where they only need to store data for a single user (themselves). SQLite provides structured storage with SQL capabilities directly within the app, avoiding network calls and minimizing battery consumption.

### Why PostgreSQL is preferred for large multi-user systems:
Enterprise systems require centralizing data from thousands of users simultaneously. PostgreSQL's process model, fine-grained locking, and MVCC ensure that a heavy analytical query doesn't prevent an e-commerce transaction from completing.

---

## 5. Experiments / Observations

### Workload Behavior
*   **High Concurrency Write Test:** If 100 threads attempt to insert data simultaneously, PostgreSQL will efficiently interleave the transactions using row-level locks. SQLite will serialize them; 99 threads will wait for the file lock, potentially throwing `SQLITE_BUSY` errors if the timeout is reached.
*   **Network vs Local Latency:** Executing `SELECT 1` 10,000 times will complete almost instantly in SQLite because it's a simple function call. In PostgreSQL, even on localhost, the TCP/IP stack and inter-process communication add measurable overhead.

---

## 6. Key Learnings

*   **Context Dictates Architecture:** Neither database is "better." PostgreSQL is built to manage the chaos of a network; SQLite is built to manage local files gracefully.
*   **The Cost of Concurrency:** MVCC is powerful but complex and resource-heavy. SQLite's decision to use coarse file-level locks is a brilliant simplification that meets 99% of its target use cases perfectly.
*   **The Power of In-Process:** Eliminating the network and IPC layers yields massive performance benefits for local, single-user tasks.
