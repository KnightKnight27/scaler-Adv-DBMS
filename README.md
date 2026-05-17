# scaler-Adv-DBMS
# SQLite3 vs PostgreSQL Comparison Report

## Observations

This report compares SQLite and PostgreSQL in terms of storage architecture, performance, page management, and memory-mapped I/O behavior.

SQLite3 is a lightweight embedded database stored in a single file, while PostgreSQL is a full-featured client-server relational database system designed for concurrency and scalability.

---

# Commands Used

## SQLite3 Commands

```bash
# Open database
sqlite3 test.db

# Check page size
PRAGMA page_size;

# Check page count
PRAGMA page_count;

# Enable mmap
PRAGMA mmap_size = 268435456;

# Analyze database
ANALYZE;

# Exit
.exit
```

## PostgreSQL Commands

```bash
# Connect to PostgreSQL
psql -U postgres

# Create database
CREATE DATABASE testdb;

# Connect to database
\c testdb

# Get page size
SHOW block_size;

# Get database size
SELECT pg_database_size('testdb');

# Analyze performance
EXPLAIN ANALYZE
SELECT * FROM users WHERE id = 100;

# Exit
\q
```

---

# Comparison Analysis

| Feature           | SQLite3                                    | PostgreSQL                                   |
| ----------------- | ------------------------------------------ | -------------------------------------------- |
| Database Type     | Embedded database                          | Client-server database                       |
| Default Page Size | Usually 4096 bytes                         | Usually 8192 bytes                           |
| Page Count        | Determined using `PRAGMA page_count`       | Internally managed                           |
| Storage           | Single file                                | Multiple files and directories               |
| Concurrency       | Limited write concurrency                  | High concurrency with MVCC                   |
| Query Performance | Fast for small/local workloads             | Better for large concurrent workloads        |
| mmap Support      | Direct mmap support via `PRAGMA mmap_size` | Relies mainly on shared buffers and OS cache |
| Setup Complexity  | Very simple                                | Requires server setup                        |
| Best Use Case     | Mobile apps, embedded systems              | Enterprise systems, large applications       |

---

# Page Size

## SQLite3

SQLite stores data in fixed-size pages inside a single database file.

Example:

```sql
PRAGMA page_size;
```

Typical output:

```text
4096
```

* Smaller page sizes reduce wasted space.
* Larger page sizes can improve sequential read performance.

---

## PostgreSQL

PostgreSQL uses blocks (pages) internally.

Command:

```sql
SHOW block_size;
```

Typical output:

```text
8192
```

* PostgreSQL defaults to 8 KB pages.
* Optimized for large-scale transactional systems.

---

# Page Count

## SQLite3

Page count can be directly queried:

```sql
PRAGMA page_count;
```

Database size formula:

```text
Database Size = page_size × page_count
```

SQLite exposes low-level storage details very clearly.

---

## PostgreSQL

PostgreSQL manages pages internally and does not expose simple page counts directly.

Instead, database size is checked using:

```sql
SELECT pg_database_size('testdb');
```

PostgreSQL abstracts storage management more heavily than SQLite.

---

# Query Performance

## SQLite3

Advantages:

* Extremely fast for local reads.
* Minimal overhead.
* Excellent for single-user applications.

Limitations:

* Database-level write locking.
* Reduced performance under heavy concurrency.

Best suited for:

* Mobile applications
* Desktop applications
* Small web apps
* Embedded systems

---

## PostgreSQL

Advantages:

* Better optimization engine.
* Advanced indexing support.
* Handles concurrent users efficiently.
* Supports parallel execution and MVCC.

Limitations:

* Higher memory usage.
* Requires server processes.

Best suited for:

* Enterprise applications
* High-traffic systems
* Large-scale analytics
* Multi-user systems

---

# mmap Impact

## SQLite3 mmap

SQLite supports memory-mapped I/O directly.

Example:

```sql
PRAGMA mmap_size = 268435456;
```

Benefits:

* Faster read performance.
* Reduced system calls.
* Lower CPU overhead for repeated reads.

Drawbacks:

* Large mmap values may increase memory pressure.
* Platform-dependent behavior.

SQLite gains noticeable read performance improvements from mmap in read-heavy workloads.

---

## PostgreSQL mmap

PostgreSQL does not rely heavily on mmap for database pages.

Instead, it uses:

* Shared buffers
* WAL (Write Ahead Logging)
* OS page cache

PostgreSQL focuses more on:

* Reliability
* Crash recovery
* Concurrent transaction consistency

Thus mmap has less direct impact compared to SQLite.

---

# Final Conclusion

* SQLite is lightweight, portable, and ideal for embedded or single-user applications.
* PostgreSQL is more powerful, scalable, and suitable for enterprise-level workloads.
* SQLite provides simpler visibility into pages and mmap behavior.
* PostgreSQL offers stronger concurrency, indexing, and optimization features.

For small-scale applications with minimal concurrency, SQLite is often faster and easier to manage.

For production systems with many users and complex queries, PostgreSQL is the preferred choice.
