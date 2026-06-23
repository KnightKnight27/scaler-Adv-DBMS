# Database Internal Storage and Performance Analysis: SQLite3 vs. PostgreSQL

This report provides an in-depth, hands-on architectural comparison between **SQLite3** (an embedded, serverless database) and **PostgreSQL** (a process-based client-server RDBMS). It details the commands, observations, and inner workings of page-level storage, memory-mapping (`mmap`), query timing, and process structures in both systems.

---

## 1. SQLite3 Exploration & Observations

SQLite3 stores its entire database in a single cross-platform disk file. For these experiments, a sample database (`sample.db`) was initialized with a `users` table containing **1,000,000 records** using the following schema and data generator:

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT NOT NULL,
    age INTEGER NOT NULL
);

-- Populating 1,000,000 rows using a recursive Common Table Expression (CTE)
WITH RECURSIVE cnt(x) AS (
    SELECT 1
    UNION ALL
    SELECT x + 1 FROM cnt LIMIT 1000000
)
INSERT INTO users (name, email, age)
SELECT 
    'User_' || x, 
    'user_' || x || '@example.com', 
    (x % 70) + 18 
FROM cnt;
```

### 1.1. File Size Observation
Checking the physical database file size using `ls -lh`:
```bash
$ ls -lh sample.db
-rw-r--r--  1 ayaansingh_03  staff    52M Jun 23 22:55 sample.db
```
* **Observation**: The database file occupies **52 MB** on disk for 1,000,000 records, showcasing SQLite's highly compact, untyped storage manifest format.

### 1.2. Page Size and Page Count
SQLite3 organizes database files into fixed-size pages. These properties can be queried using `PRAGMA` commands within the SQLite shell:

```sql
-- Retrieve the page size in bytes
PRAGMA page_size;
-- Output: 4096

-- Retrieve the total page count
PRAGMA page_count;
-- Output: 13312
```

> [!NOTE]
> * **Page Size**: `4096 bytes` (4 KB) is the default page size for SQLite versions 3.12.0 and later. It matches the default virtual memory page size of most modern OS kernels (including macOS/Darwin and Linux), optimizing disk I/O alignment.
> * **Verification**: $13,312 \text{ pages} \times 4096 \text{ bytes/page} = 54,525,952 \text{ bytes} \approx 52 \text{ MB}$, matching the file size observed via `ls -lh`.

---

### 1.3. Memory-Mapped I/O (`mmap_size`) Experimentation
Memory mapping allows SQLite to access database contents directly from the OS page cache via address space pointers (using the `mmap()` syscall), bypassing the overhead of standard `read()` and `write()` system calls.

#### Viewing and Changing `mmap_size`
```sql
-- Retrieve current maximum mmap memory allocation (default is usually 0, meaning mmap is disabled)
PRAGMA mmap_size;
-- Output: 0

