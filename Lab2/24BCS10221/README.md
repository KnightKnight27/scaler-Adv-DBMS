# Database Systems Lab Submission

**Name:** MD Kaif Molla
**Role Number:** 24bcs10221

---

# Lab 2: SQLite3 vs PostgreSQL Exploration

## Objective

The goal of this lab was to create the same database in both SQLite3 and PostgreSQL, insert a large amount of data, and compare their storage behavior and query performance.

The comparison was mainly based on:

* Database page size
* Number of pages used
* Query execution speed
* Effect of memory-mapped I/O (`mmap`) in SQLite

---

# 1. SQLite3 Exploration

## Commands Used

### Creating the Table and Inserting 1,000,000 Rows

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT
);

WITH RECURSIVE cnt(x) AS (
    SELECT 1
    UNION ALL
    SELECT x + 1 FROM cnt WHERE x < 1000000
)
INSERT INTO users(id, name, email)
SELECT
    x,
    'User ' || x,
    'user' || x || '@example.com'
FROM cnt;
```

### Checking Database File Size

```bash
ls -lh sample.db
```

### Finding Page Size and Page Count

```sql
PRAGMA page_size;
PRAGMA page_count;
```

### Measuring Query Execution Time

```bash
# Without mmap
time sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM users;" > /dev/null

# With mmap
time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;" > /dev/null
```

---

## Observations

| Metric                  | Value             |
| ----------------------- | ----------------- |
| File Size               | 41 MB             |
| Page Size               | 4096 bytes (4 KB) |
| Page Count              | 10,605            |
| Query Time Without mmap | ~0.478 sec        |
| Query Time With mmap    | ~0.461 sec        |

---

## Analysis

SQLite stores data in fixed-size pages. In this experiment, each page size was 4 KB. Since the database contained 1,000,000 rows, SQLite used around 10,605 pages to store all the records.

When memory-mapped I/O (`mmap`) was enabled, the query execution became slightly faster. This happens because SQLite can access the database file directly through memory instead of repeatedly performing normal disk read operations.

The performance improvement was small but noticeable. Since the database was already cached by the operating system after the first run, the difference between normal reads and `mmap` was not extremely large.

---

# 2. PostgreSQL Exploration

## Commands Used

### Creating and Populating the Table

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    email TEXT
);

INSERT INTO users(name, email)
SELECT
    'User ' || x,
    'user' || x || '@example.com'
FROM generate_series(1, 1000000) AS x;

ANALYZE users;
```

### Finding Block Size and Page Count

```sql
SHOW block_size;

SELECT relpages
FROM pg_class
WHERE relname = 'users';
```

### Measuring Query Execution Time

```bash
time psql -d postgres -c "SELECT * FROM users;" > /dev/null
```

---

## Observations

| Metric               | Value             |
| -------------------- | ----------------- |
| Block Size           | 8192 bytes (8 KB) |
| Page Count           | 8,334             |
| Query Execution Time | ~3.025 sec        |

---

## Analysis

PostgreSQL uses a larger default page size compared to SQLite. The block size is 8 KB, which is double the size used in SQLite.

Because the pages are larger, PostgreSQL required fewer pages to store the same amount of data.

The query execution time was slower compared to SQLite in this test. One major reason is that PostgreSQL follows a client-server architecture. The database server processes the query and then sends the result to the client application (`psql`), which adds communication overhead.

SQLite, on the other hand, runs directly inside the application process, making it lightweight and faster for simple local operations.

---

# 3. SQLite3 vs PostgreSQL Comparison

| Feature              | SQLite3           | PostgreSQL             |
| -------------------- | ----------------- | ---------------------- |
| Page Size            | 4 KB              | 8 KB                   |
| Number of Pages      | 10,605            | 8,334                  |
| Query Execution Time | ~0.478 sec        | ~3.025 sec             |
| mmap Support         | Supported         | Not used directly      |
| Architecture         | Embedded Database | Client-Server Database |

---

# Comparison Discussion

## Storage Behavior

SQLite uses smaller pages, while PostgreSQL uses larger pages. Because PostgreSQL pages are bigger, it required fewer pages to store the same dataset.

However, PostgreSQL also stores additional internal information for transaction handling and concurrency management, which increases storage overhead.

---

## Query Performance

SQLite performed faster during the full table scan in this experiment.

This is mainly because SQLite is embedded directly into the application and does not require communication between separate processes.

PostgreSQL uses a dedicated database server, so additional time is spent handling client-server communication and result transfer.

---

## mmap Impact

Enabling `mmap` in SQLite slightly improved performance. Memory mapping allows SQLite to access database pages directly through memory, reducing some disk I/O overhead.

PostgreSQL does not use `mmap` in the same way. Instead, it mainly relies on its own shared buffer management and operating system caching.

---

# Conclusion

Both SQLite and PostgreSQL store data using fixed-size pages, but they are designed for different use cases.

* SQLite is lightweight, simple to use, and performs very well for local applications or small projects.
* PostgreSQL is more powerful and scalable, making it suitable for large applications with multiple users and advanced database requirements.

In this experiment, SQLite showed faster query execution for a simple full-table scan, while PostgreSQL demonstrated more advanced storage and server-side database management features.
