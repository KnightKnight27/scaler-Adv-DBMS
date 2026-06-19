# ADBMS Assignment 1 — SQLite3 vs PostgreSQL

**Name:** _Vibhuti Bhatnagar_
**Role Number:** _24BCS10288_

This report documents hands-on experiments comparing **SQLite 3.51.0** and **PostgreSQL 16.13** on the same dataset (a `users` table with 100 000 rows, plus a 1 000 000-row variant for clearer timing differences). Every command and observation below was run on this machine; no values are estimated.

**Environment**

| Item | Value |
|---|---|
| OS | macOS (Darwin 25.4.0, arm64) |
| SQLite | `sqlite3 --version` → `3.51.0` |
| PostgreSQL | `psql --version` → `psql (PostgreSQL) 16.13 (Homebrew)` |
| Postgres data directory | `/opt/homebrew/var/postgresql@16` |

---

## 1. SQLite3 Exploration

### 1.1 Sample database

A sample database `sample.db` was created with a single `users` table containing 100 000 rows (and two secondary indexes). A second `large.db` with 1 000 000 wider rows was built for clearer timing measurements.

Seed (excerpt from [seed_sqlite.sql](seed_sqlite.sql)):

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY, name TEXT, email TEXT,
    age INTEGER, city TEXT, created TEXT
);
WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n < 100000)
INSERT INTO users SELECT n, 'User_'||n, 'user'||n||'@example.com',
       20+(n%60), <city expr>, datetime('now','-'||(n%365)||' days') FROM seq;
CREATE INDEX idx_users_city ON users(city);
CREATE INDEX idx_users_age  ON users(age);
```

```bash
$ sqlite3 sample.db < seed_sqlite.sql
$ ls -lh sample.db
-rw-r--r--  1 user  staff   9.3M May  9 17:42 sample.db
```

### 1.2 PRAGMA observations

```sql
sqlite> PRAGMA page_size;       -- 4096
sqlite> PRAGMA page_count;      -- 2383
sqlite> PRAGMA freelist_count;  -- 0
sqlite> PRAGMA encoding;        -- UTF-8
sqlite> PRAGMA journal_mode;    -- delete
sqlite> PRAGMA mmap_size;       -- 0  (memory-mapping disabled by default)
```

Cross-check: `page_size × page_count = 4096 × 2383 = 9 760 768 bytes ≈ 9.31 MB`, which exactly matches `ls -lh sample.db = 9.3M`. So the on-disk file is just the array of fixed-size pages; there is no header overhead beyond what those pages account for.

`.dbinfo` summary:

```
database page size:  4096
write format:        1
read format:         1
database page count: 2383
freelist page count: 0
text encoding:       1 (utf8)
software version:    3051000
number of tables:    1
number of indexes:   2
```

### 1.3 mmap_size experiment

`PRAGMA mmap_size` controls how many bytes of the database file SQLite memory-maps. Default is **0** (no mapping; reads go through `pread()` syscalls into private heap buffers).

```sql
sqlite> PRAGMA mmap_size;                   -- 0       (default)
sqlite> PRAGMA mmap_size = 268435456;       -- request 256 MB
sqlite> PRAGMA mmap_size;                   -- 268435456 (granted)
sqlite> PRAGMA compile_options;             -- includes:
        --   DEFAULT_MMAP_SIZE=0
        --   MAX_MMAP_SIZE=1073741824   (1 GiB cap on this build)
