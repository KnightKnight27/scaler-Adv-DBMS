# Lab 1 — SQLite3 vs PostgreSQL: Pages, mmap, and Query Timing

**Role Number:** _10347_
**Name:** Tirth Shah_

---

## 1. Environment

| Item | Value |
|---|---|
| OS | macOS (Darwin 25.2) |
| SQLite3 | 3.51.0 |
| PostgreSQL | 17.2 (EnterpriseDB build) |
| Sample tables | `users` (200,000 rows), `orders` (500,000 rows) |
| Hardware note | Same machine for both engines, warm OS page cache between runs |

The same logical schema and row counts were used in both engines so that per-engine
storage and timing numbers can be compared directly.

---

## 2. SQLite3 Exploration

### 2.1 Sample database

```sql
CREATE TABLE users (
  id INTEGER PRIMARY KEY,
  name TEXT, email TEXT, age INTEGER, city TEXT, bio TEXT
);

WITH RECURSIVE seq(n) AS (
  SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n < 200000
)
INSERT INTO users (name, email, age, city, bio)
SELECT 'User_'||n, 'user'||n||'@example.com', 18+(n%60),
       CASE (n%5) WHEN 0 THEN 'Mumbai' WHEN 1 THEN 'Delhi'
                  WHEN 2 THEN 'Bengaluru' WHEN 3 THEN 'Hyderabad'
                  ELSE 'Chennai' END,
       'This is a sample biography string for user number '||n||'.'
FROM seq;

-- 'orders' built similarly with 500,000 rows
```

### 2.2 File size

```bash
$ ls -lh sample.db
-rw-r--r-- 1 parvamshah staff 42M sample.db
```

### 2.3 PRAGMA inspection

```sql
PRAGMA page_size;       -- 4096
PRAGMA page_count;      -- 10646
PRAGMA freelist_count;  -- 0
PRAGMA cache_size;      -- 2000  (pages → ~8 MB)
PRAGMA mmap_size;       -- 0     (memory-mapped I/O DISABLED by default)
PRAGMA journal_mode;    -- delete
PRAGMA encoding;        -- UTF-8
```

Sanity check: `page_size × page_count = 4096 × 10646 ≈ 43.6 MB`, matching the
on-disk file.

### 2.4 Effect of `page_size`

Same `users` table built into two databases that differ only in page size:

| `page_size` | `page_count` | File size |
|---:|---:|---:|
| 4096 (default) | 7142 | 28 MB |
| 8192 | 3538 | 28 MB |

Doubling the page size roughly halves the page count; the total bytes are
similar because the data volume is unchanged. Larger pages mean fewer, bigger
I/Os — better for sequential scans, worse for tiny point lookups (more wasted
bytes per read).

### 2.5 `mmap_size` experiment — query timing

Three queries were timed three times each. Reported numbers below are the
median of three warm-cache runs.

```sql
.timer on
PRAGMA mmap_size = 0;          -- (or 268435456 for 256 MB)
SELECT COUNT(*), AVG(age) FROM users;
SELECT COUNT(*), AVG(amount) FROM orders;
SELECT u.city, COUNT(o.id), SUM(o.amount)
  FROM users u JOIN orders o ON o.user_id = u.id
  GROUP BY u.city;
```

| Query | mmap = 0 (off) | mmap = 256 MB |
|---|---:|---:|
| `COUNT(*) , AVG(age)` on users | 0.011 s | 0.011 s |
| `COUNT(*) , AVG(amount)` on orders | 0.017 s | 0.017 s |
| Join + GROUP BY across both tables | 0.168 s | 0.163 s |

**Observation.** With a 42 MB database and a fast SSD on macOS, the benefit of
enabling `mmap_size` was small (a few percent on the heavier query) because the
file already fits comfortably in the OS page cache. mmap matters more when:

- the database is large enough that bypassing the OS read syscall and the
  user-space SQLite cache copy actually saves work, or
- access is random and you want the kernel to fault pages in lazily rather
  than copying through SQLite's page cache.

