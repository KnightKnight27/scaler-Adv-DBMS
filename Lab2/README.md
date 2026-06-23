# Advanced DBMS Lab — SQLite3 vs PostgreSQL Comparison

## Environment

- **OS:** macOS (Apple Silicon — aarch64-apple-darwin)
- **SQLite3 Version:** 3.41.2 (2023-03-22)
- **PostgreSQL Version:** 15.8 (Homebrew)
- **Dataset:** `users` table with **10,000 rows** (id, name, email, age, city, created_at)

---

## 1. SQLite3 Exploration

### 1.1 Database Setup

```sql
-- Create and populate the sample database
sqlite3 sample.db

CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT NOT NULL,
    age INTEGER,
    city TEXT,
    created_at TEXT DEFAULT (datetime('now'))
);

-- Insert 10,000 rows using recursive CTE
WITH RECURSIVE cnt(x) AS (
    SELECT 1
    UNION ALL
    SELECT x+1 FROM cnt WHERE x < 10000
)
INSERT INTO users (name, email, age, city)
SELECT
    'User_' || x,
    'user' || x || '@example.com',
    18 + (x % 50),
    CASE (x % 5)
        WHEN 0 THEN 'Mumbai'
        WHEN 1 THEN 'Delhi'
        WHEN 2 THEN 'Bangalore'
        WHEN 3 THEN 'Chennai'
        WHEN 4 THEN 'Kolkata'
    END
FROM cnt;
```

### 1.2 File Size Observation

```bash
$ ls -lh sample.db
-rw-r--r--  1 mehulagarwal  staff  680K  May 9 18:46  sample.db
```

**Observation:** The entire database, including 10,000 rows, fits in a **single 680 KB file**. SQLite stores everything (schema, data, indexes) in one portable file.

### 1.3 PRAGMA — Page Size & Page Count

```sql
sqlite> PRAGMA page_size;
4096

sqlite> PRAGMA page_count;
170
```

| Metric       | Value  |
|-------------|--------|
| Page Size   | 4,096 bytes (4 KB) |
| Page Count  | 170 |
| Total Size  | 170 × 4096 = 696,320 bytes ≈ 680 KB |

**Observation:** SQLite uses a default page size of **4 KB**. The 10,000-row table occupies **170 pages**. The calculated size (170 × 4 KB = 680 KB) matches the file size observed via `ls -lh`.

### 1.4 mmap_size Experiment

`mmap_size` controls how much of the database file is memory-mapped. By default, it is **0** (disabled).

```sql
-- Check default mmap_size
sqlite> PRAGMA mmap_size;
0

-- Enable mmap (set to 1 GB)
sqlite> PRAGMA mmap_size = 1073741824;
1073741824
```

When mmap is enabled, SQLite maps the database file directly into the process's virtual address space using the OS `mmap()` system call. This avoids `read()`/`write()` syscalls and lets the OS manage caching via the page cache.

### 1.5 Query Timing — With vs Without mmap

```sql
-- Timer enabled
.timer on
```

#### Full Table Scan: `SELECT * FROM users;`

| Run | Without mmap (real) | With mmap (real) |
|-----|-------------------|-----------------|
| 1   | 0.007s            | 0.008s          |
| 2   | 0.007s            | 0.006s          |
| 3   | 0.006s            | 0.007s          |
| **Avg** | **~0.007s**   | **~0.007s**     |

#### Aggregate Queries

| Query | Without mmap | With mmap |
|-------|-------------|-----------|
| `SELECT COUNT(*) FROM users WHERE city = 'Mumbai';` → 2000 | 0.001s | 0.000s |
| `SELECT AVG(age) FROM users;` → 42.5 | 0.000s | 0.000s |

**Observation:** For a **680 KB database**, the difference between mmap and traditional I/O is negligible. The dataset fits entirely in the OS page cache after the first read. mmap benefits become significant with **larger databases** (hundreds of MB+) where avoiding syscall overhead matters.

### 1.6 Process Monitoring

```bash
$ ps aux | grep sqlite
```

**Observation:** SQLite is an **embedded, in-process** database engine. It runs as a library within the calling process, not as a separate server process. There is **no persistent background daemon** — the `sqlite3` CLI process appears only while the shell session is active.

---

## 2. PostgreSQL (PSQL) Setup

### 2.1 Installation & Startup

```bash
# Install via Homebrew
brew install postgresql@15

# Start the server
brew services start postgresql@15

# Verify it's running
pg_isready
# Output: /tmp:5432 - accepting connections
```

### 2.2 Database Setup