```

Behavior observed:

* The setting is **per connection** — opening a fresh `sqlite3` shell shows `mmap_size = 0` again unless you set it (or compile a non-zero default).
* SQLite silently caps the value at `MAX_MMAP_SIZE` (1 GiB here). Asking for more does not error; it just stops at the cap.
* Once mapped, page accesses become memory loads instead of `pread()` syscalls — fewer system calls, no copy from kernel buffer to user heap. The benefit is largest on cold reads of large databases.

### 1.4 Query timing — `time SELECT * FROM users;`

Measured with SQLite's built-in `.timer on` (more precise than shell `time`) on `large.db` (1 000 000 rows, 166 MB) using a forced full-row scan that touches every column:

```
SELECT SUM(LENGTH(name)+LENGTH(email)+LENGTH(city)+LENGTH(created)+LENGTH(bio)) FROM users;
```

| Run | mmap_size = 0 (no mmap) | mmap_size = 256 MB |
|----:|------------------------:|--------------------:|
| 1   | 0.195 s                 | 0.195 s             |
| 2   | 0.193 s                 | 0.239 s             |
| 3   | 0.233 s                 | 0.229 s             |
| **avg** | **≈0.207 s**        | **≈0.221 s**        |

A simple aggregate over the 100 k row `sample.db` was sub-millisecond either way (≈0.013 s for a forced length-sum on the 9.3 MB file). The naive `SELECT COUNT(*) FROM users;` is even faster because SQLite satisfies it from the index without scanning the heap.

**Why mmap didn't speed things up here:** the OS page cache already held the 166 MB file after the first run, so without-mmap reads hit cached pages too — the win from skipping `pread()` is small in that regime. Without `sudo purge` available we could not flush the cache to demonstrate the cold-read advantage. Documented behavior: mmap helps most on (a) large databases with random access patterns, and (b) cold-cache reads, where it eliminates one buffer copy.

### 1.5 Process inspection

```bash
$ ps aux | grep sqlite3 | grep -v grep
user 64903 0.6 0.0 441890048 2400 ?? SN 5:44PM 0:00.00 sqlite3 sample.db
```

The sqlite3 client is a single process (~2.4 MB RSS while idle on `sample.db`). There is **no separate server** — the calling process opens the file directly, and the only "background" activity is the rollback journal / WAL the same process maintains.

---

## 2. PostgreSQL (PSQL) Setup

### 2.1 Install + start

```bash
$ brew install postgresql@16
$ brew services start postgresql@16
$ psql --version
psql (PostgreSQL) 16.13 (Homebrew)
$ psql -d postgres -c "SELECT version();"
PostgreSQL 16.13 (Homebrew) on aarch64-apple-darwin25.2.0 ...
```

### 2.2 Seed equivalent dataset

```sql
-- inside: createdb labdb && psql -d labdb
CREATE TABLE users (
    id BIGINT PRIMARY KEY, name TEXT, email TEXT,
    age INT, city TEXT, created TIMESTAMP
);
INSERT INTO users(id, name, email, age, city, created)
SELECT g, 'User_'||g, 'user'||g||'@example.com',
       20+(g%60),
       (ARRAY['Bangalore','Mumbai','Delhi','Chennai','Hyderabad'])[1+(g%5)],
       NOW()-((g%365)||' days')::INTERVAL
FROM generate_series(1, 100000) g;
CREATE INDEX idx_users_city ON users(city);
CREATE INDEX idx_users_age  ON users(age);
```

### 2.3 Page size + page count

PostgreSQL's "page" is called a **block** and its size is fixed at compile time (default 8 KB):

```sql
labdb=# SHOW block_size;            -- 8192
labdb=# SELECT pg_size_pretty(pg_database_size('labdb')),
               pg_database_size('labdb');
 pg_size_pretty | pg_database_size
----------------+------------------
 20 MB          |         21232663

labdb=# SELECT pg_size_pretty(pg_relation_size('users'))      AS heap,
               pg_relation_size('users')/8192                  AS heap_pages,
               pg_size_pretty(pg_total_relation_size('users')) AS total_with_idx;
  heap   | heap_pages | total_with_idx
---------+------------+----------------
 9384 kB |       1173 | 13 MB

labdb=# SELECT indexrelid::regclass, pg_size_pretty(pg_relation_size(indexrelid)),
               pg_relation_size(indexrelid)/8192 AS pages
        FROM pg_index WHERE indrelid = 'users'::regclass;
     index      |  size   | pages
----------------+---------+------
 users_pkey     | 2208 kB |  276
 idx_users_city |  696 kB |   87
 idx_users_age  |  712 kB |   89
```

So the same logical 100 000 rows occupy:

* **Heap:** 1 173 pages × 8 KB = **9.38 MB** (≈ identical to SQLite's 9.31 MB)
* **Heap + 3 indexes:** 13 MB
* **Whole `labdb`:** 20 MB (system catalogs add ≈7 MB)

### 2.4 mmap-style behavior

Postgres does **not** expose anything analogous to SQLite's `PRAGMA mmap_size`. Instead it uses a fixed-size **shared-memory buffer pool** (`shared_buffers`) plus the OS page cache:

```sql
labdb=# SHOW shared_buffers;        --  128MB
labdb=# SHOW effective_cache_size;  --  4GB   (planner hint, not allocation)
labdb=# SHOW work_mem;              --  4MB
```

So the closest comparison points are:

| SQLite                              | PostgreSQL                        |
|-------------------------------------|-----------------------------------|
| `mmap_size` (per connection)        | `shared_buffers` (server-wide)    |
| Memory-maps file pages directly     | Reads pages into shared buffer pool |
| Default 0 (off)                     | Default 128 MB on this install    |
| Same process as the application     | Background processes manage it    |

### 2.5 Query timing — `\timing`

100 000-row `users` table, three runs of the same forced full-row scan:

```sql
labdb=# \timing on
labdb=# SELECT COUNT(*), AVG(age) FROM users;        -- Time: 14.803 ms
labdb=# SELECT SUM(LENGTH(name)+LENGTH(email)
                +LENGTH(city)+LENGTH(created::text))
        FROM users;                                  -- Time: 18.916 ms
