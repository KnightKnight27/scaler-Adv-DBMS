# SQLite3 vs PostgreSQL — Comparison Report

## Objective

Install and explore SQLite3 and PostgreSQL, and compare them based on:

- Page Size
- Page Count
- Query Performance
- Impact of Memory Mapping (`mmap`) in SQLite

A `users` table with **100,000 rows** was created in both databases for a fair comparison.

---

## 1. SQLite3 Exploration

### Installation

SQLite3 was installed using the precompiled command-line tools for Windows (`sqlite-tools-win-x64`).

### Database Setup

A database file `sample.db` was created and populated with a `users` table containing 100,000 rows.

### Commands Used

```bash
# Check file size on disk
ls -lh sample.db

# Check for running SQLite processes
ps aux | grep sqlite
```

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;

-- Enable mmap (256 MB) and time query
PRAGMA mmap_size = 268435456;
.timer on
SELECT COUNT(*) FROM users;

-- Disable mmap and time same query
PRAGMA mmap_size = 0;
SELECT COUNT(*) FROM users;

-- Full table scan
SELECT * FROM users;
```

### Observations

| Metric            | Value                      |
| ----------------- | -------------------------- |
| Database File     | `sample.db`                |
| Number of Rows    | 100,000                    |
| Page Size         | 4096 bytes (4 KB)          |
| Page Count        | 513                        |
| Default mmap Size | 0 bytes                    |
| mmap Size Used    | 268,435,456 bytes (256 MB) |

### Query Performance — `SELECT COUNT(*) FROM users`

| Mode         | Real Time              |
| ------------ | ---------------------- |
| With mmap    | 0.014022 s (14.022 ms) |
| Without mmap | 0.005009 s (5.009 ms)  |

### Query Performance — `SELECT * FROM users`

| Mode         | Real Time   |
| ------------ | ----------- |
| With mmap    | 51.161271 s |
| Without mmap | 49.117024 s |

### Analysis

SQLite stores the entire database in a single file. The default page size is 4 KB.

Enabling `mmap` did not improve performance — queries ran faster without it. Since the dataset fits comfortably in the OS page cache, memory-mapping provided no additional benefit.

The `SELECT *` timings (~50 s) are dominated by console I/O overhead from printing 100,000 rows, not database engine speed. `SELECT COUNT(*)` is a more reliable benchmark.

---

## 2. PostgreSQL Exploration

### Installation

PostgreSQL was installed using the official Windows installer.

### Database Setup

A database `sampledb` was created and populated with a `users` table containing 100,000 rows.

### Commands Used

```sql
SHOW block_size;

SELECT pg_relation_size('users') /
       current_setting('block_size')::int AS page_count;

\timing
SELECT COUNT(*) FROM users;
SELECT * FROM users;
```

### Observations

| Metric                 | Value             |
| ---------------------- | ----------------- |
| Database Name          | `sampledb`        |
| Number of Rows         | 100,000           |
| Page Size (Block Size) | 8192 bytes (8 KB) |
| Page Count             | 637               |

### Query Performance

| Query                         | Execution Time              |
| ----------------------------- | --------------------------- |
| `SELECT COUNT(*) FROM users`  | 12.908 ms                   |
| `SELECT * FROM users`         | 31.381 ms (cancelled early) |

### Analysis

PostgreSQL uses a client-server architecture designed for high concurrency. Its default block size is 8 KB — twice that of SQLite.

`SELECT COUNT(*)` completed in 12.908 ms. The `SELECT *` query was manually cancelled after timing data was captured.

---

## 3. Comparison Summary

| Metric                  | SQLite                                                      | PostgreSQL                        |
| ----------------------- | ----------------------------------------------------------- | --------------------------------- |
| Architecture            | Embedded, single-file database                              | Client-server database            |
| Number of Rows          | 100,000                                                     | 100,000                           |
| Page Size               | 4096 bytes (4 KB)                                           | 8192 bytes (8 KB)                 |
| Page Count              | 513                                                         | 637                               |
| `SELECT COUNT(*)` Time  | 14.022 ms (with mmap) / 5.009 ms (without mmap)             | 12.908 ms                         |
| `SELECT *` Time         | ~51 s (with mmap) / ~49 s (without mmap) — I/O bound        | 31.381 ms (cancelled)             |
| Memory Mapping (`mmap`) | Supported; disabled by default                              | Not applicable                    |
| Storage                 | Single `.db` file                                           | Multiple server-managed files     |
| Setup Complexity        | Minimal                                                     | Moderate                          |
| Best Use Cases          | Embedded apps, local tools, prototyping                     | Enterprise and multi-user systems |

---

## 4. Key Findings

1. SQLite uses a smaller page size (4 KB); PostgreSQL uses 8 KB blocks.
2. PostgreSQL required more pages due to additional metadata and storage overhead.
3. `SELECT COUNT(*)` performance was comparable across both databases.
4. Disabling `mmap` in SQLite was faster for this workload — the dataset fits in the OS cache, so mmap added overhead rather than helping.
5. `SELECT *` timings are not reliable benchmarks due to console output dominating execution time.
6. SQLite is simpler to install and use; PostgreSQL offers more advanced features and better scalability.

---

## 5. Conclusion

Both databases handled `SELECT COUNT(*)` on 100,000 rows efficiently with similar execution times.

SQLite is a lightweight, serverless, file-based database best suited for embedded systems, local tools, and small-scale applications where portability matters.

PostgreSQL is a full-featured, enterprise-grade RDBMS built for concurrent users, complex queries, and large-scale deployments.

For simple read workloads, both are comparable. For production systems requiring multi-user access, reliability, and scalability, PostgreSQL is the better choice.

---

## References

- [SQLite Official Documentation](https://www.sqlite.org/docs.html)
- [PostgreSQL Official Documentation](https://www.postgresql.org/docs/)
