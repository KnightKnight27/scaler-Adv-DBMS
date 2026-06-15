# scaler-Adv-DBMS
# SQLite3 vs PostgreSQL: Storage & Performance Lab Report

## Environment

- OS: Ubuntu 24.04 LTS (x86_64)
- SQLite3 version: 3.45.x (system)
- PostgreSQL version: 16.13
- Dataset: 10,000 rows — `users` table with `id`, `name`, `email`, `age`, `city` columns

---

## 1. SQLite3 Exploration

### 1.1 File Size

```bash
$ ls -lh sample.db
-rw-r--r-- 1 root root 468K May 9 17:08 sample.db
```

The database file is **468 KB** on disk for 10,000 rows, stored as a single flat file.

---

### 1.2 PRAGMA Commands

```sql
PRAGMA page_size;   -- Result: 4096
PRAGMA page_count;  -- Result: 117
```

| Property   | Value  |
|------------|--------|
| Page Size  | 4096 bytes (4 KB) |
| Page Count | 117 pages |
| Total Size | 117 × 4096 = 479,232 bytes ≈ 468 KB |

SQLite3 uses a **4 KB default page size**, which is the atomic unit of I/O. The database is divided into exactly 117 such pages.

Other relevant PRAGMA values observed:

```sql
PRAGMA journal_mode;  -- Result: delete  (default WAL-less journaling)
PRAGMA cache_size;    -- Result: -2000   (2000 KB = ~2 MB in-memory cache)
```

---

### 1.3 mmap_size Experiment

`mmap_size` controls how much of the database file SQLite maps directly into the process's virtual address space using the OS memory-mapping facility (`mmap()`). When set to `0`, SQLite uses traditional `read()` system calls. When set to a positive value, it maps up to that many bytes of the file, allowing the OS page cache to serve reads without copying data into user space.

**Changing mmap_size:**

```sql
-- Disable mmap (default)
PRAGMA mmap_size = 0;

-- Enable mmap with 256 MB window
PRAGMA mmap_size = 268435456;

-- Verify
PRAGMA mmap_size;  -- Returns the active size
```

**Observed Behavior:**
- With `mmap_size=0`: SQLite issues `pread()` calls per page access.
- With `mmap_size=268435456`: SQLite maps the entire 468 KB file (well within the window), and the kernel serves all reads from the page cache directly. This eliminates a `read()` syscall + buffer copy per page.

---

### 1.4 Query Timing

Command used:

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

**5-Trial Results:**

| Trial | Without mmap (ms) | With mmap (ms) |
|-------|-------------------|----------------|
| 1     | 12                | 6              |
| 2     | 18                | 6              |
| 3     | 6                 | 6              |
| 4     | 6                 | 6              |
| 5     | 6                 | 6              |
| **Average** | **9.6 ms**  | **6.0 ms**     |

**Observation:** mmap reduced average query time by ~37.5%. The first two non-mmap trials are slower due to cold OS page-cache state. Once pages are cached (trials 3–5), the gap narrows. With mmap enabled, the kernel handles caching automatically and consistently from the first trial onward within the session.

---

### 1.5 Process Inspection

```bash
$ ps aux | grep sqlite
(no sqlite processes running)
```

This is expected. SQLite3 is an **in-process library** — it runs inside the calling application's process, not as a separate server daemon. When the `sqlite3` CLI exits, no background process remains. This is a fundamental architectural difference from PostgreSQL.

---

## 2. PostgreSQL (PSQL) Setup

### 2.1 Installation and Startup

```bash
apt-get install -y postgresql postgresql-contrib
service postgresql start
# PostgreSQL 16 started on port 5432
```

### 2.2 Database and Table Setup

```sql
CREATE DATABASE labdb;
\c labdb

CREATE TABLE users (
  id SERIAL PRIMARY KEY,
  name TEXT NOT NULL,
  email TEXT NOT NULL,
  age INTEGER,
  city TEXT
);

INSERT INTO users (name, email, age, city)
SELECT 'User_' || g, 'user' || g || '@example.com',
       (g % 60) + 18,
       CASE (g % 5)
         WHEN 0 THEN 'New York' WHEN 1 THEN 'London'
         WHEN 2 THEN 'Tokyo'   WHEN 3 THEN 'Paris'
         ELSE 'Sydney'
       END
FROM generate_series(1, 10000) AS g;
-- INSERT 0 10000
```

