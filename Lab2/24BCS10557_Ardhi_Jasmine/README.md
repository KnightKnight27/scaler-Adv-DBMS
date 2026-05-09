# SQLite3 vs PostgreSQL — Comparison Report

---

## Part 1 — SQLite3 Exploration

### Commands Used

```bash
# Install & verify
sudo apt install sqlite3
sqlite3 --version
# SQLite version 3.45.1 2024-01-30 16:01:20

# Create database and table
sqlite3 sample.db

CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER);
INSERT INTO users (name, age) VALUES ('Alice', 22), ('Bob', 25), ('Charlie', 30);

-- Bulk insert 100,000 rows
WITH RECURSIVE cnt(x) AS (
    SELECT 1
    UNION ALL
    SELECT x+1 FROM cnt WHERE x < 100000
)
INSERT INTO users(name, age) SELECT 'User' || x, 20 + (x % 30) FROM cnt;
```

### Observations

**File Size:**
```bash
ls -lh
# -rw-r--r-- 1 jasmine jasmine 2.0M May 9 17:10 sample.db
```
SQLite stores the entire database in a **single file** — 100,000+ rows fit in just 2.0 MB.

**Page Size & Page Count:**
```sql
PRAGMA page_size;   -- 4096
PRAGMA page_count;  -- 488
```
Calculated size: `4096 × 488 ≈ 2.0 MB` — matches the actual file size exactly.

**mmap Experiment:**
```sql
PRAGMA mmap_size;               -- 0 (disabled by default)
PRAGMA mmap_size = 268435456;   -- enable 256 MB mmap
PRAGMA mmap_size;               -- 268435456 (confirmed)
```

**Query Timing:**
```bash
# Without mmap
time sqlite3 sample.db "SELECT * FROM users;"
# real 0m0.391s | user 0m0.096s | sys 0m0.284s

# With mmap
time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;"
# real 0m0.389s | user 0m0.097s | sys 0m0.280s
```

mmap reduced query time by ~2ms. The gain is small here because the 2 MB database fits easily in the OS page cache. mmap benefits grow with larger databases and repeated reads.

**Process check:**
```bash
ps aux | grep sqlite
# sqlite3 runs as a single in-process library — no server daemon
```

---

## Part 2 — PostgreSQL Exploration

### Commands Used

```bash
# Install & start
sudo apt install postgresql postgresql-contrib
sudo systemctl status postgresql

# Enter psql and set up database
sudo -u postgres psql

CREATE DATABASE sampledb;
\c sampledb

CREATE TABLE users (id SERIAL PRIMARY KEY, name TEXT, age INT);

INSERT INTO users(name, age)
SELECT 'User' || generate_series, 20 + (generate_series % 30)
FROM generate_series(1, 100000);
```

### Observations

**Page Size:**
```sql
SHOW block_size;
-- 8192
```
PostgreSQL uses 8 KB blocks — double SQLite's 4 KB pages.

**Page Count:**
```sql
SELECT relpages FROM pg_class WHERE relname = 'users';
```
`relpages` gives the number of 8 KB pages used by the table. Estimated size = `relpages × 8192 bytes`.

**Query Timing:**
```bash
\q
time psql -d sampledb -c "SELECT * FROM users;"
```
Timing includes client-server socket overhead (authentication, query planning, result transmission) unlike SQLite's in-process execution.

**Process check:**
```bash
ps aux | grep postgres
# Multiple processes: postmaster, checkpointer, autovacuum, WAL writer, per-connection workers
```

---

## Comparison

| Feature              | SQLite3                          | PostgreSQL                        |
|----------------------|----------------------------------|-----------------------------------|
| **Architecture**     | Serverless, file-based           | Client-server, multi-process      |
| **Default Page Size**| 4096 bytes (4 KB)                | 8192 bytes (8 KB)                 |
| **Page Count (100K rows)** | 488 pages              | Varies (stored in tablespace)     |
| **Storage**          | Single `.db` file (2.0 MB)       | Directory of files                |
| **mmap Support**     | Manual via `PRAGMA mmap_size`    | Managed internally by OS          |
| **mmap Impact**      | ~2ms gain (small DB, warm cache) | Transparent via `shared_buffers`  |
| **Query Performance**| Fast for small, single-user load | Better for large, concurrent load |
| **Concurrency**      | Limited (write locks entire DB)  | High (MVCC, row-level locking)    |
| **Setup**            | Zero config                      | Requires service + user setup     |

---

## Conclusion

- **SQLite** is lightweight and zero-config — ideal for local apps, mobile, and prototyping. The entire database lives in one file.
- **PostgreSQL** is built for production — concurrent users, large datasets, and high availability.
- **mmap** on SQLite marginally reduces sys-call overhead but the impact is small for databases that fit in memory.
- For any multi-user or large-scale application, PostgreSQL is the better choice.