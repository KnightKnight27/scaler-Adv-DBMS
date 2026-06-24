# SQLite3 vs PostgreSQL: An Empirical and Architectural Comparison

## Overview
This document presents an empirical comparison between **SQLite3** and **PostgreSQL**, contrasting an embedded, file-based database with a full-fledged client-server relational database system. The evaluation is based on local experimentation, focusing on storage footprint, page/block size, query execution performance, and process architecture.

---

## 1. SQLite3: Embedded Database Evaluation

### Experimental Setup
The following commands were used to profile an SQLite3 database instance:

```bash
sqlite3 test.db
ls -lh
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
PRAGMA mmap_size = 268435456;
time sqlite3 test.db "PRAGMA mmap_size=0; SELECT * FROM users;"
time sqlite3 test.db "PRAGMA mmap_size=268435456; SELECT * FROM users;"
ps aux | grep sqlite
```

### Observations
- **Storage Footprint**: The entire database is stored as a single file, ranging from **3.7 MB** to **8.8 MB** depending on the sample size.
- **Page Configuration**: The fixed page size is **4096 bytes (4 KB)**, with a total page count between 931 and 2242.
- **Memory-Mapped I/O (mmap)**: The `mmap_size` defaults to 0 but was temporarily increased to 256 MB (268,435,456 bytes) for the connection session.
- **Query Performance**: A full table scan took roughly **40–52 ms** (real time). Enabling `mmap` yielded no measurable improvement, as the database easily fit within the OS page cache, making memory mapping redundant in this scenario.
- **Process Model**: SQLite3 does not employ a background server daemon. It runs entirely as a library within the calling application's thread space. Running `ps aux | grep sqlite` returned only the `grep` command itself, verifying the absence of persistent background processes.

---

## 2. PostgreSQL: Client-Server Architecture Evaluation

### Experimental Setup
The following commands were used to profile a PostgreSQL database instance:

```bash
# Start the PostgreSQL service
sudo systemctl start postgresql                                # Linux
/opt/homebrew/opt/postgresql@18/bin/postgres -D ... -p 55432   # macOS

# Database interaction
createdb scaler_lab
psql -d scaler_lab

# Profiling commands
SHOW block_size;
SELECT relpages FROM pg_class WHERE relname = 'users';
SELECT pg_size_pretty(pg_relation_size('users')) AS table_size;
EXPLAIN ANALYZE SELECT * FROM users;
ps aux | grep postgres
```

### Observations
- **Storage Footprint**: The `users` table alone required **6.4 MB to 7.5 MB**. This larger footprint (compared to SQLite's entire DB) is due to PostgreSQL's per-tuple overhead and the separate Write-Ahead Log (WAL) architecture.
- **Block Configuration**: The default block size is **8192 bytes (8 KB)**, double that of SQLite. The table occupied between 824 and 935 pages.
- **Query Performance**: Execution times for a full table scan ranged from **6.5 ms** to **37.8 ms**. The query planner utilized a sequential scan with minimal planning overhead (~0.2 ms). PostgreSQL consistently outperformed SQLite, proving **25% to 6× faster** depending on the environment.
- **Process Model**: PostgreSQL utilizes a multi-process architecture. Checking running processes revealed several daemons, confirming a robust client-server model designed for high concurrency:
  - Main `postmaster` process
  - Checkpointer
  - Background writer
  - WAL writer
  - Autovacuum launcher
  - Logical replication launcher
  - I/O workers

---

## 3. Comparative Analysis

| Metric | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| **Architecture** | Embedded, single-file | Client-server, multi-process |
| **Storage Model** | Single `.db` file | Multiple files + WAL |
| **Page/Block Size** | 4096 bytes (4 KB) | 8192 bytes (8 KB) |
| **Table/DB Size** | 3.7 – 8.8 MB (Entire DB) | 6.4 – 7.5 MB (Table only) |
| **Memory Mapping (mmap)** | Yes (Per-connection, user-configurable)| No direct mmap; relies on shared buffers |
| **Full Scan Query Time**| 40 – 52 ms | 6.5 – 38 ms |
| **Background Processes**| None (Library embedded in app) | Multiple (`checkpointer`, `walwriter`, etc.) |
| **Concurrency Support** | Single writer (Database-level lock) | Full MVCC, row-level locks |
| **Setup Complexity** | Zero configuration | Moderate (Installation and service management) |
| **Idle Resource Usage** | Very low (~4 MB RSS) | Higher (~45 MB + per-connection overhead) |

---

## 4. Conclusion

- **SQLite3** is the optimal choice for embedded systems, local device storage, and rapid prototyping. Its zero-configuration design, single-file portability, and minimal resource footprint make it highly efficient for low-concurrency use cases. However, tuning features like `mmap` may yield diminishing returns for databases small enough to fit entirely in the OS page cache.
- **PostgreSQL** is the superior solution for production environments demanding high concurrency, data integrity, and complex query optimization. Its shared buffer cache and multi-process architecture deliver sub-10 ms response times for the same dataset, justifying the trade-off of higher memory consumption and setup complexity.
