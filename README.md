# scaler-Adv-DBMS

# Advanced DBMS Lab Report

**Role Number:** 10293
**Name:** Parth Agarwal

## Objective

The objective of this lab is to compare SQLite3 and PostgreSQL using a sample database and observe:

- Database file/storage size
- Page size
- Page count
- Query execution time
- Impact of memory mapping, where applicable

No screenshots are included as per the submission guidelines.

## System Details

| Item | Value |
| --- | --- |
| Operating System | Windows 10 Home Single Language 25H2, build 26200, AMD64 |
| SQLite Version | SQLite 3.53.1, Windows x64 command-line tools |
| PostgreSQL Version | PostgreSQL 18.3, Windows x64 binaries |
| Sample Database | `users` table with 10,000 rows |

> Note: SQLite was installed using the official SQLite Windows x64 tools archive. PostgreSQL was installed using the official EDB PostgreSQL Windows x64 binaries archive and started locally on port `55432`.

## SQLite3 Exploration

### Commands Used

```bash
sqlite3 sample.db
```

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
PRAGMA mmap_size = 0;
PRAGMA mmap_size = 268435456;
SELECT * FROM users;
```

```bash
ls -lh
time sqlite3 sample.db "SELECT * FROM users;"
ps aux | grep sqlite
```

### Observations

| Metric | Observation |
| --- | --- |
| Database file size | 384.00 KB |
| Page size | 4096 bytes |
| Page count | 96 |
| mmap size before change | 0 |
| mmap size after change | 268435456 |
| Query time without mmap | 128.387 ms |
| Query time with mmap | 138.810 ms |

### SQLite3 Analysis

SQLite stores the complete database in a single file. The total database size can be estimated using:

```text
page_size * page_count
```

Changing `mmap_size` allows SQLite to map database pages into memory. For read-heavy queries, this may reduce file I/O overhead and improve execution time. The impact depends on database size, operating system caching, available memory, and the query being executed.

In this experiment, `PRAGMA mmap_size = 268435456` successfully changed the mmap setting from `0` to `268435456`. However, the measured `SELECT * FROM users;` query was slightly slower with mmap enabled. For this small 384 KB database, mmap did not improve performance because the dataset was already small enough to be handled efficiently by normal file and operating system caching.

## PostgreSQL Setup

### Commands Used

```bash
psql --version
psql -U postgres
```

```sql
CREATE DATABASE lab_db;
\c lab_db

CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100),
    email VARCHAR(150)
);

INSERT INTO users (name, email)
SELECT
    'User ' || generate_series,
    'user' || generate_series || '@example.com'
FROM generate_series(1, 10000);

SHOW block_size;
SELECT pg_relation_size('users');
SELECT pg_total_relation_size('users');
SELECT relpages FROM pg_class WHERE relname = 'users';
\timing on
SELECT * FROM users;
```

```bash
ps aux | grep postgres
```

### Observations

| Metric | Observation |
| --- | --- |
| PostgreSQL block/page size | 8192 bytes |
| Table size | 672.00 KB |
| Total relation size | 936.00 KB |
| Page count / relpages | 84 |
| Query execution time | 106.446 ms |

### PostgreSQL Analysis

PostgreSQL is a server-based relational database system. Unlike SQLite, data is not stored as one simple application-level database file. PostgreSQL manages storage internally using relations, pages, indexes, catalogs, background processes, and shared buffers.

The default PostgreSQL page size is commonly 8 KB. Page count can be observed using catalog information such as `pg_class.relpages`, while relation size can be checked using `pg_relation_size()` and `pg_total_relation_size()`.

PostgreSQL does not expose a direct `mmap_size` setting like SQLite for normal query tuning. Instead, it relies on its own memory management features such as shared buffers, the operating system page cache, and query planner optimizations.

## SQLite3 vs PostgreSQL Comparison

| Feature | SQLite3 | PostgreSQL |
| --- | --- | --- |
| Architecture | Embedded database | Client-server database |
| Storage model | Single database file | Managed data directory with relations and internal files |
| Page size | 4096 bytes | 8192 bytes |
| Page count | 96 | 84 |
| Query performance | 128.387 ms without mmap; 138.810 ms with mmap | 106.446 ms |
| mmap support | Supports `PRAGMA mmap_size`; mmap was enabled but did not improve this small query | No direct equivalent for normal query execution |
| Best suited for | Lightweight local storage, small apps, embedded use | Multi-user systems, large applications, concurrent workloads |

## Comparison Analysis

SQLite3 is simple to install and use because it works directly with a database file. It is useful for local applications, testing, embedded systems, and smaller workloads. Page size and page count can be checked directly using `PRAGMA` commands. The `mmap_size` setting can affect read performance by allowing memory-mapped access to the database file.

PostgreSQL requires a running database server and has more setup steps, but it provides stronger support for concurrent users, authentication, indexing, query planning, transactions, and large-scale applications. Its storage is more complex than SQLite because it manages tables, indexes, metadata, and background processes separately.

For this lab, PostgreSQL completed the full table scan slightly faster than SQLite with mmap enabled, while SQLite without mmap was also close in timing. SQLite reported a 4096-byte page size and 96 pages, while PostgreSQL reported an 8192-byte block size and 84 relation pages. The mmap experiment showed that enabling mmap does not always improve performance, especially when the database is small and already benefits from normal caching.

## Conclusion

SQLite3 is easier to use and inspect for file-level experiments, while PostgreSQL is more powerful for production-style database workloads. SQLite's `mmap_size` can influence read performance, but PostgreSQL uses a different memory and storage architecture, so the same mmap experiment does not directly apply.

Based on the observations, PostgreSQL is better suited for multi-user and scalable applications, while SQLite3 is better suited for lightweight, local, and embedded database use cases.
