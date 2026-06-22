# scaler-Adv-DBMS
# SQLite vs PostgreSQL Comparison Report

**Name:** Piyush Kumar Mahato  

## Objective

The purpose of this lab was to install and explore SQLite and PostgreSQL, and compare them based on:

* Page Size
* Page Count
* Query Performance
* Impact of Memory Mapping (`mmap`) in SQLite

A `users` table containing 100,000 rows was created in both databases to ensure a fair comparison.

---

# 1. SQLite3 Exploration

## Installation

SQLite was installed using the precompiled command-line tools for Windows (`sqlite-tools-win-x64`).

## Database Setup

A database file named `sample.db` was created and populated with a `users` table containing 100,000 rows.

## Commands Used

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;

PRAGMA mmap_size = 268435456;
SELECT COUNT(*) FROM users;

PRAGMA mmap_size = 0;
SELECT COUNT(*) FROM users;

.timer on
SELECT * FROM users;
```

## Observations

| Metric            |                      Value |
| ----------------- | -------------------------: |
| Database File     |                `sample.db` |
| Number of Rows    |                    100,000 |
| Page Size         |          4096 bytes (4 KB) |
| Page Count        |                        513 |
| Default mmap Size |                    0 bytes |
| mmap Size Used    | 268,435,456 bytes (256 MB) |

## Query Performance (`SELECT COUNT(*) FROM users`)

| Mode         |              Real Time |
| ------------ | ---------------------: |
| With mmap    | 0.014022 s (14.022 ms) |
| Without mmap |  0.005009 s (5.009 ms) |

## Query Performance (`SELECT * FROM users`)

| Mode         |   Real Time |
| ------------ | ----------: |
| With mmap    | 51.161271 s |
| Without mmap | 49.117024 s |

## SQLite Analysis

SQLite stores the entire database in a single file, making it extremely lightweight and easy to deploy. The default page size is 4 KB. In this experiment, enabling memory mapping (`mmap`) did not improve performance; in fact, the query executed slightly faster without `mmap`.

The `SELECT *` query took nearly 50 seconds because the terminal had to print 100,000 rows. This overhead is caused by console output rather than the database engine itself. A better benchmark is `SELECT COUNT(*)`, which scans the table but returns only one row.

---

# 2. PostgreSQL Exploration

## Installation

PostgreSQL was installed using the official Windows installer.

## Database Setup

A database named `sampledb` was created and populated with a `users` table containing 100,000 rows.

## Commands Used

```sql
SHOW block_size;

SELECT pg_relation_size('users') /
       current_setting('block_size')::int AS page_count;

\timing
SELECT COUNT(*) FROM users;

SELECT * FROM users;
```

## Observations

| Metric                 |             Value |
| ---------------------- | ----------------: |
| Database Name          |        `sampledb` |
| Number of Rows         |           100,000 |
| Page Size (Block Size) | 8192 bytes (8 KB) |
| Page Count             |               637 |

## Query Performance

| Query                                      | Execution Time |
| ------------------------------------------ | -------------: |
| `SELECT COUNT(*) FROM users`               |      12.908 ms |
| `SELECT * FROM users` (cancelled manually) |      31.381 ms |

## PostgreSQL Analysis

PostgreSQL uses a client-server architecture and is designed for high concurrency and large-scale applications. Its default page size is 8 KB, which is twice the page size used by SQLite.

The `SELECT COUNT(*)` query completed in 12.908 ms, demonstrating excellent performance. The `SELECT *` query was manually cancelled after timing information had already been displayed.

---

# 3. Comparison Summary

| Metric                  |                                           SQLite |                        PostgreSQL |
| ----------------------- | -----------------------------------------------: | --------------------------------: |
| Architecture            |                   Embedded, single-file database |            Client-server database |
| Number of Rows          |                                          100,000 |                           100,000 |
| Page Size               |                                4096 bytes (4 KB) |                 8192 bytes (8 KB) |
| Page Count              |                                              513 |                               637 |
| `SELECT COUNT(*)` Time  | 14.022 ms (with mmap)<br>5.009 ms (without mmap) |                         12.908 ms |
| `SELECT *` Time         |  51.161 s (with mmap)<br>49.117 s (without mmap) |             31.381 ms (cancelled) |
| Memory Mapping (`mmap`) |                                        Supported |                    Not applicable |
| Database Storage        |                                Single `.db` file |     Multiple server-managed files |
| Setup Complexity        |                                      Very simple |                      More complex |
| Best Use Cases          |                       Embedded apps, local tools | Enterprise and multi-user systems |

---

# 4. Key Findings

1. **SQLite uses a smaller page size (4 KB)**, while PostgreSQL uses an 8 KB block size.
2. **PostgreSQL required more pages** because of additional metadata and storage overhead.
3. **Query performance for `SELECT COUNT(*)` was very similar** in both databases.
4. **SQLite performed faster without `mmap`** in this experiment.
5. **`SELECT *` timings were dominated by console output**, so they are not reliable indicators of database engine speed.
6. **SQLite is easier to install and use**, whereas PostgreSQL offers more advanced features and better scalability.

---

# 5. Conclusion

Both SQLite and PostgreSQL performed efficiently when executing `SELECT COUNT(*)` on a table containing 100,000 rows.

SQLite is a lightweight, file-based database that is ideal for small applications, local tools, and embedded systems. It requires minimal setup and is highly portable.

PostgreSQL is a robust, enterprise-grade relational database system with advanced capabilities such as concurrency control, security, replication, and extensibility.

In this experiment, PostgreSQL and SQLite demonstrated comparable performance for simple read queries. However, PostgreSQL is better suited for production systems with multiple users and large-scale workloads, while SQLite excels in simplicity and portability.

---

# 6. Files Submitted

* `README.md` (this report)

---

# 7. References

* SQLite Official Documentation
* PostgreSQL Official Documentation
