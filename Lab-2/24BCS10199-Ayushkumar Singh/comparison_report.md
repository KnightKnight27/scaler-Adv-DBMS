# Database Comparison Report: SQLite3 vs PostgreSQL

## 1. SQLite3 Exploration

### Environment and Setup
- **Sample Database**: Created a database with a `users` table containing 100,000 rows.
- **File Size**:
  ```bash
  $ ls -lh sample.db
  -rw-r--r--  1 ayushsingh  staff   16M May  9 18:16 sample.db
  ```

### Page Statistics (PRAGMA)
- **Page Size**: 4096 bytes
- **Page Count**: 3986

### MMAP Experiment
We experimented with `mmap_size` to observe the performance impact of memory-mapped I/O.
- **Without MMAP (`mmap_size = 0`)**: ~0.5270 seconds
- **With MMAP (`mmap_size = 256MB`)**: ~0.3296 seconds
- **Observation**: Memory mapping significantly improved read performance by reducing syscall overhead for large sequential scans.

### Process Observation
SQLite is a library-based database, so it runs within the host process.
```bash
$ ps aux | grep python3 | grep keep_sqlite_open
ayushsingh       56728   0.0  0.0 410387984   1520   ??  S     6:17PM   0:00.01 python3 keep_sqlite_open.py
```

---

## 2. PostgreSQL Exploration

### Environment and Setup
- **Server Version**: PostgreSQL 14.22
- **Data Volume**: 100,000 rows in a `users` table.

### Page Statistics
- **Block (Page) Size**: 8192 bytes
- **Page Count (relpages)**: 2223

### Query Execution
- **SELECT * FROM users**: ~0.749 seconds
- **Memory Management**: PostgreSQL uses `shared_buffers` (configured at 128MB) for caching data pages in memory, which serves a similar purpose to MMAP in SQLite but is managed by the DB buffer manager rather than directly by the OS kernel's MMAP.

---

## 3. Comparison Summary

| Metric | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| **Default Page Size** | 4096 bytes (4KB) | 8192 bytes (8KB) |
| **Page Count (100k rows)** | 3986 | 2223 |
| **Query Performance** | ~0.53s (No MMAP) / ~0.33s (MMAP) | ~0.75s |
| **MMAP Impact** | High (~40% faster) | Managed via `shared_buffers` |
| **Architecture** | Serverless / Embedded Library | Client-Server Architecture |
| **File Storage** | Single `.db` file | Directory-based cluster |

### Conclusion
- **SQLite3** is highly efficient for local, single-user scenarios and shows significant performance boosts when leveraging `mmap`. Its smaller page size is better for small, fragmented reads.
- **PostgreSQL** uses a larger default page size (8KB), which is optimized for enterprise workloads and high-concurrency environments. While the raw `SELECT *` time was slightly higher in this single-threaded test due to client-server overhead, its scalability and robust buffering (`shared_buffers`) make it superior for large-scale applications.