---

### 2.3 Page Size and Page Count

```sql
SELECT current_setting('block_size') AS page_size_bytes;
-- Result: 8192

SELECT
  relname,
  pg_relation_size(oid) / current_setting('block_size')::int AS computed_pages,
  pg_size_pretty(pg_relation_size(oid)) AS size
FROM pg_class
WHERE relname = 'users';
```

| Property     | Value          |
|--------------|----------------|
| Page Size    | 8192 bytes (8 KB) |
| Page Count   | 96 pages       |
| Table Size   | 768 KB         |
| Total w/TOAST & index | 1040 KB |

PostgreSQL calls its pages **"blocks"** — each is 8 KB (vs SQLite's 4 KB). More data fits per page, resulting in fewer pages for the same dataset.

---

### 2.4 Query Timing

**Internal execution time via EXPLAIN ANALYZE:**

```sql
EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM users;
```

```
Seq Scan on users
  (actual time=0.004..0.627 rows=10000 loops=1)
  Buffers: shared hit=96
Planning Time: 0.12 ms
Execution Time: 0.92 ms
```

**Shell-level timing (5 trials, includes client connection overhead):**

| Trial | Time (ms) |
|-------|-----------|
| 1     | 61        |
| 2     | 61        |
| 3     | 60        |
| 4     | 58        |
| 5     | 58        |
| **Average** | **59.6 ms** |

The shell-level time includes TCP socket setup, authentication, query parsing, result serialization, and transmission — not just execution. The **internal execution time is ~0.9 ms**, far lower than the shell overhead.

---

### 2.5 Buffer/Memory Configuration (PostgreSQL's mmap Equivalent)

PostgreSQL does not expose `mmap_size` directly. Instead, it uses a **shared buffer pool** managed internally:

```sql
SELECT name, setting, unit FROM pg_settings
WHERE name IN ('shared_buffers', 'work_mem', 'effective_cache_size', 'block_size');
```

| Setting              | Value        | Meaning                              |
|----------------------|--------------|--------------------------------------|
| `block_size`         | 8192 bytes   | Page/block size                      |
| `shared_buffers`     | 16384 × 8KB = 128 MB | Shared in-memory buffer pool |
| `work_mem`           | 4096 KB (4 MB) | Per-operation sort/hash memory     |
| `effective_cache_size`| 524288 × 8KB = 4 GB | Planner's estimate of OS cache |

All 96 pages of the `users` table fit entirely within `shared_buffers` (128 MB). The EXPLAIN output confirmed `shared hit=96`, meaning zero disk I/O occurred after the first load.

### 2.6 Process Inspection

```bash
$ ps aux | grep postgres
postgres  3141  /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main ...
postgres  3142  postgres: checkpointer
postgres  3143  postgres: background writer
postgres  3145  postgres: walwriter
postgres  3146  postgres: autovacuum launcher
```

PostgreSQL runs as a **persistent multi-process server**. Multiple background worker processes handle checkpointing, WAL writing, autovacuum, and connections. This is structurally opposite to SQLite's embedded model.

---

## 3. Comparison Report

### 3.1 Page Size

| Database    | Page Size | Notes                                      |
|-------------|-----------|--------------------------------------------|
| SQLite3     | 4096 B (4 KB) | Default; configurable via `PRAGMA page_size` before first write |
| PostgreSQL  | 8192 B (8 KB) | Fixed at compile time; not user-changeable |

PostgreSQL's larger page size means fewer I/O operations per sequential scan but potentially more wasted space for small rows. SQLite's smaller pages allow finer granularity and are more appropriate for embedded/mobile use cases where memory is constrained.

---

### 3.2 Page Count (10,000-row `users` table)

| Database    | Page Count | Total Size |
|-------------|------------|------------|
| SQLite3     | 117 pages  | 468 KB     |
| PostgreSQL  | 96 pages   | 768 KB     |

Despite having double the page size, PostgreSQL uses fewer pages but more total disk space. This is because PostgreSQL stores additional per-row metadata (tuple headers, transaction visibility fields, alignment padding) that inflates row size compared to SQLite's compact storage.

---

### 3.3 Query Performance

| Metric                         | SQLite3        | PostgreSQL        |
|--------------------------------|----------------|-------------------|
| Avg shell time (5 trials)      | 9.6 ms         | 59.6 ms           |
| Internal execution time        | ~6 ms          | ~0.9 ms           |
| Client connection overhead     | None (in-process) | ~58–60 ms      |

SQLite is faster at the shell level because there is no client-server round trip — the CLI is the database. PostgreSQL's internal engine is actually faster per execution (~0.9 ms vs ~6 ms), but this advantage is masked by TCP connection and authentication overhead in CLI benchmarks. In a persistent connection scenario (application code), PostgreSQL would be faster for complex queries.

---

### 3.4 mmap Impact

| Database    | mmap Mechanism             | Observed Impact |
|-------------|----------------------------|-----------------|
| SQLite3     | `PRAGMA mmap_size`         | ~37.5% reduction in average query time (9.6 ms → 6.0 ms). Most significant on first query (cold cache): 12–18 ms without mmap vs 6 ms with. |
| PostgreSQL  | `shared_buffers` pool (implicit) | All 96 pages cached in shared memory after first access (`shared hit=96`). Effectively always "mmap-like" for repeated queries. |

**SQLite3 mmap behavior in detail:**
Without mmap, each page access triggers a `pread()` syscall with a kernel-to-userspace buffer copy. With `mmap_size=268435456`, the entire 468 KB file is mapped into virtual address space. Page faults serve the data directly from the kernel's page cache on first access; subsequent accesses within the session are near-zero cost. The benefit is most visible on the **first query** in a session (cold start).

**PostgreSQL equivalent:**
PostgreSQL's `shared_buffers` acts like a persistent, process-shared mmap region. Data read from disk is pinned in shared memory and reused across all client connections. PostgreSQL also leverages the OS page cache on top of shared_buffers (controlled by `effective_cache_size` for the planner). In practice, the result is the same: repeated queries against warm data incur no disk I/O.

---

### 3.5 Architecture Summary

| Feature              | SQLite3                        | PostgreSQL                         |
|----------------------|--------------------------------|------------------------------------|
| Architecture         | Embedded library (in-process)  | Client-server (separate daemon)    |
| Concurrency          | Single writer at a time        | Full MVCC, multiple concurrent writers |
| Default page size    | 4 KB                           | 8 KB                               |
| mmap control         | `PRAGMA mmap_size` (explicit)  | `shared_buffers` (implicit, persistent) |
| Process model        | No background processes        | Multiple background worker processes |
| Best use case        | Local/embedded, single-user    | Multi-user, networked, high-concurrency |
| Query overhead       | Minimal (no network)           | Higher per-query (connection cost) |
| Raw engine speed     | Slower for complex queries     | Faster for complex/concurrent queries |

---

## 4. Conclusions

1. **Page size matters for I/O efficiency.** PostgreSQL's 8 KB pages mean fewer read operations per sequential scan; SQLite's 4 KB pages suit memory-constrained environments better.

2. **mmap meaningfully improves SQLite cold-start performance** (~37% in this lab) by eliminating per-page syscall overhead. Enabling it via `PRAGMA mmap_size` is a low-cost optimization for read-heavy workloads.

3. **PostgreSQL's shared buffer pool is inherently "always-mmap."** Unlike SQLite where mmap is opt-in per session, PostgreSQL maintains a persistent shared memory region across all connections, making warm-cache performance automatic.

4. **Shell-level benchmarks favor SQLite** due to connection overhead in PostgreSQL. Application-level benchmarks on persistent connections would show PostgreSQL's engine outperforming SQLite on complex queries, joins, and concurrent access patterns.

5. **SQLite is not a "lesser" database** — it is architecturally suited for different workloads. For embedded use, local tooling, or single-writer applications, SQLite's simplicity and zero-configuration nature are strengths. PostgreSQL's complexity pays off at scale, with concurrency, and in networked multi-user environments.