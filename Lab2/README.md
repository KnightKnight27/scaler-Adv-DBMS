# Advanced DBMS Lab — SQLite3 vs PostgreSQL

**Name:** Mayank Soni
**Roll No:** 24BCS10127

---

## 1. SQLite3 Exploration

### Setup

SQLite3 was already available via miniconda.

```bash
sqlite3 --version
# 3.51.0 2025-11-04 19:38:17
```

### Sample Database

Created `sample.db` with two tables — `users` (10,000 rows) and `orders` (20,000 rows).

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT NOT NULL,
    age INTEGER,
    city TEXT,
    created_at TEXT DEFAULT (datetime('now'))
);

CREATE TABLE orders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER,
    product TEXT,
    amount REAL,
    order_date TEXT DEFAULT (datetime('now')),
    FOREIGN KEY (user_id) REFERENCES users(id)
);
```

Data was inserted using recursive CTEs.

### File Size

```bash
ls -lh sample.db
# -rw-r--r--  1.5M May 9 20:03 sample.db
```

**Observation:** The entire database (30,000 rows) fits in a single 1.5 MB file — a key SQLite characteristic.

### PRAGMA Commands

```bash
sqlite3 sample.db "PRAGMA page_size;"    # 4096
sqlite3 sample.db "PRAGMA page_count;"   # 383
sqlite3 sample.db "PRAGMA mmap_size;"    # 0
sqlite3 sample.db "PRAGMA journal_mode;" # delete
sqlite3 sample.db "PRAGMA cache_size;"   # -2000
sqlite3 sample.db "PRAGMA freelist_count;" # 0
```

| PRAGMA           | Value  | Meaning                                  |
|------------------|--------|------------------------------------------|
| `page_size`      | 4096   | Each page is 4 KB                        |
| `page_count`     | 383    | 383 pages total                          |
| `mmap_size`      | 0      | Memory-mapped I/O disabled by default    |
| `journal_mode`   | delete | Rollback journal deleted after each txn  |
| `cache_size`     | -2000  | ~2 MB page cache (negative = KiB units)  |
| `freelist_count` | 0      | No unused pages; DB is compact           |

**Verification:** 383 × 4096 = 1,568,768 bytes ≈ 1.5 MB ✓

### mmap Experiment

`mmap_size` controls whether SQLite uses memory-mapped I/O instead of standard `read()`/`write()` system calls.

- `mmap_size = 0` → disabled (default); uses `read()` calls per page
- `mmap_size = 268435456` → enabled (256 MB); OS virtual memory handles paging directly

When enabled, the OS page cache can serve data without any system call, reducing overhead on repeated scans.

### Query Timing — With vs Without mmap

```bash
# SELECT all users
time sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM users;" > /dev/null
# 0.019s

time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;" > /dev/null
# 0.009s

# JOIN users and orders
time sqlite3 sample.db "PRAGMA mmap_size=0; SELECT u.name, o.product, o.amount FROM users u JOIN orders o ON u.id = o.user_id;" > /dev/null
# 0.014s

time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT u.name, o.product, o.amount FROM users u JOIN orders o ON u.id = o.user_id;" > /dev/null
# 0.012s

# GROUP BY aggregation
time sqlite3 sample.db "PRAGMA mmap_size=0; SELECT city, COUNT(*), AVG(age) FROM users GROUP BY city;"
# 0.016s

time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT city, COUNT(*), AVG(age) FROM users GROUP BY city;"
# 0.006s
```

| Query                 | Without mmap | With mmap | Speedup |
|-----------------------|-------------|-----------|---------|
| `SELECT * FROM users` | 19 ms       | 9 ms      | ~2.1×   |
| `JOIN users ↔ orders` | 14 ms       | 12 ms     | ~1.2×   |
| `GROUP BY city`       | 16 ms       | 6 ms      | ~2.7×   |

**Observation:** mmap gave a consistent 1.2×–2.7× speedup. The biggest gain was on the aggregation query, which does a full table scan — exactly where eliminating repeated `read()` calls helps most.

### Process Check

```bash
ps aux | grep sqlite
```

**Observation:** No separate server process exists. SQLite runs as an in-process library inside the application. The CLI process exits as soon as the query finishes.

---

## 2. PostgreSQL Setup & Experiments

### Setup

Installed via Homebrew.

```bash
psql --version
# psql (PostgreSQL) 14.20 (Homebrew)

pg_isready
# /tmp:5432 - accepting connections
```

### Sample Database

Same schema and data (10,000 users + 20,000 orders) created using `generate_series()`.

```sql
CREATE DATABASE adv_dbms_lab;

CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    email VARCHAR(200) NOT NULL,
    age INTEGER,
    city VARCHAR(100),
    created_at TIMESTAMP DEFAULT NOW()
);

CREATE TABLE orders (
    id SERIAL PRIMARY KEY,
    user_id INTEGER REFERENCES users(id),
    product VARCHAR(100),
    amount NUMERIC(10,2),
    order_date TIMESTAMP DEFAULT NOW()
);
```

### Page Size & Page Count

```sql
SHOW block_size;
-- 8192

SELECT relname, relpages, reltuples::INTEGER
FROM pg_class WHERE relname IN ('users', 'orders');
```

| Table  | Pages | Approx Rows | Size    |
|--------|-------|-------------|---------|
| users  | 106   | 10,000      | 1120 kB |
| orders | 148   | 20,000      | 1672 kB |

Total DB size: ~11 MB (includes indexes, system catalogs, WAL infrastructure).

### Cache Configuration

```sql
SHOW shared_buffers;       -- 128MB
SHOW effective_cache_size; -- 4GB
```

- `shared_buffers` — PostgreSQL's own buffer pool
- `effective_cache_size` — hint to query planner about total available cache (OS + DB)

### Query Timing

```sql
\timing on