`mmap_size = 0` keeps SQLite on its conventional read/write path; setting a
positive value enables memory-mapped I/O up to that byte limit.

### 2.6 SQLite processes

```bash
$ ps aux | grep -i sqlite | grep -v grep
# (empty)
```

SQLite is an **in-process library**, not a server. The `sqlite3` CLI is a
short-lived shell process that exits as soon as the query returns; there is no
persistent daemon. This is the single biggest architectural contrast with
PostgreSQL.

---

## 3. PostgreSQL Exploration

### 3.1 Sample database (same shape as SQLite)

```sql
CREATE DATABASE dbms_lab;
\c dbms_lab

CREATE TABLE users (id SERIAL PRIMARY KEY, name TEXT, email TEXT,
                    age INTEGER, city TEXT, bio TEXT);
INSERT INTO users (name,email,age,city,bio)
SELECT 'User_'||g, 'user'||g||'@example.com', 18+(g%60),
       CASE (g%5) WHEN 0 THEN 'Mumbai' WHEN 1 THEN 'Delhi'
                  WHEN 2 THEN 'Bengaluru' WHEN 3 THEN 'Hyderabad'
                  ELSE 'Chennai' END,
       'This is a sample biography string for user number '||g||'.'
FROM generate_series(1,200000) g;

-- 'orders' built similarly with 500,000 rows
ANALYZE;
```

### 3.2 Server / page settings

```sql
SHOW block_size;             -- 8192
SHOW shared_buffers;         -- 128MB
SHOW effective_cache_size;   -- 4GB
SHOW work_mem;               -- 4MB
```

Postgres' page (block) size is **fixed at compile time**, defaulting to 8 KB.
Unlike SQLite, you cannot change it on an existing cluster with a `PRAGMA`.

### 3.3 Page count and on-disk size

```sql
SELECT relname, relpages AS page_count,
       pg_size_pretty(pg_relation_size(oid)) AS size
FROM pg_class
WHERE relname IN ('users','orders','users_pkey','orders_pkey')
ORDER BY relname;
```

| Relation | Pages | Size |
|---|---:|---:|
| `users` (heap) | 4 251 | 33 MB |
| `users_pkey` (index) | 551 | 4.4 MB |
| `orders` (heap) | 3 356 | 26 MB |
| `orders_pkey` (index) | 1 374 | 11 MB |
| **Database total** | — | **82 MB** |

### 3.4 Query timings

```sql
\timing on
SELECT COUNT(*), AVG(age) FROM users;
SELECT COUNT(*), AVG(amount) FROM orders;
SELECT u.city, COUNT(o.id), SUM(o.amount)
  FROM users u JOIN orders o ON o.user_id=u.id
  GROUP BY u.city;
```

Median of three warm-cache runs:

| Query | Cold (1st run) | Warm (median) |
|---|---:|---:|
| `COUNT(*), AVG(age)` users | 35.3 ms | 7.1 ms |
| `COUNT(*), AVG(amount)` orders | 36.7 ms | 9.6 ms |
| Join + GROUP BY | 52.7 ms | 41 ms |

`EXPLAIN (ANALYZE, BUFFERS)` on the join shows Postgres ran a **Parallel Hash
Join with 2 workers**, scanning 7 635 shared buffers entirely from cache
(`shared hit=7635`, no disk reads), and finishing in ~120 ms with planning.

### 3.5 Postgres processes

```bash
$ ps aux | grep 'postgres:'
postgres  900  postgres: logical replication launcher
postgres  899  postgres: autovacuum launcher
postgres  898  postgres: walwriter
postgres  872  postgres: background writer
postgres  870  postgres: checkpointer
postgres  868  postgres: logger
```

A persistent **multi-process server** with dedicated background workers for
WAL, checkpointing, autovacuum, etc. Each client connection gets its own
backend process.

---

## 4. SQLite vs PostgreSQL — Comparison

### 4.1 Storage