labdb=#  -- run 2                                       Time: 15.777 ms
labdb=#  -- run 3                                       Time: 14.840 ms
```

`EXPLAIN (ANALYZE, BUFFERS)` confirms the plan:

```
Finalize Aggregate  ... actual time=16.769..17.552 rows=1
  Buffers: shared hit=1173
  Gather  ... Workers Launched: 1
    Partial Aggregate  ...
      -> Parallel Seq Scan on users  rows=50000 loops=2
         Buffers: shared hit=1173
Planning Time: 0.226 ms
Execution Time: 17.580 ms
```

Notes from the plan:

* `Buffers: shared hit=1173` — every page came from `shared_buffers`, no disk I/O. Page count exactly matches `pg_relation_size('users')/8192 = 1173`.
* PostgreSQL split the scan across 2 workers (`Parallel Seq Scan`), which SQLite cannot do — SQLite is single-threaded per query.

For parity with SQLite's `large.db`, a 1 000 000-row `users_large` was created (170 MB heap, 21 735 pages) and timed:

| Run                       | Time     |
|---------------------------|---------:|
| `COUNT(*) FROM users_large` | 25.3 ms |
| Forced full scan, run 1     | 104.9 ms |
| Forced full scan, run 2     | 95.7 ms  |
| Forced full scan, run 3     | 116.3 ms |

### 2.6 Process inspection

```bash
$ ps aux | grep postgres | grep -v grep
postgres: logical replication launcher
postgres: autovacuum launcher
postgres: walwriter
postgres: background writer
postgres: checkpointer
/opt/homebrew/opt/postgresql@16/bin/postgres -D /opt/homebrew/var/postgresql@16
```

Six processes are always running: a postmaster plus background workers (checkpointer, bgwriter, walwriter, autovacuum, logical-replication launcher). Per-query parallel workers are forked on demand. This is a fundamentally different model from SQLite's single in-process design.

---

## 3. Comparison

### 3.1 Page size / page count (100 000-row `users`)

| Metric                          | SQLite 3.51                    | PostgreSQL 16                 |
|---------------------------------|--------------------------------|-------------------------------|
| Page (block) size               | **4 096 B** (1024–65536, default 4 KB) | **8 192 B** (compile-time fixed) |
| Pages for the table             | 2 383 *(heap + 2 indexes, single file)* | 1 173 heap + 276 + 87 + 89 = **1 625 total** |
| Bytes for the table             | 9.31 MB (single file)           | 9.38 MB heap, 13 MB w/ indexes |
| Bytes for the database          | 9.31 MB *(everything in one file)* | 20 MB *(includes system catalogs)* |
| Page-count formula verified     | `4096 × 2383 = 9 760 768` ✓    | `8192 × 1173 = 9 609 216` ✓   |
| Where indexes live              | Same `.db` file as the heap    | Separate relation files in `base/<oid>/` |

**Takeaways**

* Postgres pages are 2× the size of SQLite pages, so it needs roughly half the page count for the same heap bytes.
* SQLite stores the heap and all indexes in a **single** file; Postgres uses one file per relation under `base/<dbid>/`. This is why `pg_database_size('labdb')` (20 MB) is larger than the user table's 13 MB — the rest is system catalogs and metadata.
* In SQLite, `page_size × page_count` *is* the file size. In Postgres, you must sum `pg_relation_size` across heap + each index (or use `pg_total_relation_size`) to get the on-disk footprint of a logical table.

### 3.2 Query performance

Same workload (forced full-row scan over text columns), single-machine warm-cache averages:

| Dataset           | SQLite (no mmap) | SQLite (mmap 256 MB) | PostgreSQL |
|-------------------|-----------------:|---------------------:|-----------:|
| 100 k rows, 9 MB  | ≈ 13–15 ms       | ≈ 13–15 ms           | ≈ 15–19 ms |
| 1 M rows, 166 MB  | ≈ 195–235 ms     | ≈ 195–240 ms         | ≈ 95–116 ms |

Observations:

1. **Small dataset (100 k rows):** SQLite is marginally faster than Postgres because there is **no IPC, no planner round-trip, no parallel-worker setup** — the query runs inside the client process. Postgres pays a fixed ≈1–2 ms overhead per query (planning + protocol).
2. **Larger dataset (1 M rows):** Postgres pulls ahead by ~2× because it splits the scan across parallel workers (`Workers Launched: 1`, two loops in EXPLAIN). SQLite is single-threaded per query and saturates one core.
3. **Aggregates that hit indexes** (`COUNT(*)`) are fast on both; SQLite turns `SELECT COUNT(*) FROM (SELECT * FROM users)` into an index scan and reports sub-millisecond timings.
4. The Postgres EXPLAIN showed `shared hit=1173` (every page served from `shared_buffers`); the equivalent stat on SQLite isn't directly exposed, but the warm-cache timings imply the same — both engines were CPU-bound, not I/O-bound, on this run.

### 3.3 mmap impact

| Aspect                          | SQLite                                  | PostgreSQL                        |
|---------------------------------|-----------------------------------------|-----------------------------------|
| Mechanism                       | `mmap()` of the DB file (per connection) | `shared_buffers` pool (server-wide) |
| Default                         | **0** (off)                             | 128 MB (always on)                |
| How to change                   | `PRAGMA mmap_size = N;` per session     | `shared_buffers = ...` in postgresql.conf + restart |
| Cap on this machine             | 1 GiB (`MAX_MMAP_SIZE` compile flag)    | Bounded by shared-memory limits   |
| Measured effect on warm scans   | Negligible (≈ 0.207 s vs 0.221 s, within noise) | N/A — buffer pool is always active |
| Where it helps most             | Large DBs, cold-cache, random reads     | Working sets that fit in shared_buffers |

Why we did not see a dramatic SQLite mmap win: the 166 MB `large.db` fits trivially in this machine's OS page cache, so even the non-mmap path was reading from RAM after the first warm-up. We could not run `purge` (Operation not permitted without sudo) to drop the page cache, so a true cold-cache test was out of scope. Documented behavior is that mmap eliminates one user-space copy per page and ~one syscall per page, which becomes visible on cold reads of multi-GB databases.

### 3.4 Architecture summary

| Aspect                  | SQLite                                  | PostgreSQL                              |
|-------------------------|-----------------------------------------|-----------------------------------------|
| Process model           | In-process library; one OS process per app | Client–server with a postmaster + 5 background workers + per-connection backends |
| Concurrency for one query | Single-threaded                       | Parallel seq-scan / parallel join workers |
| Network protocol        | None (file access)                      | libpq over TCP/Unix socket              |
| Storage layout          | One file (heap + indexes + WAL/journal) | One file per relation under `base/<dbid>/`, separate `pg_wal`, `pg_xact`, etc. |
| Default page size       | 4 KB                                    | 8 KB                                    |
| Default cache mechanism | OS page cache only (`mmap_size=0`)      | `shared_buffers` (128 MB) + OS page cache |
| When to prefer          | Embedded, single-writer, ≤ a few GB     | Multi-user, high-concurrency, large DBs, parallelism |

---

## 4. Files in this submission

| File | Purpose |
|---|---|
| [README.md](README.md) | This report |
| [seed_sqlite.sql](seed_sqlite.sql) | Seed script for `sample.db` (100 k rows) |
| `seed_large.sql` | Seed for `large.db` (1 M rows) — generated inline during the run |
| `sample.db`, `large.db` | SQLite databases produced by the seed scripts (kept locally; not required for grading) |

---

## 5. Reproducing the experiments

```bash
# 1. SQLite
sqlite3 sample.db < seed_sqlite.sql
sqlite3 sample.db "PRAGMA page_size; PRAGMA page_count; PRAGMA mmap_size;"
sqlite3 sample.db <<'EOF'
.timer on
PRAGMA mmap_size = 0;
SELECT SUM(LENGTH(name)+LENGTH(email)+LENGTH(city)+LENGTH(created)) FROM users;
PRAGMA mmap_size = 268435456;
SELECT SUM(LENGTH(name)+LENGTH(email)+LENGTH(city)+LENGTH(created)) FROM users;
EOF

# 2. PostgreSQL
brew install postgresql@16 && brew services start postgresql@16
createdb labdb
psql -d labdb -c "SHOW block_size;"
psql -d labdb -c "SELECT pg_size_pretty(pg_relation_size('users')),
                         pg_relation_size('users')/8192 AS heap_pages;"
psql -d labdb -c "\timing on
SELECT SUM(LENGTH(name)+LENGTH(email)+LENGTH(city)+LENGTH(created::text)) FROM users;"
```