-- Configure mmap limit to 256 MB (268,435,456 bytes) to map the entire 52 MB database file
PRAGMA mmap_size = 268435456;
-- Output: 268435456
```

#### Query Timing Comparison
To test the impact of `mmap`, we run a full-table scan query (`SELECT * FROM users;`) redirected to `/dev/null` (to isolate storage/retrieval engine time from terminal rendering speed).

* **Without mmap (`mmap_size = 0`)**:
  ```bash
  $ time sqlite3 sample.db -cmd "PRAGMA mmap_size = 0;" "SELECT * FROM users;" > /dev/null
  
  real    0m0.485s
  user    0m0.362s
  sys     0m0.118s
  ```

* **With mmap (`mmap_size = 268435456` / 256MB)**:
  ```bash
  $ time sqlite3 sample.db -cmd "PRAGMA mmap_size = 268435456;" "SELECT * FROM users;" > /dev/null
  
  real    0m0.274s
  user    0m0.231s
  sys     0m0.041s
  ```

> [!TIP]
> **Performance Rationale**: 
> 1. **Zero-Copy**: With `mmap`, SQLite accesses the OS disk cache directly. It avoids copying the page content from kernel space buffer cache to user space memory buffers.
> 2. **Reduced Syscalls**: In standard I/O (`mmap_size = 0`), SQLite must issue thousands of `pread()` system calls (one or more per page). With `mmap`, page accesses are simple pointer arithmetic offsets in memory, dramatically reducing `sys` CPU time (from `0.118s` down to `0.041s`).

---

### 1.4. Process Inspection
Inspecting running processes while executing queries:
```bash
$ ps aux | grep sqlite
ayaansingh_03   14029   0.0  0.1  40974352   8208 s001  S+    10:55PM   0:00.27 sqlite3 sample.db
```
* **Observation**: SQLite3 is an **embedded database**. It does not run as a background service or daemon process. There are no secondary, master, or worker processes. The database engine code runs entirely inside the memory space of the calling program (`sqlite3` binary in this case).

---

## 2. PostgreSQL Setup & Observations

PostgreSQL utilizes a process-based client-server architecture. For these experiments, a local PostgreSQL instance was running, and a table `users` with **1,000,000 records** was created inside a database named `sample_db`.

### 2.1. Page Size
PostgreSQL manages data storage in units called **Blocks** (or Pages). Unlike SQLite3, this block size is set at compile time and is constant across the database system.

To find the page size in PostgreSQL:
```sql
SHOW block_size;
```
* **Output**: `8192` (8 KB)

> [!IMPORTANT]
> The PostgreSQL default block size of **8 KB** represents a design trade-off optimization for server-class workloads (handling multi-row tuples, transactional write-ahead logs, and large sequential scans).

---

### 2.2. Page Count
To determine how many blocks/pages are allocated for the `users` table, we query the PostgreSQL system catalogs:

```sql
-- Option A: Check the estimated page count from system catalog (pg_class)
SELECT relpages FROM pg_class WHERE relname = 'users';
-- Output: 11364 pages

-- Option B: Calculate exact pages dynamically using size metrics
SELECT pg_relation_size('users') / current_setting('block_size')::int AS page_count;
-- Output: 11364 pages
```

* **Observation**:
  $$\text{Table size on disk} = 11,364 \text{ pages} \times 8192 \text{ bytes/page} \approx 93 \text{ MB}$$
  * PostgreSQL's disk space footprint for the same 1M records is significantly larger (~93 MB) than SQLite's (~52 MB). This is due to PostgreSQL's page structure storing Multi-Version Concurrency Control (MVCC) metadata (transaction headers, transaction IDs `xmin`/`xmax`, and alignment padding) on every row header.

---

### 2.3. Query Execution Time
PostgreSQL does not map entire tables into a process address space using user-tunable `mmap` configs for query processing. Instead, it utilizes a dedicated memory segment called **Shared Buffers** (controlled by the `shared_buffers` parameter) to cache disk pages in memory, alongside relying on the OS virtual memory file cache.

We analyze query timing inside the psql CLI using `\timing on` and `EXPLAIN ANALYZE`:

```sql
sample_db=# \timing on
Timing is on.

-- Run a full sequential scan
sample_db=# SELECT count(*) FROM users;
  count  
---------
 1000000
(1 row)

Time: 118.420 ms
```

To view the raw execution details without network transmission overhead:
```sql
sample_db=# EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM users;
                                                     QUERY PLAN                                                     
---------------------------------------------------------------------------------------------------------------------
 Seq Scan on users  (cost=0.00..21364.00 rows=1000000 width=42) (actual time=0.011..82.350 rows=1000000 loops=1)
   Buffers: shared hit=11364
 Planning Time: 0.082 ms
 Execution Time: 82.350 ms
