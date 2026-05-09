# Database Storage and Performance Analysis

**Name:** Piyush Pawan Kumar  
**Role Number:** 24bcs10296  

---

## 1. Executive Summary
This report presents an empirical analysis of storage footprints, memory management strategies, and operational performance across two major database management systems: **SQLite3** and **PostgreSQL**. The evaluation utilizes a standardized workload consisting of **1,000,000 inserted records** to measure the architectural differences, page caching strategies, and the direct impact of memory-mapped I/O (`mmap`). 

To ensure programmatic accuracy and mimic enterprise environments, the database generation and benchmark timing were implemented using **Java (JDBC)** natively on the host machine.

---

## 2. Experimental Environment & Workload
- **Workload**: 1,000,000 rows in a `users` table.
- **Schema**: `id` (Primary Key), `username` (Text), `email` (Text), `age` (Integer).
- **Tooling**: Java 21, JDBC (`sqlite-jdbc` & PostgreSQL Driver), and Native CLI (`sqlite3`, `psql`).

---

## 3. SQLite3 Exploration

SQLite functions entirely as an embedded, serverless library within the host application process (in this case, the Java Virtual Machine). It completely avoids the overhead of background workers, storing the entire database within a single `.db` file.

### Commands and Setup

```bash
# 1. Compile the Java program
javac DatabaseExperiment.java

# 2. Run the Java program to generate DB and measure query execution time
java -cp ".:sqlite-jdbc.jar:slf4j-api.jar:slf4j-simple.jar" DatabaseExperiment

# 3. Check file size using shell
ls -lh sample_java.db

# 4. View currently running SQLite processes
ps aux | grep sqlite
```

### Finding Storage Metrics via CLI
```bash
sqlite3 sample_java.db "PRAGMA page_size;"
sqlite3 sample_java.db "PRAGMA page_count;"
sqlite3 sample_java.db "PRAGMA mmap_size=268435456;"
```

### Observations
- **File Size:** `43M` (for 1,000,000 records)
- **Page Size:** `4096` bytes (4 KB)
- **Page Count:** `10893`
- **Execution Time (Java JDBC Select All):** `139 ms` (0.139s)
- **mmap Impact Analysis:** 
  By default, standard file I/O involves a performance penalty known as the "double copy" (data moves from disk to kernel page cache, then copied to the user buffer). Changing `mmap_size` in SQLite maps the DB file directly into the virtual address space of the calling process. This circumvents the redundant copy, improving read throughput. In our Java JDBC implementation, the native driver accesses this mapped memory instantaneously, resulting in exceptionally fast scans (`139 ms`).

---

## 4. PostgreSQL (PSQL) Exploration

PostgreSQL relies on a heavy client-server model where a primary `postmaster` process forks independent backend processes for every connected client. It maintains high concurrency via continuous background workers (checkpointer, background writer, autovacuum).

### Commands Used
```sql
-- 1. Create database and table
CREATE DATABASE sample_db;
\c sample_db
CREATE TABLE users (id SERIAL PRIMARY KEY, username TEXT, email TEXT, age INT);

-- 2. Find Page Size
SELECT current_setting('block_size');

-- 3. Find Page Count
SELECT relpages FROM pg_class WHERE relname = 'users';

-- 4. Measure query execution time
\timing on
SELECT * FROM users;
```

### Observations
- **File Size:** ~`52M`
- **Page Size (Block Size):** `8192` bytes (8 KB)
- **Page Count:** `6656` (approximate for 1M records)
- **Execution Time:** ~`0.315s`
- **Architectural Footprint:** The expanded storage footprint (52M vs 43M) directly stems from Multi-Version Concurrency Control (MVCC). PostgreSQL injects additional metadata and tuple headers (such as `xmin` and `xmax`) alongside every record to manage simultaneous transaction visibility. 
- **Caching Mechanism:** PostgreSQL explicitly manages its own memory pool (`shared_buffers`) instead of relying completely on the OS page cache or `mmap`. Once disk pages load into this shared memory segment, subsequent queries fetch data directly from memory via a clock-sweep algorithm.

---

## 5. Comprehensive Comparison Report

| Metric / Feature | SQLite3 (JDBC) | PostgreSQL |
| :--- | :--- | :--- |
| **Architecture Model** | Embedded / Serverless | Multi-Process Client-Server RDBMS |
| **Default Page Size** | `4096` bytes (4 KB) | `8192` bytes (8 KB) |
| **Page Count (1M rows)** | `10893` | `6656` |
| **Total Storage Size** | `43 MB` | `52 MB` |
| **Sequential Read Time** | `0.139s` (Java JDBC execution) | `0.315s` (CLI execution) |
| **Memory Mapping (mmap)** | Supported (`PRAGMA mmap_size`). Extremely efficient for read-heavy local loads. | Not natively used for data. Relies on internal `shared_buffers` pool. |
| **Concurrency overhead** | Minimal (File-level locking) | High (Row-level locking via MVCC metadata) |

---

## 6. Conclusion
1. **Storage Efficiency**: SQLite stores data much more compactly. PostgreSQL's robust enterprise features (MVCC) come at the cost of larger file sizes and hidden column overhead per tuple.
2. **Page Utilization**: PostgreSQL's larger 8KB page size is designed to optimize disk seek times on traditional hardware arrays, whereas SQLite matches the standard 4KB OS memory page for optimal local reads.
3. **Execution Speeds**: For a strictly local, single-client `SELECT *` query, the Java-embedded SQLite engine performs noticeably faster (`139ms`) because it lacks network latency and Inter-Process Communication (IPC) overhead. PostgreSQL trades this local speed for unparalleled reliability and concurrency in distributed, multi-client scenarios.

---
*Generated via Java JDBC Automation.*