| Dimension | SQLite | PostgreSQL |
|---|---|---|
| Page size (default) | 4096 B | 8192 B |
| Page size configurable | Yes — `PRAGMA page_size` per database | Only at compile time of the cluster |
| Same dataset on disk | 42 MB single file | 82 MB across heap + index files |
| Why bigger on PG | Per-row 24 B header (`xmin`/`xmax`/ctid…), separate B-tree indexes for primary keys, FSM/VM forks | — |

### 4.2 Query performance (same hardware, warm cache)

| Query | SQLite | PostgreSQL |
|---|---:|---:|
| `COUNT/AVG` users (200k) | 11 ms | 7 ms |
| `COUNT/AVG` orders (500k) | 17 ms | 10 ms |
| Join + GROUP BY | **168 ms** | **41 ms** |

Aggregates on a single small table are similar in both engines (both fit in
cache, both stream the heap). On the join, **PostgreSQL is ~4× faster** here
because it picks a parallel hash join across 2 worker processes; SQLite runs
single-threaded with a nested-loop / sort-merge style plan.

### 4.3 mmap impact

| Engine | How mmap is configured | Observed impact on this dataset |
|---|---|---|
| SQLite | `PRAGMA mmap_size = N` (off by default) | Marginal — a few percent on the heavy query at 256 MB mmap |
| PostgreSQL | No per-DB knob; uses `shared_buffers` (128 MB here) plus the OS page cache | Already cache-resident, so disk wasn't the bottleneck |

mmap is most useful in SQLite when the file is large, access is random, and
the cost of `read()` syscalls + extra copies into SQLite's page cache becomes
visible. For a 42 MB DB on an SSD it is essentially noise.

### 4.4 Architecture

| Dimension | SQLite | PostgreSQL |
|---|---|---|
| Process model | In-process library; CLI is a short-lived shell | Persistent server, one backend per connection + background workers |
| Concurrency | Reader/writer locks at file level; one writer at a time (WAL mode helps) | MVCC; many concurrent readers and writers |
| Parallel query execution | No | Yes (parallel seq scan, parallel hash join — observed in `EXPLAIN`) |
| Indexes | Stored inside the same file | Separate relation files per index |
| Auth/network | None — file permissions only | Roles, `pg_hba.conf`, TCP/socket |

### 4.5 When to pick which

- **SQLite** — embedded apps, single-writer workloads, tests, on-device
  storage, anything where "the database is a file you copy" is a feature.
- **PostgreSQL** — multi-user services, larger datasets, concurrent
  writers, queries that benefit from parallelism, anything that needs roles,
  replication, or extensions.

---

## 5. Commands Used (cheat-sheet)

### SQLite

```bash
sqlite3 sample.db
.timer on
.headers on
PRAGMA page_size; PRAGMA page_count; PRAGMA mmap_size; PRAGMA cache_size;
PRAGMA mmap_size = 268435456;   -- enable 256 MB mmap
ls -lh sample.db
ps aux | grep sqlite
```

### PostgreSQL

```bash
psql -U postgres -d dbms_lab
\timing on
SHOW block_size;
SHOW shared_buffers;
SELECT relname, relpages, pg_size_pretty(pg_relation_size(oid))
  FROM pg_class WHERE relname IN ('users','orders');
SELECT pg_size_pretty(pg_database_size('dbms_lab'));
EXPLAIN (ANALYZE, BUFFERS) <query>;
ps aux | grep 'postgres:'
```

---

## 6. Key Takeaways

1. **Page size is a tunable in SQLite, fixed in Postgres.** Doubling
   SQLite's `page_size` halved its page count for the same data.
2. **Same data, different footprint.** Postgres uses ~2× the disk for the
   same logical rows because of MVCC headers and separate index relations.
3. **`mmap_size` matters most for large, random-access SQLite databases**;
   on a 42 MB SSD-resident file, the win was negligible.
4. **PostgreSQL won the heavy query (~4×)** mainly through parallel
   execution, not through cleverer single-threaded code.
5. **Architecturally**, SQLite is a library you link in; PostgreSQL is a
   server you talk to. That single difference drives most of the other
   trade-offs.
