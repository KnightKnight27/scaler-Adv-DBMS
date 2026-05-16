# SQLite3 vs PostgreSQL — Storage & Page-Cache Lab

**Name:** Ashutosh Kumar
**Roll Number:** 24BSC10111
**Course:** Advanced DBMS (SST / Scaler)
**Date:** 2026-05-09
**Environment:** Ubuntu 24.04 (Linux 6.17, 16 GB RAM), SQLite 3.45.1, PostgreSQL 16.13

---

## 1. Overview

This lab compares how SQLite3 and PostgreSQL store data on disk and how the
operating-system page cache (and SQLite's `mmap_size`) affect query latency.
Both engines were given the **same workload** so the on-disk numbers and
timings are directly comparable:

- `users`  — 200,000 rows (id, name, email, city, age, created)
- `orders` — 1,000,000 rows (id, user_id, amount, placed_at) with an index
  on `user_id`
- one secondary index on `users.city`

Total: 1.2M rows, two indexes.

---

## 2. SQLite3

### 2.1 Installing & launching

```bash
sudo apt-get install -y sqlite3
sqlite3 --version
# 3.45.1 2024-01-30 ...
```

### 2.2 Building the sample database

The full builder script is in section 5; the abbreviated version is:

```sql
PRAGMA journal_mode = WAL;

CREATE TABLE users  (id INTEGER PRIMARY KEY, name TEXT, email TEXT,
                     city TEXT, age INTEGER, created TEXT);
CREATE TABLE orders (id INTEGER PRIMARY KEY, user_id INTEGER, amount REAL,
                     placed_at TEXT);

WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n<200000)
INSERT INTO users SELECT n, 'User_'||n, 'user'||n||'@example.com',
       (CASE n%5 WHEN 0 THEN 'Bengaluru' WHEN 1 THEN 'Delhi' WHEN 2 THEN 'Mumbai'
                  WHEN 3 THEN 'Chennai' ELSE 'Hyderabad' END),
       18 + n%60, date('2024-01-01', '+'||(n%365)||' days') FROM seq;

WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n<1000000)
INSERT INTO orders SELECT n, 1+(n%200000),
       round(abs(random()%10000)/100.0, 2),
       date('2024-01-01', '+'||(n%730)||' days') FROM seq;

CREATE INDEX idx_orders_user_id ON orders(user_id);
CREATE INDEX idx_users_city     ON users(city);
ANALYZE;
```

Run with: `time sqlite3 sample.db < build_sqlite.sql` — completed in **1.6 s**.

### 2.3 File size on disk

```text
$ ls -lh sample.db*
-rw-r--r-- 1 ashutosh ashutosh 58M May  9 23:27 sample.db
```

A WAL-mode database also creates `sample.db-wal` and `sample.db-shm` while a
connection is open; both are 0 bytes after a clean shutdown.

### 2.4 PRAGMA values

```text
sqlite> PRAGMA page_size;     -- 4096      (4 KiB pages, the default)
sqlite> PRAGMA page_count;    -- 14638
sqlite> PRAGMA cache_size;    -- -2000     (2 MiB private cache; negative = KiB)
sqlite> PRAGMA journal_mode;  -- wal
sqlite> PRAGMA mmap_size;     -- 0         (mmap disabled by default)
```

**Sanity check:** `page_size × page_count = 4096 × 14638 = 59,957,248 B ≈ 57.2 MiB`,
matching `ls -lh` to within rounding.

Page distribution per object (from the `dbstat` virtual table):

| Object                | Pages | Share of file |
| --------------------- | ----- | ------------- |
| `orders` (heap)       | 7826  | 53.5 %        |
| `users` (heap)        | 3124  | 21.3 %        |
| `idx_orders_user_id`  | 2898  | 19.8 %        |
| `idx_users_city`      |  788  |  5.4 %        |
| `sqlite_stat1` + schema | 2   | <0.1 %        |

The user-id index alone is bigger than the entire `users` table.

### 2.5 Experimenting with `mmap_size`

`mmap_size` tells SQLite how many bytes of the file it may map into the
process's address space and read directly with pointer arithmetic, bypassing
the per-page `pread()` + memcpy that the default I/O path performs.

| Setting                         | What it means                                     |
| ------------------------------- | ------------------------------------------------- |
| `PRAGMA mmap_size = 0;`         | mmap disabled — every page goes through `pread()` |
| `PRAGMA mmap_size = 268435456;` | up to 256 MiB mapped (more than the 58 MiB DB)    |

Caches were dropped between runs with
`sync; echo 3 > /proc/sys/vm/drop_caches` so "run 1" really had to read from
disk. Timings come from `sqlite3 .timer on`:

| Query                                                 | mmap=0, run 1 | mmap=0, run 2 (warm) | mmap=256MB, run 1 (cold) | mmap=256MB, run 2 (warm) |
| ----------------------------------------------------- | ------------- | -------------------- | ------------------------ | ------------------------ |
| `SELECT COUNT(*) FROM users;`                         | 0.004 s       | 0.004 s              | 0.004 s                  | 0.004 s                  |
| `SELECT COUNT(*) FROM orders;`                        | 0.006 s       | 0.005 s              | 0.010 s                  | 0.005 s                  |
| Top-10 spenders (group-by + order-by on `orders`)     | **0.288 s**   | 0.276 s              | 0.304 s                  | **0.262 s**              |
| Avg amount per city (`users ⨝ orders`, group-by)      | **0.349 s**   | 0.342 s              | 0.316 s                  | **0.332 s**              |

Raw transcript of one run (without mmap, cold cache):

```text
sqlite> .timer on
sqlite> PRAGMA mmap_size=0;
0
Run Time: real 0.000 user 0.000000 sys 0.000038
sqlite> SELECT COUNT(*) FROM users;
200000
Run Time: real 0.004 user 0.000000 sys 0.001995
sqlite> SELECT COUNT(*) FROM orders;
1000000
Run Time: real 0.006 user 0.003456 sys 0.000000
sqlite> SELECT user_id, SUM(amount) AS spend FROM orders GROUP BY user_id ORDER BY spend DESC LIMIT 10;
22678|479.68
172280|474.52
118072|467.08
...
Run Time: real 0.288 user 0.256509 sys 0.013770
sqlite> SELECT u.city, AVG(o.amount) FROM users u JOIN orders o ON o.user_id=u.id GROUP BY u.city;
Bengaluru|50.00538825
Chennai|50.03225195
Delhi|49.97008145
Hyderabad|49.9442023
Mumbai|49.962953
Run Time: real 0.349 user 0.297014 sys 0.049603
```

**What the numbers say**

- The whole 58 MiB database fits comfortably in the OS page cache after the
  first read, so the cold-vs-warm gap is small (~70 ms on the join), and the
  mmap-vs-no-mmap gap is even smaller — within run-to-run noise.
- `mmap` shows its biggest theoretical win on databases **larger than RAM**
  with random reads, where eliminating one user/kernel copy per page becomes
  visible. At this scale you mostly observe noise.
- `COUNT(*)` is only ~5 ms even on the cold run because the rows stream
  sequentially through one B-tree, paging in adjacent disk blocks.

### 2.6 `ps aux` while a SQLite query runs

A long recursive CTE was launched in the background to keep the process alive
long enough to inspect:

```text
$ sqlite3 sample.db "PRAGMA mmap_size=268435456; \
    WITH RECURSIVE r(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM r WHERE n<50000000) \
    SELECT COUNT(*) FROM r;" &

$ ps aux | grep sqlite
USER       PID %CPU %MEM   VSZ   RSS TTY STAT  COMMAND
ashutosh 107427  100  0.0  6196  4276  ?  R    sqlite3 sample.db PRAGMA mmap_size=268435456; …

$ grep -E "Vm|Rss|Threads" /proc/107427/status
VmPeak:    6196 kB
VmSize:    6196 kB
VmRSS:     4276 kB
RssAnon:    404 kB
RssFile:   3872 kB
Threads:      1
```

SQLite is a **library running inside the user's own process** — one thread,
~6 MB virtual size, ~4 MB resident. There is no separate server, no
connection pool, no shared-memory segment.

---

## 3. PostgreSQL

### 3.1 Installing & launching

```bash
sudo apt-get install -y postgresql postgresql-contrib
sudo systemctl start postgresql
psql --version
# psql (PostgreSQL) 16.13 (Ubuntu 16.13-0ubuntu0.24.04.1)
```

### 3.2 Building the sample database

```sql
CREATE TABLE users  (id INTEGER PRIMARY KEY, name TEXT, email TEXT,
                     city TEXT, age INTEGER, created DATE);
CREATE TABLE orders (id INTEGER PRIMARY KEY,
                     user_id INTEGER REFERENCES users(id),
                     amount NUMERIC(10,2), placed_at DATE);

INSERT INTO users (id, name, email, city, age, created)
SELECT n, 'User_'||n, 'user'||n||'@example.com',
       (ARRAY['Bengaluru','Delhi','Mumbai','Chennai','Hyderabad'])[1 + n%5],
       18 + n%60, DATE '2024-01-01' + ((n%365)||' days')::interval
FROM generate_series(1, 200000) AS n;

INSERT INTO orders (id, user_id, amount, placed_at)
SELECT n, 1 + n%200000, round((random()*100)::numeric, 2),
       DATE '2024-01-01' + ((n%730)||' days')::interval
FROM generate_series(1, 1000000) AS n;

CREATE INDEX idx_orders_user_id ON orders(user_id);
CREATE INDEX idx_users_city     ON users(city);
ANALYZE;
```

Run with: `sudo -u postgres psql -d lab_sample -f build_postgres.sql`.

### 3.3 File layout on disk

PostgreSQL spreads a database across many files in
`$DATA_DIRECTORY/base/<DB-OID>/`, **one file per relation** (heap or index),
split into 1 GiB segments. There is no single `.db` file equivalent.

```text
$ sudo -u postgres psql -tAc "SHOW data_directory"
/var/lib/postgresql/16/main

$ sudo ls -lhS /var/lib/postgresql/16/main/base/16388 | head -7
-rw------- 1 postgres postgres  50M  16396       # orders heap
-rw------- 1 postgres postgres  22M  16399       # toast / fsm files
-rw------- 1 postgres postgres  17M  16389       # users heap
-rw------- 1 postgres postgres  12M  16406       # idx_orders_user_id
-rw------- 1 postgres postgres 4.4M  16394       # users PK
-rw------- 1 postgres postgres 1.4M  16407       # idx_users_city

$ sudo du -sh /var/lib/postgresql/16/main/base/16388
107M

postgres=# SELECT pg_size_pretty(pg_database_size('lab_sample'));   -- 112 MB
```

Per-relation sizes (matching the SQLite breakdown):

| Relation             | Size   | Bytes      |
| -------------------- | ------ | ---------- |
| `orders`             | 50 MB  | 52,183,040 |
| `users`              | 16 MB  | 17,252,352 |
| `idx_orders_user_id` | 11 MB  | 11,681,792 |
| `idx_users_city`     | 1.3 MB |  1,409,024 |

### 3.4 PRAGMA-equivalents (`SHOW` / `pg_class`)

PostgreSQL exposes settings via `SHOW` and per-relation page counts via
`pg_class.relpages`:

```text
postgres=# SHOW block_size;            -- 8192      (8 KiB pages, twice SQLite's default)
postgres=# SHOW shared_buffers;        -- 128MB     (PG's own buffer cache, separate from OS cache)
postgres=# SHOW effective_cache_size;  -- 4GB       (planner hint about combined PG+OS cache)
postgres=# SHOW work_mem;              -- 4MB       (per-operator working memory)
postgres=# SHOW maintenance_work_mem;  -- 64MB      (used by VACUUM, CREATE INDEX, etc.)
postgres=# SHOW wal_buffers;           -- 4MB       (WAL staging area)
```

Page count per relation:

| Relation             | Pages | Computed size (×8 KiB) |
| -------------------- | ----- | ---------------------- |
| `orders`             | 6370  | 50 MB                  |
| `users`              | 2106  | 16 MB                  |
| `idx_orders_user_id` | 1426  | 11 MB                  |
| `idx_users_city`     | 172   | 1.3 MB                 |
| **Whole DB total**   | 14204 | 111 MB                 |

So the same 1.2M rows that took **14,638 × 4 KiB = 57 MiB** in SQLite take
**14,204 × 8 KiB = 111 MiB** in PostgreSQL — roughly a **2× expansion**, driven
by 8 KiB pages, 24-byte MVCC tuple headers, alignment, and the per-relation
free-space and visibility-map sidecar files.

### 3.5 Query timings (`\timing on`)

Caches were dropped before run 1 the same way as in the SQLite section.

| Query                                    | Run 1 (cold) | Run 2 (warm) |
| ---------------------------------------- | ------------ | ------------ |
| `SELECT COUNT(*) FROM users;`            | 13.75 ms     |  8.99 ms     |
| `SELECT COUNT(*) FROM orders;`           | 19.26 ms     | 15.14 ms     |
| Top-10 spenders                          | 298.94 ms    | 267.76 ms    |
| Avg per city (join + group-by)           | 109.67 ms    | 102.90 ms    |

`EXPLAIN (ANALYZE, BUFFERS)` for the join — the most interesting query —
showed PostgreSQL using a **Parallel Hash Join with two workers**, fully
served from the buffer cache (no `read=…`):

```text
Finalize GroupAggregate            (actual time=132.162..135.294 rows=5 loops=1)
  Group Key: u.city
  Buffers: shared hit=8520
  ->  Gather Merge                 Workers Planned: 2  Workers Launched: 2
       ->  Partial HashAggregate   (actual time=130.265..130.268 rows=5 loops=3)
              ->  Parallel Hash Join  Hash Cond: (o.user_id = u.id)
                     ->  Parallel Seq Scan on orders o   rows=333333 loops=3
                     ->  Parallel Hash  Buckets: 262144  Memory: 11488 kB
                            ->  Parallel Seq Scan on users u  rows=66667 loops=3
Planning Time: 0.166 ms
Execution Time: 135.330 ms
```

None of this infrastructure exists in SQLite (no parallel workers, no
buffer-cache hit metrics). That difference accounts for most of the gap on
the join query.

### 3.6 `ps aux` for PostgreSQL

```text
$ ps aux | grep postgres
postgres 104740  /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main …
postgres 104741  postgres: 16/main: checkpointer
postgres 104742  postgres: 16/main: background writer
postgres 104745  postgres: 16/main: walwriter
postgres 104746  postgres: 16/main: autovacuum launcher
postgres 104747  postgres: 16/main: logical replication launcher
```

Total RSS across all 6 background processes: **~72 MB**, *before any client
has connected*. Each new client will spawn a dedicated backend on top of
that. This multi-process architecture is the entire reason PostgreSQL needs
a separate `shared_buffers` setting (it is shared memory all backends mmap
into) while SQLite needs nothing of the kind.

---

## 4. Side-by-side comparison

| Property                       | SQLite3                                                              | PostgreSQL                                                                |
| ------------------------------ | -------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| Architecture                   | Embedded library, single process, one file                          | Client/server, postmaster + per-connection backend, multi-file data dir   |
| **Page size (default)**        | **4 KiB** (`PRAGMA page_size`)                                       | **8 KiB** (`SHOW block_size`)                                             |
| **Page count for this DB**     | 14,638                                                               | 14,204                                                                    |
| **Total on-disk size**         | **58 MiB** (one `.db` file)                                          | **112 MiB** (one heap + indexes + FSM/VM/TOAST per relation)              |
| Per-row overhead               | Variable header (~3–10 bytes)                                        | 24-byte tuple header + alignment, per MVCC                                |
| How to inspect                 | `PRAGMA page_size`, `PRAGMA page_count`, `dbstat` virtual table      | `SHOW block_size`, `pg_class.relpages`, `pg_relation_size()`              |
| Cache layer                    | OS page cache + optional `mmap_size`; private 2 MiB row cache       | Private `shared_buffers` (128 MiB default) **plus** OS page cache         |
| Concurrency                    | Single writer, many readers (WAL); no parallel query                | MVCC + per-connection backend; **parallel workers** for big scans/joins   |
| `SELECT COUNT(*)` of 1M rows   | ~5 ms                                                                | ~15–19 ms                                                                 |
| Group-by top-10 spenders       | ~280 ms                                                              | ~270–300 ms                                                               |
| Join + group-by 1M ⨝ 200K      | ~340 ms (single thread)                                              | ~105 ms (Parallel Hash Join, 2 workers)                                   |
| Process RSS at rest            | **~4 MB** (single sqlite3 process during a query)                    | **~72 MB** total across 6 server processes, before any client             |
| `mmap` impact at this scale    | Negligible — DB fits in OS cache                                     | N/A — PG always uses `pread()` + `shared_buffers`, not `mmap`             |

### 4.1 Where each one wins

**SQLite is faster** at trivially small queries (`COUNT(*)`, point lookups)
because there is no IPC, no planner overhead, no connection setup — the call
goes straight from your code into a function that walks a B-tree.

**PostgreSQL is faster** the moment a query benefits from parallelism or a
larger working-memory budget. The avg-per-city join finished in ~105 ms in PG
(two parallel workers, 11 MB hash table) versus ~340 ms in SQLite (single
thread, 2 MB row cache). The crossover point in this workload is somewhere
around the join — anything heavier and the gap grows.

### 4.2 What `mmap_size` taught us

For a database that fits in RAM, `mmap_size` is essentially **invisible**:
with or without it, the second read comes from the OS page cache and is
fast. The setting matters when the database is *bigger than RAM* and access
is random — then bypassing the `pread()`+memcpy path per page reduces CPU and
memory-bandwidth pressure. PostgreSQL has no direct equivalent — its
`shared_buffers` is a privately managed cache with its own replacement
policy, and relation files are accessed via `pread()`/`pwrite()`, never
`mmap`.

### 4.3 Bottom line

- Use **SQLite** when the data lives next to the application, fits on one
  disk, and you don't need parallel queries or many concurrent writers — the
  whole DBMS overhead is a few megabytes of RSS.
- Use **PostgreSQL** when you have multiple writers, larger-than-RAM data,
  query-planner-driven optimisation, or workloads that benefit from parallel
  workers — at the cost of a real server, real configuration, and real RAM.

---

## 5. Reproducing every number in this report

### SQLite section

```bash
# 1. Build the database
sqlite3 sample.db <<'SQL'
PRAGMA journal_mode = WAL;
CREATE TABLE users  (id INTEGER PRIMARY KEY, name TEXT, email TEXT,
                     city TEXT, age INTEGER, created TEXT);
CREATE TABLE orders (id INTEGER PRIMARY KEY, user_id INTEGER, amount REAL,
                     placed_at TEXT);
WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n<200000)
INSERT INTO users SELECT n, 'User_'||n, 'user'||n||'@example.com',
       (CASE n%5 WHEN 0 THEN 'Bengaluru' WHEN 1 THEN 'Delhi' WHEN 2 THEN 'Mumbai'
                  WHEN 3 THEN 'Chennai' ELSE 'Hyderabad' END),
       18 + n%60, date('2024-01-01', '+'||(n%365)||' days') FROM seq;
WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n<1000000)
INSERT INTO orders SELECT n, 1+(n%200000),
       round(abs(random()%10000)/100.0, 2),
       date('2024-01-01', '+'||(n%730)||' days') FROM seq;
CREATE INDEX idx_orders_user_id ON orders(user_id);
CREATE INDEX idx_users_city     ON users(city);
ANALYZE;
SQL

# 2. File size and PRAGMA values
ls -lh sample.db
sqlite3 sample.db "PRAGMA page_size; PRAGMA page_count; PRAGMA mmap_size;"

# 3. Cold-cache timings (Linux)
sync && sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
sqlite3 sample.db <<'SQL'
.timer on
PRAGMA mmap_size=0;            -- or 268435456 to enable mmap
SELECT COUNT(*) FROM users;
SELECT COUNT(*) FROM orders;
SELECT user_id, SUM(amount) AS spend
FROM orders GROUP BY user_id ORDER BY spend DESC LIMIT 10;
SELECT u.city, AVG(o.amount)
FROM users u JOIN orders o ON o.user_id=u.id GROUP BY u.city;
SQL

# 4. Inspect the live process
sqlite3 sample.db "WITH RECURSIVE r(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM r WHERE n<50000000) SELECT COUNT(*) FROM r;" &
ps aux | grep sqlite
cat /proc/$!/status | grep -E "Vm|Rss|Threads"
```

### PostgreSQL section

```bash
# 1. Create the database and load schema
sudo -u postgres createdb lab_sample
sudo -u postgres psql -d lab_sample <<'SQL'
CREATE TABLE users  (id INTEGER PRIMARY KEY, name TEXT, email TEXT,
                     city TEXT, age INTEGER, created DATE);
CREATE TABLE orders (id INTEGER PRIMARY KEY,
                     user_id INTEGER REFERENCES users(id),
                     amount NUMERIC(10,2), placed_at DATE);
INSERT INTO users (id, name, email, city, age, created)
SELECT n, 'User_'||n, 'user'||n||'@example.com',
       (ARRAY['Bengaluru','Delhi','Mumbai','Chennai','Hyderabad'])[1 + n%5],
       18 + n%60, DATE '2024-01-01' + ((n%365)||' days')::interval
FROM generate_series(1, 200000) AS n;
INSERT INTO orders (id, user_id, amount, placed_at)
SELECT n, 1 + n%200000, round((random()*100)::numeric, 2),
       DATE '2024-01-01' + ((n%730)||' days')::interval
FROM generate_series(1, 1000000) AS n;
CREATE INDEX idx_orders_user_id ON orders(user_id);
CREATE INDEX idx_users_city     ON users(city);
ANALYZE;
SQL

# 2. Page / size info
sudo -u postgres psql -d lab_sample -c "SHOW block_size;"
sudo -u postgres psql -d lab_sample -c "
  SELECT relname, relpages, pg_size_pretty(pg_relation_size(oid))
  FROM pg_class
  WHERE relname IN ('users','orders','idx_orders_user_id','idx_users_city')
  ORDER BY relpages DESC;"
sudo -u postgres psql -d lab_sample -c "SELECT pg_size_pretty(pg_database_size('lab_sample'));"

# 3. Physical files
DDIR=$(sudo -u postgres psql -tAc "SHOW data_directory")
DBOID=$(sudo -u postgres psql -tAc "SELECT oid FROM pg_database WHERE datname='lab_sample'")
sudo ls -lhS "$DDIR/base/$DBOID" | head
sudo du -sh "$DDIR/base/$DBOID"

# 4. Cold-cache timings
sync && sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
sudo -u postgres psql -d lab_sample <<'SQL'
\timing on
SELECT COUNT(*) FROM users;
SELECT COUNT(*) FROM orders;
SELECT user_id, SUM(amount) AS spend
FROM orders GROUP BY user_id ORDER BY spend DESC LIMIT 10;
SELECT u.city, AVG(o.amount)
FROM users u JOIN orders o ON o.user_id=u.id GROUP BY u.city;
EXPLAIN (ANALYZE, BUFFERS)
SELECT u.city, AVG(o.amount)
FROM users u JOIN orders o ON o.user_id=u.id GROUP BY u.city;
SQL

# 5. Server processes
ps aux | grep postgres
ps -C postgres -o rss --no-headers | awk '{ s += $1 } END { printf "%d KB total\n", s }'
```

All numbers in this report were produced on the lab machine using exactly
these commands.