SELECT * FROM users;
-- Time: 5.220 ms

SELECT u.name, o.product, o.amount
FROM users u JOIN orders o ON u.id = o.user_id;
-- Time: 13.301 ms

SELECT city, COUNT(*), AVG(age) FROM users GROUP BY city;
-- Time: 5.804 ms
```

### Process Check

```bash
ps aux | grep postgres | head -5
```

**Observation:** PostgreSQL runs as a multi-process server with dedicated background workers:

| Process                      | Role                                    |
|------------------------------|-----------------------------------------|
| background writer            | Flushes dirty pages to disk             |
| walwriter                    | Writes Write-Ahead Log entries          |
| autovacuum launcher          | Reclaims dead tuples (MVCC cleanup)     |
| stats collector              | Gathers query statistics                |
| logical replication launcher | Manages replication slots               |

---

## 3. Comparison Analysis

### Page Size

| Property    | SQLite3 | PostgreSQL |
|-------------|---------|------------|
| Page size   | 4 KB    | 8 KB       |
| Configurable? | Yes (at DB creation via `PRAGMA page_size`) | Yes (at compile time) |

PostgreSQL's larger pages mean fewer I/O operations for sequential scans, but more potential waste per row. SQLite's 4 KB default is better suited for smaller, embedded datasets.

### Page Count

| Table   | Rows   | SQLite3       | PostgreSQL |
|---------|--------|---------------|------------|
| users   | 10,000 | 383 (full DB) | 106        |
| orders  | 20,000 | (shared)      | 148        |

SQLite reports `page_count` for the entire database file. PostgreSQL tracks pages per table, but the total DB on disk is ~11 MB vs SQLite's 1.5 MB — the difference comes from MVCC overhead, system catalogs, indexes, and WAL files.

### Query Performance

| Query                 | SQLite (no mmap) | SQLite (mmap) | PostgreSQL |
|-----------------------|-----------------|---------------|------------|
| `SELECT * FROM users` | 19 ms           | 9 ms          | 5.2 ms     |
| `JOIN users ↔ orders` | 14 ms           | 12 ms         | 13.3 ms    |
| `GROUP BY city`       | 16 ms           | 6 ms          | 5.8 ms     |

- On **simple SELECT and aggregation**, PostgreSQL is fastest due to its optimized `shared_buffers` pool.
- On the **JOIN query**, SQLite+mmap (12 ms) nearly matches PostgreSQL (13.3 ms). At this dataset size, SQLite's lack of client-server overhead compensates.
- SQLite without mmap is consistently slowest due to per-page `read()` system call overhead.

### mmap Impact

| Aspect            | Without mmap          | With mmap                     |
|-------------------|-----------------------|-------------------------------|
| I/O method        | `read()` system calls | Memory-mapped virtual memory  |
| System call overhead | Higher             | Lower (OS handles paging)     |
| Wall clock time   | 14–19 ms              | 6–12 ms                       |
| Avg speedup       | Baseline              | ~2× faster                    |

mmap benefit is most visible for full-table scans and aggregations. PostgreSQL achieves a similar effect internally via `shared_buffers` without requiring any mmap configuration.

### Architecture

| Feature         | SQLite3                        | PostgreSQL                          |
|-----------------|-------------------------------|--------------------------------------|
| Type            | Embedded library               | Client-server                        |
| Server process  | None                           | Multiple background workers          |
| Storage         | Single file                    | Directory with multiple files        |
| Concurrency     | File-level locking             | MVCC with row-level locking          |
| DB size (same data) | 1.5 MB                    | 11 MB                                |
| Memory model    | Optional mmap + OS page cache  | `shared_buffers` + OS cache          |
| Best for        | Embedded, mobile, single-user  | Multi-user, concurrent, production   |

---

## 4. Key Takeaways

1. **SQLite is fast for single-user workloads** — with mmap enabled, it matched PostgreSQL on the JOIN and came close on aggregation.
2. **mmap is free performance** for SQLite on read-heavy workloads (~2× average speedup). Worth enabling.
3. **PostgreSQL's size overhead is justified** — MVCC, concurrent access, replication, and crash recovery come at the cost of ~7× more disk space for the same data.
4. **Page size is a trade-off** — larger pages reduce I/O count but can waste space for sparse or small rows.
5. **Architecture determines use case** — SQLite for embedded/desktop apps, PostgreSQL for any multi-user or production scenario.

---

## 5. Commands Reference

### SQLite3

```bash
sqlite3 --version
ls -lh sample.db

sqlite3 sample.db "PRAGMA page_size;"
sqlite3 sample.db "PRAGMA page_count;"
sqlite3 sample.db "PRAGMA mmap_size;"
sqlite3 sample.db "PRAGMA journal_mode;"
sqlite3 sample.db "PRAGMA cache_size;"
sqlite3 sample.db "PRAGMA freelist_count;"

time sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM users;" > /dev/null
time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;" > /dev/null

ps aux | grep sqlite
```

### PostgreSQL

```bash
psql --version
pg_isready
psql adv_dbms_lab
```

```sql
\timing on

SHOW block_size;
SELECT relname, relpages FROM pg_class WHERE relname IN ('users','orders');
SELECT pg_size_pretty(pg_database_size('adv_dbms_lab'));

SHOW shared_buffers;
SHOW effective_cache_size;

SELECT * FROM users;
SELECT u.name, o.product, o.amount FROM users u JOIN orders o ON u.id = o.user_id;
SELECT city, COUNT(*), AVG(age) FROM users GROUP BY city;
```

```bash
ps aux | grep postgres
```