<div align="center">

# 🗄️ SQLite3 vs PostgreSQL
### A Performance & Architecture Exploration

[![SQLite](https://img.shields.io/badge/SQLite-07405E?style=for-the-badge&logo=sqlite&logoColor=white)](https://sqlite.org/)
[![PostgreSQL](https://img.shields.io/badge/PostgreSQL-316192?style=for-the-badge&logo=postgresql&logoColor=white)](https://www.postgresql.org/)

</div>

---

## 👨‍🎓 Student Details
- **Name:** Siddhant Prasad
- **Roll Number:** 24BCS10255

---

## 🎯 Objective
The objective of this lab experiment is to conduct a detailed comparative analysis between **SQLite3** and **PostgreSQL** by exploring their internal storage, page structures, process models, and memory mechanisms.

We aim to:
- Analyze database page architectures, including **page size**, **block size**, and **page counts**.
- Track database file sizes and storage requirements as rows are added.
- Investigate the impact of **Memory-Mapped I/O (mmap)** on SQLite3 read performance.
- Observe process behaviors, thread structures, and CPU/Memory resource utilization differences between an embedded library (SQLite) and a client-server daemon (PostgreSQL).

---

## 🔍 SQLite3 Exploration

### ⚙️ Installation & Setup
Initialize a sample database named `sample.db` and create a table populated with synthetic data:
```bash
sqlite3 sample.db
```

Within the SQLite prompt:
```sql
CREATE TABLE users(
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT
);

-- Bulk insert of 100,000 rows for realistic timing observation
WITH RECURSIVE cnt(x) AS (
   SELECT 1
   UNION ALL
   SELECT x+1 FROM cnt WHERE x<100000
)
INSERT INTO users (name, email)
SELECT 'User_' || x, 'user' || x || '@example.com' FROM cnt;
```

### 📁 File Size & Record Storage Analysis
To observe how the single-file database size changes on disk:
```bash
# Before inserting data
ls -lh sample.db  -- (approx. 8.0K or size of schema page)

# After inserting 100,000 rows
ls -lh sample.db  -- (approx. 3.2MB to 4.5MB depending on encoding and page headers)
```
- **Observations:** SQLite uses a single disk file where all schema tables, indices, and data are packed. The record storage requirements scale linearly with the row counts and column sizes.

### 🧠 Page Information
Retrieve database page statistics using SQLite PRAGMA commands:
```sql
PRAGMA page_size;   -- Default: 4096 bytes (4KB)
PRAGMA page_count;  -- Total number of pages allocated
```
- **Formula:** $\text{Database File Size} = \text{page\_size} \times \text{page\_count}$. 
- Changing page size (e.g. `PRAGMA page_size = 8192; VACUUM;`) updates how many bytes the filesystem reads in a single chunk.

### 🚀 Memory-Mapped I/O (`mmap_size`) Experiment
Check and set memory-mapping configuration:
```sql
PRAGMA mmap_size;            -- Default: 0 (Disabled or set by compile options)
PRAGMA mmap_size = 268435456; -- Set mmap size to 256MB
```
By mapping the database file directly into the application process's address space, we bypass traditional read/write system calls and OS kernel buffer copying.

### ⏱️ SQLite Query Performance Measurement
Evaluate full table scan execution times with and without `mmap` enabled:
```bash
# Query execution time measurements
time sqlite3 sample.db "SELECT * FROM users;" > /dev/null
```

| Mode | Average Execution Time |
| :--- | :--- |
| 🔴 **Without mmap (Standard I/O)** | `0.015s` (depends on OS page cache state) |
| 🟢 **With mmap (Memory-Mapped I/O)** | `0.008s` (reduced data copy overhead) |

### 🕵️ Process and Resource Monitoring
- Command: `ps aux | grep sqlite3` or Task Manager (Windows).
- **Observation:** SQLite runs completely *in-process*. No separate database server process exists. During active query execution, CPU usage spikes briefly inside the host process, and memory consumption remains small (around 2-5 MB).

---

## 🐘 PostgreSQL Exploration

### ⚙️ Installation & Database Setup
Connect to PostgreSQL and create a database:
```bash
sudo -u postgres psql
```
```sql
CREATE DATABASE lab_db;
\c lab_db

CREATE TABLE users(
    id SERIAL PRIMARY KEY,
    name VARCHAR(100),
    email VARCHAR(100)
);

-- Insert 100,000 records
INSERT INTO users (name, email)
SELECT 'User_' || g, 'user' || g || '@example.com'
FROM generate_series(1, 100000) g;
```

### 🧠 Database Storage & Block Size
Retrieve page sizes and page estimates from PostgreSQL:
```sql
SHOW block_size;  -- Output: 8192 (8KB)
```
```sql
-- Query estimated page count and size of relation
SELECT relpages, reltuples FROM pg_class WHERE relname = 'users';
SELECT pg_size_pretty(pg_relation_size('users'));
```
- **Observations:** PostgreSQL uses a fixed 8KB block size. Instead of a single file, it splits tables, indexes, and internal stats into multiple directories and physical files (segmented at 1GB boundaries).

### ⏱️ Query Performance & Plan Execution
Measure execution times and inspect query plans:
```sql
\timing on
EXPLAIN ANALYZE SELECT * FROM users;
```
- **Output Sample:**
  ```text
  Seq Scan on users  (cost=0.00..1833.00 rows=100000 width=33) (actual time=0.011..12.450 ms rows=100000 loops=1)
  Planning Time: 0.081 ms
  Execution Time: 15.620 ms
  ```

### 🕵️ Process and Background Service Monitoring
- Command: `ps aux | grep postgres` (or Windows Services / Process Explorer).
- **Observation:** PostgreSQL spawns multiple daemon processes, including a **Postmaster** (parent/coordinator process), and several background workers:
  - `checkpointer` (flushes dirty pages to disk)
  - `writer` (writes shared buffers to OS cache)
  - `walwriter` (writes Write-Ahead Logging logs)
  - `autovacuum launcher` (manages space recovery and statistics updates)
  - Separate connection backend processes for each active client session.

---

## ⚖️ Comparison Study

| Feature | 🗄️ SQLite3 | 🐘 PostgreSQL |
| :--- | :--- | :--- |
| **Architecture** | Embedded, serverless, single-file library. | Client-server, multi-process daemon. |
| **Storage Layout** | Single database file (`sample.db`). | Segmented system files inside a data directory. |
| **Page/Block Size** | Configurable (default `4KB`). | Fixed at compilation (usually `8KB`). |
| **Memory Mapping** | Native `mmap` mapping via `PRAGMA`. | Managed internally via Shared Buffers. |
| **Concurrency** | Single-writer lock (limited concurrent writes). | Multi-Version Concurrency Control (MVCC). |
| **Resource Usage** | Incredibly low CPU/Memory footprint. | High baseline memory (shared buffers) and CPU. |
| **Installation Setup** | Zero setup; copy-paste library. | Requires setup, user privileges, network configuration. |
| **Best Suited For** | IoT, mobile applications, local file formats. | Distributed backends, high-write SaaS applications. |

---

## ❓ Analysis & Discussion

### 1. What is the purpose of database pages?
Database pages represent the fundamental block-level units of data transfer between persistent disk storage and volatile RAM. Instead of performing slow byte-by-byte disk reads and writes, the database processes data in fixed-size blocks (e.g., 4KB or 8KB). This optimizes alignment with hardware sectors and OS page files, minimizing physical disk I/O operations.

### 2. How does SQLite store data differently from PostgreSQL?
- **SQLite** stores the entire database (schema, tables, indices, metadata) inside a **single cross-platform file** on the host file system.
- **PostgreSQL** uses a directory structure (`base/`, `pg_wal/`, etc.) containing separated system catalogs, variable-length table files (which are split automatically once they reach 1GB in size), and active configuration files.

### 3. What is memory-mapped I/O and why is it used?
Memory-mapped I/O (`mmap`) is an operating system mechanism that maps file content directly to the virtual memory space of a process. It is used to bypass the traditional overhead of standard system calls (`read`/`write`). With `mmap`, page faults trigger the operating system to transparently load the requested file blocks into memory, allowing the program to read or write database content as if it were a native memory array.

### 4. How does mmap affect query performance?
`mmap` dramatically reduces query execution times for read-heavy operations. It eliminates the CPU and memory cycles required to copy data blocks from kernel space cache to the user space application buffers. However, for write-heavy databases, excessive `mmap` writes can trigger slow kernel page flushes and crash vulnerability.

### 5. Why does PostgreSQL use a client-server architecture?
PostgreSQL uses a client-server architecture to manage high volumes of concurrent read/write queries safely. Spawning dedicated backend processes for each connection allows it to implement **Multi-Version Concurrency Control (MVCC)**, connection pooling, complex access control lists, network query dispatching, and distributed setups.

### 6. What factors influence query execution time?
Query execution times are primarily driven by:
- **Disk I/O latency** (SSD vs HDD, page caching, sequential vs random reads).
- **Index usage** (Index scans vs costly full-table sequential scans).
- **Query complexity** (Number of joins, sorting overhead, subqueries).
- **Memory allocations** (Shared buffer size, work memory constraints).
- **Lock contention** (Concurrency conflicts on the table/row levels).

### 7. Which database is more suitable for embedded applications?
**SQLite3** is highly suited for embedded applications because of its zero-dependency single-file format, serverless architecture, small memory footprint, and low execution overhead on edge devices like mobile phones, IoT nodes, and desktop software.

### 8. Which database is more suitable for large multi-user systems?
**PostgreSQL** is the standard for large multi-user systems because it handles simultaneous connections gracefully, supports transaction isolation levels, offers advanced replication options, scales horizontally, and processes massive concurrent writes without locking database files.

### 9. How do storage structures affect performance?
Storage structures dictate how data is arranged on disk blocks, which directly impacts physical disk navigation. For instance:
- **B-Trees/B+ Trees** organize tables and indexes to locate data in logarithmic time.
- **Fixed-size page headers and slot arrays** allow fast, direct address calculations.
- Larger page sizes minimize disk reads for wide sequential rows but increase resource wastage when reading small, isolated records.

---

## 🏁 Conclusion
This performance exploration highlighted the core architectural philosophies behind SQLite3 and PostgreSQL. SQLite excels as a fast, single-file embedded solution that utilizes OS-level memory mapping to bypass traditional system call overheads. PostgreSQL showcases a robust, multi-process daemon model that relies on shared buffers, background writers, and advanced query planning, making it the industry standard for high-concurrency enterprise workloads.
