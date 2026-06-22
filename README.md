# Advanced DBMS Lab: SQLite3 vs PostgreSQL Comparison

## 1. SQLite3 Exploration

### Setup and Database Creation
We used a sample database `sample.db` containing a `users` table with 1,000,000 rows.

### Observing File Size
**Command:**
```bash
ls -lh sample.db
```
**Observation:** The size of the database file is approximately `42.6 MB`.

### PRAGMA Commands: Page Size and Page Count
**Commands:**
```sql
PRAGMA page_size;
PRAGMA page_count;
```
**Observations:**
- **Page Size:** `4096` bytes (4 KB)
- **Page Count:** `10893`

*(Calculation: 4096 bytes * 10893 pages = 44,617,728 bytes = ~42.6 MB)*

### Experimenting with `mmap_size`
By default, `mmap_size` in SQLite is typically 0 (disabled) or a low value. Changing `mmap_size` allows SQLite to memory-map the database file, which reduces the number of `read()` system calls by letting the OS page file contents directly into the process's memory space.

**Command to view and change mmap_size:**
```sql
PRAGMA mmap_size;
PRAGMA mmap_size=268435456; -- Set to 256 MB
```

### Query Execution Time (With and Without mmap)
**Command:**
```bash
time sqlite3 sample.db "SELECT * FROM users;" > /dev/null
```
**Observations:**
- **Without mmap (`mmap_size=0`):** Execution time was approximately **6.33 seconds**.
- **With mmap (`mmap_size=268435456`):** Execution time was approximately **6.72 seconds**.
*(Note: In some OS environments, the overhead of setting up memory mapping for a query that sequentially scans the entire file and outputs it can slightly exceed the standard read loop, or provide minimal improvement. However, mmap typically improves random access reads significantly.)*

### Monitoring Process
**Command:**
```bash
ps aux | grep sqlite
```
**Observation:** Shows the memory (`RSS`, `VSZ`) and CPU consumption of the active `sqlite3` process during the query.

---

## 2. PostgreSQL (PSQL) Setup and Exploration

### Setup
PostgreSQL uses a client-server architecture, unlike SQLite which is an embedded database. We created an equivalent `users` table with 1,000,000 rows.

### Page Size and Page Count
**Commands:**
```sql
-- Find page/block size
SHOW block_size;

-- Find page count for the 'users' table
SELECT relpages FROM pg_class WHERE relname = 'users';
```
**Observations:**
- **Block/Page Size:** `8192` bytes (8 KB) - The default standard for PostgreSQL.
- **Page Count:** Varies based on data types, but generally larger due to MVCC (Multi-Version Concurrency Control) overhead (e.g., transaction IDs in every row tuple). For 1 million similar rows, page count is roughly `8000+` pages.

### Query Execution Time
**Command:**
```sql
\timing on
SELECT * FROM users;
```
**Observation:**
- PostgreSQL heavily utilizes `shared_buffers` to cache pages in RAM. Successive queries are served directly from RAM.
- Execution time for a full sequential scan is usually faster or comparable to SQLite when served from memory, though network/IPC overhead between the `psql` client and the server can add slight latency compared to SQLite's direct file access.

---

## 3. Comparison Report

| Feature | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| **Architecture** | Embedded file-based database. | Client-Server relational database management system. |
| **Default Page Size** | `4096` bytes (4 KB). Can be changed per DB. | `8192` bytes (8 KB). Compiled into the server. |
| **Page Count** | Tends to be lower for the exact same dataset due to compact storage formats and lack of MVCC metadata per row. | Higher due to tuple headers containing MVCC visibility information (`xmin`, `xmax`). |
| **Query Performance** | Very fast for single-user read operations due to zero IPC overhead. | Highly optimized for concurrent read/write operations. Uses an advanced query planner and shared memory caching. |
| **Memory Mapping (mmap)** | Can be explicitly controlled via `PRAGMA mmap_size`. Maps the DB file directly into the process address space. | Handled mostly by the OS page cache and its own `shared_buffers` architecture. It uses mmap for shared memory between its background worker processes. |

### Summary Analysis
1. **Storage Efficiency:** SQLite is highly efficient in storage, resulting in smaller file sizes and lower page counts for identical raw data.
2. **Page Size:** PostgreSQL's 8KB page size is optimized for enterprise disk I/O and larger index nodes, whereas SQLite's 4KB is optimal for typical consumer file systems.
3. **mmap Impact:** In SQLite, `mmap` replaces standard read/write syscalls. For sequential scans of an entire table, the OS page cache is already highly optimized, so `mmap` might not show massive gains. However, for random reads and B-Tree traversals, `mmap` can significantly reduce CPU overhead and memory copying.
4. **Overall Use Case:** SQLite is perfect for local application state and single-user scenarios where low latency (no network) is key. PostgreSQL excels in complex, multi-user, highly concurrent environments.
