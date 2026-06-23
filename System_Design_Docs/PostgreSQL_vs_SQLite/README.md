# PostgreSQL vs SQLite: Architecture Comparison

## 1. Problem Background

### Why the Database Systems Exist
*   **PostgreSQL:** Built to serve as a feature-rich, standards-compliant relational system capable of running complex workloads with high concurrency and strong consistency, all accessible over a network.
*   **SQLite:** Designed as a minimal, serverless library that lets applications store structured data locally without the operational burden of a separate database server. It runs on billions of devices worldwide.

### What Problems They Were Designed to Solve
*   **PostgreSQL** coordinates concurrent access from potentially thousands of clients to a shared data set while preserving ACID properties under heavy write contention.
*   **SQLite** eliminates the need for network configuration, user management, and background daemons when an application simply needs persistent local storage for its own state.

---

## 2. Architecture Overview

### PostgreSQL: Client-Server Architecture
*   **Design:** A multi-process model. The `postmaster` daemon listens on a TCP port and forks a dedicated backend OS process for each connected client.
*   **Data Flow:** Client → Network Transport → Postmaster → Backend Process → Shared Buffers → Disk I/O.

### SQLite: Embedded Database Architecture
*   **Design:** An in-process library compiled directly into the host application. There is no separate server process.
*   **Data Flow:** Application Function Call → SQLite Library → Virtual File System (VFS) → OS File System.

---

## 3. Internal Design

### Process Model and Storage Engine
*   **PostgreSQL:** Uses a process-per-connection model. Its storage engine depends on a shared buffer pool shared across processes, writes pages asynchronously through background workers, and guarantees durability through WAL.
*   **SQLite:** Executes inside the application's own process space. Its storage engine relies on the OS file-system cache, and the entire database lives in a single cross-platform file.

### File Organization and Layout
*   **PostgreSQL:** Spreads a database across many files. Each table and index occupies its own file, and files are split at 1 GB boundaries.
*   **SQLite:** Stores the whole database in one file (e.g., `app.db`). The file is a sequence of pages that contain B-Trees for both table data and indexes.

### Transaction Management and Concurrency Control
*   **PostgreSQL (MVCC):** Multi-version concurrency control ensures readers never block writers and writers never block readers, making it well suited for mixed read-write workloads at scale.
*   **SQLite (File Locks / WAL):** Earlier versions used OS-level file locks—exclusive for writes, shared for reads—so a write blocked all reads. Modern SQLite offers WAL mode, which allows concurrent reads during a single write, but still limits the system to one writer at a time.

---

## 4. Design Trade-Offs

### Advantages
**PostgreSQL:**
*   Scales to very high read/write concurrency.
*   Offers robust security features: roles, network-level access control, SSL.
*   Supports advanced querying via JSONB, full-text search, PostGIS, and window functions.

**SQLite:**
*   Requires no setup or administration beyond shipping a library.
*   Has a tiny footprint; a database is just a single portable file.
*   Performs simple queries faster because there is no inter-process communication or network stack.

### Limitations
**PostgreSQL:**
*   Resource-intensive—needs dedicated memory, CPU, and ongoing configuration tuning.
*   Administration complexity grows with the deployment (connection pooling, replication setup, vacuum tuning).

**SQLite:**
*   Only one writer can be active at any moment.
*   No native network access; clients must share the file directly.
*   Lacks stored procedures, analytical window functions, and other enterprise features.

### Why SQLite works well for mobile applications:
Mobile environments are resource-constrained and serve a single user. SQLite gives apps full SQL capability locally, avoiding network calls and reducing power consumption.

### Why PostgreSQL is preferred for large multi-user systems:
Centralizing data from many concurrent users demands fine-grained locking and robust concurrency control. PostgreSQL's MVCC and process model prevent a heavy analytical query from blocking a short OLTP transaction.

---

## 5. Experiments / Observations

### Workload Behavior
*   **High Concurrency Write Test:** With 100 threads inserting data simultaneously, PostgreSQL interleaves the transactions using row-level locks efficiently. SQLite serializes all writers; 99 threads wait on the file lock and may time out with `SQLITE_BUSY`.
*   **Network vs Local Latency:** Running `SELECT 1` ten thousand times finishes almost instantly in SQLite because it is just a function call. Even on localhost, PostgreSQL incurs TCP/IP and process-communication overhead that adds measurable latency.

---

## 6. Key Learnings

*   **Use Case Determines Choice:** Neither system is universally superior. PostgreSQL is engineered for networked multi-user environments; SQLite is optimized for self-contained local storage.
*   **Concurrency Has a Cost:** Full MVCC is complex and resource-hungry. SQLite's coarse file-level locking is a pragmatic simplification that suits the vast majority of its target deployments.
*   **In-Process Speed Is Hard to Beat:** Removing IPC and network layers gives SQLite a dramatic latency advantage for local single-user workloads.
