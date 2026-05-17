# SQLite3 vs PostgreSQL Comparison

## Objective

The goal of this lab was to explore and compare SQLite3 and PostgreSQL in terms of:

- Storage architecture
- Page size
- Page count
- Query performance
- Memory optimization
- Process behavior

---
To see/run all the experiments I did you can run the `comparision.sh`
```bash
chmod +x comparision.sh
./comparision.sh
```
---

# SQLite3 Exploration

## Database Creation

```sql
CREATE TABLE users(
    id INTEGER PRIMARY KEY,
    name TEXT
);

INSERT INTO users(name)
VALUES ('Alice'), ('Bob'), ('Charlie');
```

---

## File Size Observation

Command used:

```bash
ls -lh
```

Observation:

- SQLite stores the entire database in a single `.db` file.
- Database file size was approximately **8.2 KB**.

---

## Page Information

Commands used:

```sql
PRAGMA page_size;
PRAGMA page_count;
```

Results:

| Property | Value |
|---|---|
| Page Size | 4096 bytes |
| Page Count | 2 |

Observation:

- SQLite uses fixed-size pages for storage.
- The database occupied only 2 pages.

---

## mmap Experiment

Commands used:

```sql
PRAGMA mmap_size;
PRAGMA mmap_size=268435456;
```

Results:

| Setting | Value |
|---|---|
| Default mmap_size | 0 |
| Modified mmap_size | 268435456 |

Observation:

- SQLite allows direct memory-mapped I/O optimization.
- Enabling mmap slightly improved query performance.

---

## Query Timing

Commands used:

```sql
.timer on

SELECT * FROM users;

PRAGMA mmap_size=268435456;

SELECT * FROM users;
```

Results:

| Configuration | Observed Time |
|---|---|
| Without mmap | ~0.001 sec |
| With mmap | ~0.000 sec |

Observation:

- SQLite query execution was extremely fast for the small dataset.
- Enabling `mmap_size` slightly reduced query execution time.
- SQLite performed very efficiently because it is an embedded database with minimal overhead and direct file access.

---

## Process Observation

Command used:

```bash
ps aux | grep sqlite
```

Observation:

- SQLite runs as a single lightweight process.
- No separate server process exists.

---

# PostgreSQL Exploration

## Database Creation

Commands used:

```sql
CREATE DATABASE testdb;

CREATE TABLE users(
    id SERIAL PRIMARY KEY,
    name TEXT
);

INSERT INTO users(name)
VALUES ('Alice'), ('Bob'), ('Charlie');
```

---

## Database Size

Command used:

```sql
SELECT pg_size_pretty(pg_database_size('testdb'));
```

Result:

- Database size: approximately **8078 kB**

Observation:

- PostgreSQL uses significantly more storage than SQLite.
- Additional storage is used for metadata, WAL, catalogs, and internal management.

---

## Page Information

Command used:

```sql
SHOW block_size;
```

Result:

| Property | Value |
|---|---|
| Block Size | 8192 bytes |

Approximate page count:

```sql
SELECT
    pg_relation_size('users') / current_setting('block_size')::int
    AS approx_page_count;
```

Result:

| Property | Value |
|---|---|
| Approximate Page Count | 1 |

Observation:

- PostgreSQL uses larger default pages (8 KB).

---

## Query Timing

Commands used:

```sql
\timing
SELECT * FROM users;
```

Results:

| Attempt | Time |
|---|---|
| First | 0.664 ms |
| Second | 0.571 ms |
| Third | 0.508 ms |
| Fourth | 0.446 ms |

Observation:

- Query performance improved on repeated execution due to caching.

---

## Shared Memory Information

Commands used:

```sql
SHOW shared_buffers;
SHOW effective_cache_size;
```

Results:

| Setting | Value |
|---|---|
| shared_buffers | 128MB |
| effective_cache_size | 4GB |

Observation:

- PostgreSQL internally manages memory using shared buffers and caching.
- Unlike SQLite, it does not expose direct mmap tuning.

---

## Query Plan Analysis

Command used:

```sql
EXPLAIN ANALYZE SELECT * FROM users;
```

Observation:

- PostgreSQL performed a sequential scan.
- Query was served from shared buffer cache.
- Execution time was extremely low (~0.040 ms).

---

## Process Observation

Command used:

```bash
ps aux | grep postgres
```

Observation:

PostgreSQL runs multiple background processes including:

- logger
- checkpointer
- WAL writer
- autovacuum launcher
- IO workers

This demonstrates PostgreSQL's client-server architecture.

---

# Comparison Between SQLite3 and PostgreSQL

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | File-based | Client-server |
| Storage | Single `.db` file | Multiple internal files |
| Default Page Size | 4096 B | 8192 B |
| Database Size | 8.2 KB | 8078 KB |
| Memory Optimization | mmap_size | shared_buffers |
| Processes | Single process | Multiple background processes |
| Query Performance | Very fast | Very fast with caching |
| Complexity | Simple | More complex |
| Best Use Case | Embedded/local apps | Multi-user scalable systems |

---

# Final Conclusion

SQLite is lightweight, simple, and ideal for embedded or local applications.  
It provides direct control over memory mapping and stores the database in a single file.

PostgreSQL is more advanced and follows a client-server architecture.  
It uses internal memory management, background workers, and shared buffers to support scalability, concurrency, and reliability.

Both databases showed excellent query performance, but PostgreSQL provides significantly more advanced database management features at the cost of higher resource usage and learning curve and configuration. 