```sql
-- Create database
CREATE DATABASE adbms_lab;
\c adbms_lab

-- Create table with same schema
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    email TEXT NOT NULL,
    age INTEGER,
    city TEXT,
    created_at TIMESTAMP DEFAULT NOW()
);

-- Insert 10,000 rows
INSERT INTO users (name, email, age, city)
SELECT
    'User_' || g,
    'user' || g || '@example.com',
    18 + (g % 50),
    CASE (g % 5)
        WHEN 0 THEN 'Mumbai'
        WHEN 1 THEN 'Delhi'
        WHEN 2 THEN 'Bangalore'
        WHEN 3 THEN 'Chennai'
        WHEN 4 THEN 'Kolkata'
    END
FROM generate_series(1, 10000) AS g;
```

### 2.3 Page Size & Page Count

```sql
adbms_lab=# SHOW block_size;
 block_size
------------
 8192

adbms_lab=# ANALYZE users;
adbms_lab=# SELECT relpages AS page_count FROM pg_class WHERE relname = 'users';
 page_count
------------
        106

adbms_lab=# SELECT pg_size_pretty(pg_relation_size('users')) AS table_size;
 table_size
------------
 848 kB

adbms_lab=# SELECT pg_size_pretty(pg_total_relation_size('users')) AS total_size_with_indexes;
 total_size_with_indexes
-------------------------
 1120 kB
```

| Metric           | Value    |
|------------------|----------|
| Page Size (block_size) | 8,192 bytes (8 KB) |
| Page Count       | 106      |
| Table Size       | 848 KB   |
| Total Size (with indexes) | 1,120 KB |

**Observation:** PostgreSQL uses a default page size of **8 KB** — double that of SQLite's 4 KB. Despite this, the table uses only **106 pages** (vs SQLite's 170) because larger pages pack more rows per page. The total size is higher (1120 KB vs 680 KB) because PostgreSQL stores additional metadata, MVCC versioning info, tuple headers, and the primary key index separately.

### 2.4 Buffer Pool & Cache Configuration

PostgreSQL uses **shared buffers** (a dedicated in-memory buffer pool) instead of relying on mmap like SQLite.

```sql
adbms_lab=# SHOW shared_buffers;
 shared_buffers
----------------
 128MB

adbms_lab=# SHOW effective_cache_size;
 effective_cache_size
----------------------
 4GB
```

| Parameter | Value | Purpose |
|-----------|-------|---------|
| `shared_buffers` | 128 MB | Dedicated RAM cache managed by PostgreSQL |
| `effective_cache_size` | 4 GB | Planner hint for total available cache (shared_buffers + OS cache) |

**Observation:** PostgreSQL manages its own **buffer pool** (`shared_buffers`) to cache frequently accessed pages in memory. This is fundamentally different from SQLite's optional mmap approach — PostgreSQL has full control over eviction policies, dirty page tracking, and write-ahead logging (WAL).

### 2.5 Query Timing

```sql
\timing on
```

#### Full Table Scan: `SELECT * FROM users;`

```
Time: 9.815 ms
```

#### Aggregate Queries

| Query | Execution Time |
|-------|---------------|
| `SELECT COUNT(*) FROM users WHERE city = 'Mumbai';` → 2000 | 2.024 ms |
| `SELECT AVG(age) FROM users;` → 42.5 | 1.627 ms |
| `SELECT * FROM users WHERE id = 5000;` (PK lookup) | 0.172 ms |

#### EXPLAIN ANALYZE — Full Table Scan

```
adbms_lab=# EXPLAIN ANALYZE SELECT * FROM users;
                                              QUERY PLAN
----------------------------------------------------------------------------------------------------------
 Seq Scan on users  (cost=0.00..206.00 rows=10000 width=52) (actual time=0.002..0.304 rows=10000 loops=1)
 Planning Time: 0.014 ms
 Execution Time: 0.499 ms
```

**Observation:** The raw execution time (0.499 ms) is fast, but the total client-visible time (9.815 ms) includes network/IPC overhead and result formatting. The sequential scan reads all **106 pages**. Point queries using the primary key index are extremely fast (0.172 ms).

### 2.6 Process Monitoring

```bash
$ ps aux | grep postgres
mehulagarwal  6067  /opt/homebrew/opt/postgresql@15/bin/postgres -D /opt/homebrew/var/postgresql@15
mehulagarwal  6076  postgres: checkpointer
mehulagarwal  6077  postgres: background writer
mehulagarwal  6079  postgres: walwriter
mehulagarwal  6080  postgres: autovacuum launcher
mehulagarwal  6081  postgres: logical replication launcher
```

**Observation:** PostgreSQL runs as a **multi-process server** with dedicated background worker processes:
- **checkpointer** — flushes dirty pages from shared buffers to disk
- **background writer** — proactively writes dirty buffers to reduce checkpoint spikes
- **walwriter** — flushes WAL (Write-Ahead Log) records for durability
- **autovacuum launcher** — reclaims dead tuples from MVCC
- **logical replication launcher** — manages replication slots

---

## 3. Comparison Report

### 3.1 Page Size & Page Count

