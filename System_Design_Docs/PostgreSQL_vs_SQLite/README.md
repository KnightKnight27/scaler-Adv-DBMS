# PostgreSQL vs. SQLite Architecture Comparison

## 1. Problem Background

Database systems are designed based on the environment they are intended to run in. PostgreSQL and SQLite represent two completely different design choices:

*   **PostgreSQL**: Built as a client-server relational database. It is designed to run on a dedicated server and handle concurrent queries from multiple users over a network. It focuses on scalability, high concurrency, and complete SQL features.
*   **SQLite**: Built as an embedded database library. It does not run as a separate server process. Instead, it is linked directly into the application. It treats the database as a single file on disk and is optimized for local storage, simplicity, and low memory usage.

---

## 2. Architecture Overview

The core difference is client-server (multi-process) vs. embedded (in-process) design.

```text
[ PostgreSQL: Client-Server ]            [ SQLite: Embedded ]

  +---------+     +---------+               +------------------+
  | Client  |     | Client  |               |  App Process     |
  +---------+     +---------+               |  +------------+  |
       | TCP           | TCP                |  | App Code   |  |
       v               v                    |  +------------+  |
  +-------------------------+               |       | Call     |
  |       Postmaster        |               |       v          |
  +-------------------------+               |  +------------+  |
    | forks           | forks               |  | SQLite Lib |  |
    v                 v                     |  +------------+  |
  +---------+     +---------+               +------------------+
  | Backend |     | Backend |                        |
  +---------+     +---------+                        v
       |               |                    +------------------+
       +-------+-------+                    | Single .db File  |
               |                            +------------------+
               v
  +-------------------------+
  | Shared Memory Buffers   |
  +-------------------------+
               |
               v
  +-------------------------+
  |  Heap & Index Files     |
  +-------------------------+
```

*   **PostgreSQL**: Clients send queries over a network. The main Postmaster process listens for connections and forks a new backend worker process for each client. These backends coordinate using a shared memory buffer pool.
*   **SQLite**: The database engine is a library inside the host application. Executing a query is just a function call. The library handles its own cache and writes directly to the database file.

---

## 3. Internal Design

### 3.1 Process Model
*   **PostgreSQL**: Uses a multi-process model. Every client connection gets its own dedicated server process. Communication between processes happens through Shared Memory and semaphores.
*   **SQLite**: Single-process. Runs entirely inside the application's process. It uses OS-level file locks to synchronize access if multiple processes open the same database file.

### 3.2 File and Storage Organization
*   **PostgreSQL**: Divides data into multiple files under the database directory. Every table and index gets its own file. Data is stored in unordered heap files, and indexes point to these heap locations.
*   **SQLite**: Stores the entire database (tables, indexes, schema) in a single `.db` file. Tables are B-Trees sorted by row ID (or primary key).

### 3.3 Page Layout
Both databases write data in fixed-size blocks called pages, but organize them differently:

*   **PostgreSQL (8KB pages)**: Consists of a page header at the top, line pointers growing downwards, free space in the middle, and actual tuples (rows) growing upwards from the bottom.
*   **SQLite (4KB pages)**: Consists of a page header, a cell pointer array growing downwards, free space in the middle, and cells (records) growing upwards from the bottom.

### 3.4 Index Implementation
*   **PostgreSQL**: Uses secondary B-Tree indexes. The leaf nodes contain Tuple IDs (TIDs) that point to the physical block and offset of the row in the heap file.
*   **SQLite**: The table itself is a B+Tree. Lookups by primary key directly find the row data. Secondary indexes are separate B-Trees where the leaves store the `rowid` of the matching row.

### 3.5 Concurrency and Transactions
*   **PostgreSQL**: Uses Multi-Version Concurrency Control (MVCC). Instead of locking rows on updates, it creates new row versions (marked with transaction IDs `xmin`/`xmax`). This allows readers to access data without blocking writers. Row-level locks are used for concurrent updates.
*   **SQLite**: Uses database-level locks. Traditionally, writing to the database locks the entire file, blocking readers. In WAL (Write-Ahead Log) mode, one writer can append changes to a WAL file while readers access the main file, but only one write operation is allowed at a time.

### 3.6 Durability and Recovery
*   **PostgreSQL**: Uses Write-Ahead Logging (WAL). All changes are written to log files before they are applied to the main data files. Recovery replays the logs from the last checkpoint.
*   **SQLite**: Uses a Rollback Journal by default, which copies original pages to a journal file before modifying them so they can be restored on crash. It also supports WAL mode for recovery.

---

## 4. Design Trade-Offs

| Feature | PostgreSQL | SQLite |
| :--- | :--- | :--- |
| **Concurrency** | High (Supports many concurrent readers and writers) | Low (Single writer at a time) |
| **Resource Usage**| High (Needs dedicated memory and processes) | Low (Runs with minimal memory footprint) |
| **Read Latency** | Medium (Network and IPC overhead) | Low (Direct in-process function calls) |
| **Configuration** | Complex (Requires setup, permissions, tuning) | Zero (No setup required) |
| **File Structure** | Separate files per table/index | Single `.db` file |

---

## 5. Experiments & Observations

### 5.1 Observing PostgreSQL Query Plans
We can use `EXPLAIN ANALYZE` in PostgreSQL to see how the query planner behaves:

```sql
EXPLAIN ANALYZE 
SELECT count(*) FROM users WHERE status = 'active';
```

**Observation:**
The output shows whether the planner selected an Index Scan or a Sequential Scan. It also shows the actual execution time and the number of blocks read from the shared buffers versus disk.

### 5.2 SQLite Query Execution
In SQLite, we can inspect how B-Trees are traversed:

```sql
EXPLAIN QUERY PLAN 
SELECT * FROM users WHERE user_id = 100;
```

**Observation:**
Since `user_id` is the primary key and the table is structured as a B+Tree, the output will show `USING INTEGER PRIMARY KEY (rowid=?)`, indicating SQLite did a direct binary search down the table's B-Tree to find the row.

---

## 6. Key Learnings

1.  **Deployment Dictates Design**: PostgreSQL's complexity is necessary to support high multi-user concurrency and scalability. SQLite's simple single-file architecture is chosen to make it lightweight and fast for local storage.
2.  **No Best Database**: SQLite is highly efficient for single-user apps, mobile devices, and local testing, while PostgreSQL is the right choice for network-based, concurrent write-heavy systems.
3.  **Concurrency vs. Simplicity**: PostgreSQL trades memory and introduces maintenance tasks (like vacuuming) to support concurrent writes, whereas SQLite trades write concurrency for zero configuration and easy deployment.
