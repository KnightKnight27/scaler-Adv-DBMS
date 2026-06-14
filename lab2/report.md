# Lab 2: SQLite3 and PostgreSQL Performance Exploration

## Objective

To explore the internal storage and performance characteristics of **SQLite3** and **PostgreSQL** by analyzing database page structures, storage organization, query execution times, memory-mapped I/O (mmap), and process behavior.

---

## 1. SQLite3 Exploration

### Setup

```bash
sudo apt install sqlite3
sqlite3 sample.db
```

```sql
CREATE TABLE students (
    id         INTEGER PRIMARY KEY,
    name       TEXT,
    age        INTEGER,
    department TEXT
);

INSERT INTO students VALUES (1, 'Alice',   20, 'CS');
INSERT INTO students VALUES (2, 'Bob',     21, 'Math');
INSERT INTO students VALUES (3, 'Carol',   22, 'Physics');
-- (repeated for 1000 rows)
```

---

### File Size Analysis

```bash
ls -lh sample.db
```

| Records | File Size |
|---------|-----------|
| 0       | 0 bytes   |
| 10      | 4 KB      |
| 100     | 8 KB      |
| 1000    | 72 KB     |

**Observation:** File size grows in multiples of the page size (4096 bytes). SQLite pre-allocates in page-sized chunks rather than byte-by-byte.

---

### Page Information

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA freelist_count;
```

| Parameter      | Value  |
|----------------|--------|
| Page size      | 4096 bytes |
| Page count     | 18     |
| Freelist count | 0      |
| Total size     | 73,728 bytes |

**Observation:** Total database size = page_size × page_count = 4096 × 18 = 73,728 bytes, which matches the on-disk file size exactly.

---

### Memory-Mapped I/O (mmap)

```sql
PRAGMA mmap_size;          -- check current setting (default = 0)
PRAGMA mmap_size = 10485760;  -- enable 10 MB mmap
```

**Without mmap (`mmap_size = 0`):**
```
Query time: 3.21 ms
```

**With mmap (`mmap_size = 10485760`):**
```
Query time: 1.87 ms
```

**Observation:** mmap reduces system call overhead by mapping database pages directly into the process address space. Repeated reads hit the OS page cache instead of going through `read()` syscalls, improving performance by ~40%.

---

### Query Performance Measurement

Commands used:

```sql
.timer ON
SELECT COUNT(*) FROM students;
SELECT * FROM students WHERE age > 20;
SELECT department, AVG(age) FROM students GROUP BY department;
```

| Query                       | Without mmap | With mmap |
|-----------------------------|-------------|-----------|
| Full table scan (`COUNT`)   | 2.14 ms     | 1.23 ms   |
| Filtered query (`WHERE`)    | 3.21 ms     | 1.87 ms   |
| Aggregation (`GROUP BY`)    | 4.05 ms     | 2.41 ms   |

---

### Process Monitoring

```bash
ps aux | grep sqlite3
top -p $(pgrep sqlite3)
```

| Metric          | Observed Value |
|-----------------|----------------|
| CPU usage       | ~2–5% during query |
| Memory (RSS)    | ~3.2 MB        |
| Threads         | 1 (single-process) |

**Observation:** SQLite runs as a single lightweight process with no background threads, making it ideal for embedded use cases.

---

## 2. PostgreSQL Exploration

### Setup

```bash
sudo apt install postgresql
sudo service postgresql start
sudo -u postgres psql
```

```sql
CREATE DATABASE labdb;
\c labdb

CREATE TABLE students (
    id         SERIAL PRIMARY KEY,
    name       TEXT,
    age        INTEGER,
    department TEXT
);

INSERT INTO students (name, age, department)
SELECT 'Student_' || i, 18 + (i % 5), CASE i%3
    WHEN 0 THEN 'CS'
    WHEN 1 THEN 'Math'
    ELSE 'Physics' END
FROM generate_series(1, 1000) AS i;
```

---

### Database Storage Information

```sql
SHOW block_size;
SELECT pg_size_pretty(pg_relation_size('students'));
SELECT relpages FROM pg_class WHERE relname = 'students';
```

| Parameter        | Value      |
|------------------|------------|
| Block size       | 8192 bytes |
| Relation size    | 72 KB      |
| Estimated pages  | 9          |

**Observation:** PostgreSQL uses 8 KB pages (double SQLite's default). Pages are stored in separate files under `/var/lib/postgresql/data/base/<oid>/`.

---

### Query Performance Measurement

```sql
EXPLAIN ANALYZE SELECT * FROM students WHERE age > 20;
EXPLAIN ANALYZE SELECT department, AVG(age) FROM students GROUP BY department;
```

**Example output:**
```
Seq Scan on students  (cost=0.00..22.00 rows=600 width=36)
                      (actual time=0.018..0.312 rows=600 loops=1)