| Metric | SQLite3 | PostgreSQL |
|--------|---------|------------|
| Page Size | 4,096 bytes (4 KB) | 8,192 bytes (8 KB) |
| Page Count | 170 | 106 |
| Data File Size | 680 KB | 848 KB |
| Total Size (with indexes) | 680 KB (single file) | 1,120 KB |

**Analysis:**
- PostgreSQL's **larger page size (8 KB)** reduces the number of pages needed, which means fewer I/O operations for full scans.
- SQLite's **single-file architecture** is more compact because it has no MVCC overhead, no separate index files, and simpler tuple headers.
- PostgreSQL stores additional per-row metadata (xmin, xmax, ctid, etc.) for transaction visibility, which increases storage overhead.

### 3.2 Query Performance

| Query Type | SQLite3 | PostgreSQL |
|-----------|---------|------------|
| Full table scan (`SELECT *`) | ~7 ms | ~10 ms (client) / ~0.5 ms (execution) |
| Count with filter | ~1 ms | ~2 ms |
| Average aggregation | < 1 ms | ~1.6 ms |
| Point query (PK lookup) | < 1 ms | ~0.2 ms |

**Analysis:**
- For **small datasets (10K rows)**, SQLite is faster because it has **zero IPC overhead** — queries run in-process without client/server communication.
- PostgreSQL's reported time includes **client-server round-trip**, result serialization, and formatting overhead. The actual execution time (from `EXPLAIN ANALYZE`) is **sub-millisecond** (0.499 ms).
- For **concurrent workloads** and **write-heavy** scenarios, PostgreSQL significantly outperforms SQLite due to its MVCC, WAL, and connection pooling capabilities.

### 3.3 Memory-Mapped I/O (mmap) Impact

| Aspect | SQLite3 | PostgreSQL |
|--------|---------|------------|
| mmap support | `PRAGMA mmap_size` | Not used directly |
| Default state | Disabled (0) | N/A |
| Cache mechanism | OS page cache + optional mmap | Dedicated shared_buffers (128 MB) |
| Cache control | Minimal (OS-managed) | Full control (LRU, clock-sweep) |

**Analysis:**
- **SQLite + mmap:** When enabled, mmap allows SQLite to bypass `read()` syscalls by directly accessing file pages through virtual memory. For our 680 KB database, the impact was **negligible** (~0 ms difference) because the entire file fits in the OS page cache regardless. For databases in the **hundreds of MB range**, mmap can reduce latency by **10-30%** by eliminating syscall overhead.
- **PostgreSQL:** Does not rely on mmap for data access. Instead, it manages its own **128 MB buffer pool** (`shared_buffers`) with sophisticated eviction policies. This gives PostgreSQL predictable performance characteristics and prevents the OS from interfering with caching decisions.
- The key tradeoff: **mmap is simpler but gives less control**, while PostgreSQL's buffer manager is more complex but allows fine-tuned memory management.

### 3.4 Architecture Comparison

| Feature | SQLite3 | PostgreSQL |
|---------|---------|------------|
| Architecture | Embedded (in-process library) | Client-Server (multi-process) |
| Concurrency | Single-writer, multiple-reader | Full MVCC, multiple concurrent writers |
| Storage | Single file | Directory with multiple files |
| Processes | None (runs in app process) | ~6 background processes |
| Transactions | Journaling (rollback or WAL mode) | WAL + MVCC |
| Best for | Mobile apps, embedded systems, small-medium apps | Enterprise apps, high concurrency, large datasets |

---

## 4. Commands Reference

```bash
# SQLite3
sqlite3 sample.db
ls -lh sample.db
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
PRAGMA mmap_size = 1073741824;
.timer on
SELECT * FROM users;
ps aux | grep sqlite

# PostgreSQL
brew services start postgresql@15
pg_isready
psql adbms_lab
SHOW block_size;
SELECT relpages FROM pg_class WHERE relname = 'users';
SELECT pg_size_pretty(pg_relation_size('users'));
SHOW shared_buffers;
SHOW effective_cache_size;
\timing on
SELECT * FROM users;
EXPLAIN ANALYZE SELECT * FROM users;
ps aux | grep postgres
```

---

## 5. Key Takeaways

1. **SQLite is faster for simple, single-user workloads** because it eliminates client-server overhead entirely.
2. **PostgreSQL scales better** for concurrent access, complex queries, and large datasets due to its multi-process architecture and sophisticated buffer management.
3. **mmap has minimal impact on small databases** — the OS page cache handles caching effectively for files that fit in memory. Its benefits appear with larger datasets.
4. **PostgreSQL uses 2× the page size** (8 KB vs 4 KB), resulting in fewer but larger I/O operations — a deliberate design choice optimized for server workloads.
5. **Storage overhead** is higher in PostgreSQL due to MVCC metadata, separate index storage, and transaction management structures.