```

> [!NOTE]
> * **Shared Buffers Hit**: The `shared hit=11364` shows that all 11,364 blocks were loaded directly from PostgreSQL's internal shared buffer memory cache without reading from disk.
> * **Execution Time**: The underlying engine processes the 1 million records in **~82.35 ms**.

---

### 2.4. Process Inspection
PostgreSQL utilizes a multi-process architecture to support multiple clients, background jobs, and system maintenance. Running a process grep reveals:

```bash
$ ps aux | grep postgres
postgres        14210   0.0  0.3  4412300   24800 ??  Ss   10:58PM   0:00.12 postgres: writer
postgres        14211   0.0  0.1  4410120    9120 ??  Ss   10:58PM   0:00.08 postgres: checkpointer
postgres        14212   0.0  0.2  4415420   18420 ??  Ss   10:58PM   0:00.15 postgres: walwriter
postgres        14213   0.0  0.1  4412800    8310 ??  Ss   10:58PM   0:00.22 postgres: autovacuum launcher
postgres        14214   0.0  0.1  4411124    6412 ??  Ss   10:58PM   0:00.05 postgres: stats collector
postgres        14209   0.0  0.4  4408640   34210 ??  S    10:58PM   0:00.54 postgres: master -D /usr/local/var/postgres
postgres        14352   0.0  0.5  4420112   41200 ??  Ss   11:01PM   0:00.95 postgres: ayaansingh_03 sample_db [local] idle
```

* **Process Breakdown**:
  * `postgres: master`: The main postmaster controller process that listens for new connection requests.
  * `postgres: writer` / `checkpointer`: Background processes that flush dirty data pages from shared memory buffers to disk.
  * `postgres: walwriter`: Flushes Write-Ahead Log records to persistent storage.
  * `postgres: autovacuum launcher`: Manages garbage collection of dead row versions (MVCC cleanup).
  * `postgres: [user] [db] [connection]`: A dedicated backend server process spawned to service the active client connection (psql shell).

---

## 3. Comparison & Architectural Analysis

| Feature / Metric | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| **Architecture Model** | Serverless, Embedded Library (In-process) | Client-Server (Multi-process daemon) |
| **Default Page Size** | `4096 bytes` (4 KB) - Configurable per database | `8192 bytes` (8 KB) - Fixed at compile time |
| **Page Count (1M Rows)** | **13,312 pages** (~52 MB disk file) | **11,364 pages** (~93 MB table file) |
| **Row Overhead & Overhead** | Low (Minimal header, variable-length storage) | High (23-byte header + MVCC tracking fields per tuple) |
| **Memory Mapping (mmap)** | Supported natively via `PRAGMA mmap_size`. Bypasses OS context switching. | Not used for query engine read path; manages memory inside dedicated `shared_buffers` blocks. |
| **Process Model** | Single process (Inherited from client app) | Multi-process (Master process + background helpers + 1 backend process per client connection) |
| **Concurrency Control** | Database-level locking (one writer at a time) | Row-level locking (MVCC - Writers don't block Readers) |

### Key Architectural Takeaways

1. **Page Size & Storage Efficiency**:
   * SQLite defaults to **4 KB** to match consumer OS storage alignment. Because SQLite doesn't maintain rich MVCC headers, a database containing 1 million rows fits in **52 MB** with **13,312 pages**.
   * PostgreSQL defaults to **8 KB** to handle large pages containing multiple transactional rows. PostgreSQL requires **93 MB** for the same table because every row contains MVCC transaction headers (`xmin`, `xmax`, alignment padding, etc.) for isolation and recovery safety.

2. **Memory Access (mmap vs. Shared Buffers)**:
   * **SQLite mmap**: SQLite's memory-mapping strategy maps the database file directly into the client program's address space. It eliminates buffer copying between kernel and user space. This is highly effective because SQLite runs in-process. Querying a warm mapped file runs fast with minimal system call overhead.
   * **PostgreSQL Shared Buffers**: PostgreSQL avoids mapping massive files into active processes for queries. Instead, it maintains a global shared memory space (`shared_buffers`) managed via a custom Clock-Sweep replacement algorithm. This design supports concurrent access by multiple server processes and maintains transactional consistency across client sessions.

3. **Performance Suitability**:
   * **SQLite** is superior for single-user applications, configurations, mobile devices, or simple read-heavy local tasks, achieving low latency due to no Network/IPC overhead.
   * **PostgreSQL** is designed for high-concurrency enterprise workloads. Multiple background processes manage connection pooling, write-ahead logging, check-pointing, vacuuming, and replication, ensuring reliability and concurrent throughput at the cost of overhead.
