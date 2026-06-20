# scaler-Adv-DBMS
# SQLite3 vs PostgreSQL: A Detailed System Comparison

## Overview
This report outlines an experimental comparison between two distinct database models: **SQLite3** (a serverless, embedded engine) and **PostgreSQL** (a powerful client-server RDBMS). Using local benchmarks, we analyzed differences in file storage, memory management, query speed, and architectural processes to determine their respective operational strengths.

---

## 1. SQLite3: Embedded Database Evaluation

### Experimental Setup
The following commands were executed to analyze the behavior of an SQLite3 database:

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
- **Storage Footprint**: All data is packed into a single, highly portable `.db` file. In our tests, its size ranged from **3.7 MB to 8.8 MB** based on the volume of inserted data.
- **Page Configuration**: The database utilizes a fixed page size of **4096 bytes (4 KB)**. Our test cases revealed a total page count between 931 and 2242.
- **Memory-Mapped I/O (mmap)**: By default, `mmap_size` is disabled (0). We temporarily configured it to 256 MB (268,435,456 bytes) for a single session to test performance.
- **Query Performance**: Conducting a full table scan took approximately **40–52 ms**. Enabling `mmap` did not yield faster results because the entire database easily fit into the operating system's page cache, making memory mapping unnecessary for datasets of this scale.
- **Process Model**: SQLite3 operates completely within the host application's process. Running a system process check (`ps aux`) confirmed that there are no standalone background servers or daemons running.

---

## 2. PostgreSQL: Client-Server Architecture Evaluation

### Experimental Setup
We ran the following commands to investigate the PostgreSQL architecture:

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
- **Storage Footprint**: The `users` table occupied between **6.4 MB and 7.5 MB**. This is noticeably larger than SQLite's entire database size due to PostgreSQL maintaining a separate Write-Ahead Log (WAL) and requiring extra bytes per row to manage concurrent transactions.
- **Block Configuration**: PostgreSQL uses a default block size of **8192 bytes (8 KB)**, which is twice as large as SQLite's. The table spanned across 824 to 935 pages.
- **Query Performance**: A full table scan completed in **6.5 ms to 37.8 ms**. The query planner executed a sequential scan instantly (~0.2 ms overhead). Overall, PostgreSQL was substantially faster—**25% to 6× faster** than SQLite—showcasing the efficiency of its execution engine.
- **Process Model**: PostgreSQL relies on a robust multi-process architecture. A process check revealed several active background daemons handling specific tasks, illustrating a system built for heavy concurrency:
  - Main `postmaster` server process
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
| **Architecture** | Embedded library (Serverless) | Client-server system (Multi-process) |
| **Storage Model** | Single consolidated `.db` file | Multi-file structure with separate WAL |
| **Page/Block Size** | 4 KB (4096 bytes) | 8 KB (8192 bytes) |
| **Table/DB Size** | 3.7 – 8.8 MB (Entire Database) | 6.4 – 7.5 MB (Single Table only) |
| **Memory Mapping (mmap)** | Supported (User-configurable per session)| Unnecessary; uses a dedicated shared buffer |
| **Full Scan Query Time**| 40 – 52 ms | 6.5 – 38 ms |
| **Background Processes**| None (Runs within app thread) | Multiple (`checkpointer`, `walwriter`, etc.) |
| **Concurrency Support** | Database-level lock (Single active writer)| Row-level locks with full MVCC support |
| **Setup Complexity** | Plug-and-play (Zero config) | Moderate (Requires installation/service start) |
| **Idle Resource Usage** | Negligible (~4 MB RSS) | High (~45 MB baseline + per-connection cost) |

---

## 4. Conclusion

- **SQLite3** stands out as an excellent, frictionless choice for local storage, mobile applications, and rapid prototyping. Its single-file portability and zero-configuration design provide tremendous value. While advanced features like `mmap` exist, they provide little to no benefit when dealing with small databases that already fit within system memory caches.
- **PostgreSQL** is engineered for high-stakes production environments that require advanced query optimization, strict data integrity, and high concurrent throughput. Though it demands a larger memory footprint and more complex setup, its sophisticated shared buffer cache and dedicated background workers deliver unparalleled performance, executing queries significantly faster than embedded alternatives.