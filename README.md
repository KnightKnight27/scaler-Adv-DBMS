# Lab Task: SQLite3 and PostgreSQL Exploration & Comparison

**Name:** Tanish  
**Role Number:** 24bcs10008

---

## 1. SQLite3 Exploration

### Setup
A sample database `sample.db` was created with a `users` table containing **100,000 rows** (id, name, email, age, bio).

### Observations
- **File Size:** `9.1M` (observed via `ls -lh sample.db`)
- **Page Size:** `4096` bytes (observed via `PRAGMA page_size;`)
- **Page Count:** `2334` (observed via `PRAGMA page_count;`)

### Experiments with mmap
- **Default/Disabled mmap_size:** 0
- **Modified mmap_size:** 268435456 (256MB)

#### Query Performance Comparison
*Query: `SELECT COUNT(*) FROM users; SELECT * FROM users LIMIT 100000;`*

| Mode | Real Time | User Time | Sys Time |
| :--- | :--- | :--- | :--- |
| **Without mmap** | 0m0.062s | 0m0.038s | 0m0.007s |
| **With mmap (256MB)** | 0m0.044s | 0m0.034s | 0m0.006s |

**Observation:** Querying with memory-mapped I/O (mmap) showed a performance improvement of approximately **29%** in real time for this workload.

### Process Monitoring
Command used: `ps aux | grep sqlite`
*(Note: Execution failed in the restricted environment with "Operation not permitted", but typical output would show the sqlite3 process and its memory usage.)*

---

## 2. PostgreSQL (PSQL) Exploration

### Setup Commands
To setup a similar experiment in PostgreSQL:
```sql
CREATE DATABASE lab_db;
\c lab_db

CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    email TEXT,
    age INTEGER,
    bio TEXT
);

-- Populating 100k rows (example using generate_series)
INSERT INTO users (name, email, age, bio)
SELECT 
    'user_' || i, 
    'user_' || i || '@example.com', 
    (random() * 60 + 18)::int, 
    md5(random()::text)
FROM generate_series(1, 100000) s(i);
```

### Observations (Standard Findings)
- **Page Size:** `8192` bytes (8KB) — Checked via `SHOW block_size;`
- **Page Count:** Can be retrieved using:
  ```sql
  SELECT relpages FROM pg_class WHERE relname = 'users';
  ```
- **Query Execution Time:** Measured using `EXPLAIN ANALYZE`:
  ```sql
  EXPLAIN ANALYZE SELECT * FROM users;
  ```

---

## 3. Comparison Analysis

| Metric | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| **Default Page Size** | 4096 bytes (4KB) | 8192 bytes (8KB) |
| **Storage Model** | Single-file database | Server-based with multiple files/directories |
| **mmap Support** | Integrated via `mmap_size` | Managed by OS Buffer Cache (mmap used internally in some cases) |
| **Concurrency** | File-level locking (limited) | Multi-version Concurrency Control (MVCC) |
| **Use Case** | Local storage, embedded, mobile | Enterprise applications, high concurrency |

### Analysis of mmap impact
In SQLite3, `mmap` allows the database to map parts of the file directly into the process's address space. This avoids the overhead of copying data from kernel space to user space during `read()` calls. For read-heavy operations, this leads to significant performance gains, as seen in the ~29% speedup in our experiment. PostgreSQL relies more heavily on its own Shared Buffer Pool and the OS page cache, effectively achieving similar performance benefits but at a more complex architectural level.

---

## Commands Used

### SQLite3
```bash
# Create DB
python3 setup_sqlite.py

# Experiments
ls -lh sample.db
sqlite3 sample.db "PRAGMA page_size; PRAGMA page_count;"
sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM users;" > /dev/null
sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;" > /dev/null
```

### PostgreSQL
```bash
psql -c "SHOW block_size;"
psql -d lab_db -c "SELECT relpages FROM pg_class WHERE relname = 'users';"
psql -d lab_db -c "EXPLAIN ANALYZE SELECT * FROM users;"
```
