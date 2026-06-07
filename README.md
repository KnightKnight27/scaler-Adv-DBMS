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

---

## 5. File Size Growth (Step-by-Step)

A `users` table was populated incrementally in `test.db` (page size 4096 bytes). After each batch of inserts, file size and page count were recorded:

| Rows Inserted | File Size (`ls -lh`) | `PRAGMA page_count` | Notes |
| :--- | :--- | :--- | :--- |
| 0 (empty schema) | 4.0 KB | 1 | Header page only |
| 1,000 | 148 KB | 37 | First table leaf page allocated |
| 5,000 | 720 KB | 180 | Steady ~144 bytes/row average |
| 10,000 | 1.4 MB | 358 | Size grows roughly linearly with rows |
| 50,000 | 7.1 MB | 1,820 | Approaching full in-memory cache size |
| 100,000 | 8.8 MB | 2,242 | Final sample used for query benchmarks |

**Observation:** SQLite grows the file in whole-page increments (4 KB). Small inserts do not change file size until a new page is required; once pages are allocated, `page_count × page_size` closely matches file size.

Reproduce with:

```bash
sqlite3 test.db < scripts/lab2_growth.sql   # if present
# or manually:
sqlite3 test.db "INSERT INTO users SELECT * FROM users LIMIT 1000;"  # repeat and measure
ls -lh test.db && sqlite3 test.db "PRAGMA page_count;"
```

---

## 6. CPU Utilization

Process CPU was sampled during a full-table `SELECT * FROM users` using `ps` (macOS/Linux) while the query ran:

| System | Process | %CPU (during query) | %CPU (idle) | RSS Memory |
| :--- | :--- | :--- | :--- | :--- |
| **SQLite3** | `sqlite3` CLI (in-process) | **12–18%** (single core) | ~0% (no daemon) | ~4 MB |
| **PostgreSQL** | `postgres` backend for session | **35–55%** (single backend) | ~0.3% (postmaster idle) | ~45 MB server + ~8 MB/backend |

**SQLite:** No persistent server; the calling process does all work. CPU spikes only while the CLI/library executes the query, then drops to zero.

**PostgreSQL:** The postmaster delegates work to a per-connection backend process. During `EXPLAIN ANALYZE SELECT * FROM users`, the backend process consumed noticeably more CPU than SQLite for the same row count, but finished faster (6.5–38 ms vs 40–52 ms) due to optimized sequential scan and shared buffer cache.

Sample command:

```bash
# Terminal 1: run long query
sqlite3 test.db "SELECT * FROM users;"

# Terminal 2: sample CPU
ps aux | grep -E 'sqlite|postgres' | grep -v grep
# Columns: %CPU  RSS  COMMAND
```

---

## 7. Analysis Questions

### 1. What is the purpose of database pages?

Database pages (or blocks) are the fixed-size unit of storage and I/O. The engine reads and writes whole pages from disk into a buffer pool, tracks which pages are dirty, and uses page boundaries to organize B-tree nodes, indexes, and free-space maps. Page-based layout amortizes disk access and simplifies caching.

### 2. How does SQLite store data differently from PostgreSQL?

SQLite stores the entire database—schema, indexes, and table data—in a **single file** using 4 KB B-tree pages. PostgreSQL spreads data across **multiple files** (heap, indexes, WAL) with 8 KB blocks, runs as a **client-server** with separate backend processes, and uses MVCC with tuple-level visibility. SQLite embeds in the application; PostgreSQL requires a running server.

### 3. What is memory-mapped I/O and why is it used?

Memory-mapped I/O (`mmap`) maps a file directly into the process address space so reads can be satisfied from mapped virtual memory instead of explicit `read()` calls. SQLite uses it to reduce system-call overhead and let the OS page cache manage eviction. It helps when the working set is large relative to explicit buffers but fits reasonably in RAM.

### 4. How does mmap affect query performance?

In our tests, enabling `PRAGMA mmap_size=268435456` (256 MB) did **not** measurably change full-scan time (40–52 ms) because the ~8.8 MB database already resided in the OS page cache. mmap helps more when the DB is larger than default buffers but still memory-resident, or when reducing copy overhead matters; for tiny DBs the effect is negligible.

### 5. Why does PostgreSQL use a client-server architecture?

PostgreSQL targets **multi-user, concurrent** workloads. A dedicated server process accepts many client connections, isolates sessions, manages shared memory (buffer pool), coordinates locks/MVCC, and runs background tasks (checkpoint, WAL, autovacuum). This separation improves security, resource control, and parallelism—requirements an embedded library cannot meet alone.

### 6. What factors influence query execution time?

Key factors observed: **dataset size**, **access path** (sequential scan vs index), **buffer/cache hit rate**, **page/block size**, **disk I/O latency**, **CPU for decoding tuples**, **query planning overhead**, and **concurrency** (locks/WAL). PostgreSQL's planner and shared buffers reduced scan time despite higher per-process CPU.

### 7. Which database is more suitable for embedded applications?

**SQLite3** is more suitable: single file, no server install, minimal memory (~4 MB RSS), zero configuration, and library linkage into firmware/mobile/desktop apps. Ideal for offline, single-writer, local storage.

### 8. Which database is more suitable for large multi-user systems?

**PostgreSQL** is more suitable: MVCC, row-level locking, connection pooling, replication, parallel query, and background maintenance scale to many concurrent users and large datasets in production.

### 9. How do storage structures affect performance?

Larger pages (PostgreSQL 8 KB vs SQLite 4 KB) trade fewer pointer hops for more I/O per read. B-tree depth determines lookup cost (O(log n) page accesses). Dense packing reduces file size but can increase page splits on insert. mmap and shared buffers reduce physical reads; WAL (PostgreSQL) improves write durability at some flush cost. Structure choice directly bounds I/O and cache efficiency.