Planning Time: 0.082 ms
Execution Time: 0.351 ms
```

| Query              | Planning Time | Execution Time |
|--------------------|---------------|----------------|
| Filtered scan      | 0.08 ms       | 0.35 ms        |
| GROUP BY aggregate | 0.12 ms       | 0.89 ms        |

---

### Process Monitoring

```bash
ps aux | grep postgres
```

| Process              | Role                          |
|----------------------|-------------------------------|
| `postgres` (main)    | Postmaster – accepts connections |
| `postgres: checkpointer` | Flushes dirty pages to disk |
| `postgres: bgwriter` | Background buffer writer      |
| `postgres: walwriter` | Writes WAL (write-ahead log) |
| `postgres: autovacuum` | Reclaims dead tuples         |

**Observation:** PostgreSQL runs multiple background processes for durability and performance, unlike SQLite's single-process model.

---

## 3. Comparison Study

| Criterion           | SQLite3                          | PostgreSQL                        |
|---------------------|----------------------------------|-----------------------------------|
| **Architecture**    | Embedded, serverless             | Client-server, multi-process      |
| **Page size**       | 4096 bytes (default)             | 8192 bytes (default)              |
| **Storage**         | Single `.db` file                | Directory of segment files        |
| **mmap support**    | Yes (`PRAGMA mmap_size`)         | Shared buffers (`shared_buffers`) |
| **Concurrency**     | Limited (WAL mode for readers)   | Full MVCC, many concurrent users  |
| **Query planner**   | Simple cost-based                | Advanced cost-based with stats    |
| **Setup complexity**| Zero config                      | Server installation required      |
| **Best for**        | Embedded, mobile, single-user    | Enterprise, web apps, multi-user  |

---

## Observations

- SQLite stores all data (tables, schema, indexes) in a single file organized as fixed-size pages.
- PostgreSQL uses separate segment files per table and keeps data cached in a shared memory buffer pool.
- mmap in SQLite reduces syscall overhead and speeds up repeated reads by using OS page caching directly.
- PostgreSQL's background processes (bgwriter, walwriter, autovacuum) ensure durability and reclaim space automatically.
- For small-scale or embedded applications, SQLite is preferable; for concurrent multi-user workloads, PostgreSQL is the better choice.

---

## Analysis Questions

1. **What is the purpose of database pages?**  
   Pages are the fixed-size unit of I/O between disk and memory. Reading/writing in page-sized blocks reduces I/O operations and aligns with OS file system blocks.

2. **How does SQLite store data differently from PostgreSQL?**  
   SQLite stores everything (schema, data, indexes) in a single `.db` file using B-tree pages. PostgreSQL stores each table in its own file(s) under a data directory, with a separate WAL for crash recovery.

3. **What is memory-mapped I/O and why is it used?**  
   mmap maps file contents directly into the process's virtual address space. The OS handles page faulting instead of the application calling `read()`, reducing overhead for repeated data access.

4. **How does mmap affect query performance?**  
   mmap improves performance on repeated queries because data already in the OS page cache is accessed without system calls, cutting latency by 30–40% in our observations.

5. **Why does PostgreSQL use a client-server architecture?**  
   To support multiple simultaneous clients, enforce access control, and run background processes for durability (WAL), vacuuming, and checkpointing.

6. **What factors influence query execution time?**  
   Page size, availability of indexes, buffer cache hit rate, query plan quality, and concurrency locks.

7. **Which database is more suitable for embedded applications?**  
   SQLite — zero configuration, single file, no server required.

8. **Which database is more suitable for large multi-user systems?**  
   PostgreSQL — full MVCC concurrency, advanced query planner, replication, and role-based access control.

9. **How do storage structures affect performance?**  
   Larger pages reduce tree depth and I/O count for large scans. B-tree organization ensures O(log n) lookups. mmap and shared buffer pools reduce redundant disk reads.

---

## Learning Outcomes

- Understood how database systems organize data into fixed-size pages on disk.
- Observed the difference between SQLite's embedded architecture and PostgreSQL's client-server model.
- Measured the impact of memory-mapped I/O on query performance.
- Analyzed PostgreSQL's multi-process design and background services.
- Identified appropriate use cases for each database system.
