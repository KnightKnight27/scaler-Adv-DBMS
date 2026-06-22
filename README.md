# SQLite3 vs PostgreSQL — Storage & Query Performance Lab

**Role Number:** 24BCS10203
**Name:** Dhruv Davda
**Date:** 2026-05-09
**Platform:** macOS (Darwin 24.6.0, Apple Silicon)

---

## 1. Setup

| Tool | Version | Install |
|---|---|---|
| SQLite3 | 3.43.2 | `brew install sqlite3` |
| PostgreSQL | 14.19 | `brew install postgresql@14` + `brew services start postgresql@14` |
| Sample DB | Chinook (extended) | `curl -L -o chinook.zip https://www.sqlitetutorial.net/wp-content/uploads/2018/03/chinook.zip && unzip chinook.zip` |

Both engines were given the **same workload**: a synthetic `big_data` table with 500,000 rows containing an `id`, a hex/md5 payload (~64 chars), and a random `val` column with a B-tree index.

---

## 2. SQLite3 Experiments

### 2.1 File size on disk

```bash
$ ls -lh chinook.db
-rw-r--r--@ 1 dhruvdavda  staff   864K Nov 29  2015 chinook.db   # bare Chinook
-rw-r--r--@ 1 dhruvdavda  staff    77M May  9 23:10 chinook.db   # after adding big_data
```

The whole database — schema, data, indexes, free pages — lives in **one file**. There is no server process; `sqlite3` is just a CLI that opens the file directly.

### 2.2 PRAGMA introspection

```sql
sqlite> PRAGMA page_size;     -- 1024   (bytes per page)
sqlite> PRAGMA page_count;    -- 78449  (after big_data; was 864 originally)
sqlite> PRAGMA mmap_size;     -- 0      (mmap disabled by default)
sqlite> PRAGMA cache_size;    -- 2000   (pages held in cache)
sqlite> PRAGMA journal_mode;  -- delete (rollback journal)
```

Sanity check: `78449 pages × 1024 bytes = 80,331,776 bytes ≈ 77 MB` ✓ — matches `ls -lh`.

### 2.3 Building the workload table

```sql
CREATE TABLE big_data AS
WITH RECURSIVE cnt(x) AS (
  SELECT 1 UNION ALL SELECT x+1 FROM cnt WHERE x < 500000
)
SELECT x AS id, hex(randomblob(64)) AS payload, abs(random() % 10000) AS val
FROM cnt;
CREATE INDEX idx_big_val ON big_data(val);
```

### 2.4 Query timings — mmap OFF vs ON

Each query was run **3 times** consecutively (warm OS cache).

```sql
.timer on
PRAGMA mmap_size = 0;          -- or 268435456 (256 MB) for the second pass
SELECT COUNT(*) FROM big_data WHERE val < 5000;
SELECT AVG(val)  FROM big_data;
SELECT val, COUNT(*) FROM big_data GROUP BY val ORDER BY COUNT(*) DESC LIMIT 5;
```

| Query | mmap = 0 (run 1 / 2 / 3) | mmap = 256 MB (run 1 / 2 / 3) |
|---|---|---|
| `COUNT(*) WHERE val < 5000` | 13 / 11 / 10 ms | 12 / 10 / 9 ms |
| `AVG(val)` (full scan) | 20 / 20 / 20 ms | 21 / 21 / 20 ms |
| `GROUP BY val ORDER BY count` | 31 / 30 / 30 ms | 29 / 35 / 29 ms |

### 2.5 Query plan (covering index in action)

```text
sqlite> EXPLAIN QUERY PLAN SELECT COUNT(*) FROM big_data WHERE val < 5000;
`--SEARCH big_data USING COVERING INDEX idx_big_val (val<?)
```

The `idx_big_val` index is a **covering** index for this query — the count never touches the heap.

### 2.6 `ps aux | grep sqlite`

```text
(empty)
```

SQLite is **embedded**: there is no resident server process. Each `sqlite3` invocation opens the file, runs the query, and exits. This is fundamentally different from PostgreSQL.

---

## 3. PostgreSQL Experiments

### 3.1 Page / buffer settings

```sql
dbms_lab=# SHOW block_size;            -- 8192 bytes (compile-time constant)
dbms_lab=# SHOW shared_buffers;        -- 128MB
dbms_lab=# SHOW effective_cache_size;  -- 4GB
dbms_lab=# SHOW wal_block_size;        -- 8192
```

Unlike SQLite, the page size in Postgres is a **compile-time** constant (default 8 KB). It cannot be changed without recompiling.

### 3.2 Building the same workload table

```sql
CREATE TABLE big_data AS
SELECT g AS id,
       md5(random()::text) || md5(random()::text) AS payload,
       (random() * 10000)::int AS val
FROM generate_series(1, 500000) g;
CREATE INDEX idx_big_val ON big_data(val);
ANALYZE big_data;
```

### 3.3 Storage stats

```sql
SELECT relpages, reltuples,
       pg_size_pretty(pg_relation_size('big_data')) AS table_size
FROM pg_class WHERE relname = 'big_data';
```

| relpages | reltuples | table_size | pg_database_size |
|---|---|---|---|
| 6667 | 500000 | 52 MB | 64 MB |

Sanity check: `6667 × 8 KB ≈ 53 MB` ✓.

### 3.4 Query timings (`\timing on`, 3 runs each)

| Query | Run 1 | Run 2 | Run 3 |
|---|---|---|---|
| `COUNT(*) WHERE val < 5000` | 31.6 ms | 26.4 ms | 18.5 ms |
| `AVG(val)` (full scan) | 20.4 ms | 21.8 ms | 18.8 ms |
| `GROUP BY val ORDER BY count` | 35.7 ms | 38.0 ms | 35.1 ms |

### 3.5 Query plan (parallel sequential scan)

```text
dbms_lab=# EXPLAIN ANALYZE SELECT COUNT(*) FROM big_data WHERE val < 5000;
 Finalize Aggregate
   ->  Gather (Workers Planned: 2, Workers Launched: 2)
         ->  Partial Aggregate
               ->  Parallel Seq Scan on big_data
                     Filter: (val < 5000)
 Planning Time: 0.737 ms
 Execution Time: 218.082 ms
```

Postgres chose a **parallel sequential scan** with 2 workers — interesting because SQLite chose a covering index for the same query. With ~50% of the rows matching the filter, Postgres correctly judged a full scan cheaper than index lookups, but did not use the index as a covering scan (it has visibility-map / MVCC overhead).

### 3.6 `ps aux | grep postgres`

```text
postgres: logical replication launcher
postgres: stats collector
postgres: autovacuum launcher
postgres: walwriter
postgres: background writer
postgres: checkpointer
/opt/homebrew/opt/postgresql@14/bin/postgres -D /opt/homebrew/var/postgresql@14
```

Postgres is a **client/server** system with a postmaster plus dedicated background workers (autovacuum, WAL writer, background writer, checkpointer, etc.). Resident even when no client is connected.

---

## 4. Comparison Analysis

### 4.1 Side-by-side

| Property | SQLite3 | PostgreSQL |
|---|---|---|
| **Architecture** | Embedded library, single file | Client-server, multi-process |
| **Page size** | 1024 bytes (default) — runtime configurable, must `VACUUM` to change | 8192 bytes — **compile-time** constant |
| **Page count (500k row table)** | 78,449 pages × 1 KB = ~77 MB (whole DB) | 6,667 pages × 8 KB = ~52 MB (table only) |
| **Storage on disk** | Single `.db` file | Cluster directory (`/opt/homebrew/var/postgresql@14`) with WAL, base, etc. |
| **Cache mechanism** | Per-connection page cache + optional **mmap** of the DB file | **`shared_buffers`** (128 MB by default) shared across backends |
| **Concurrency** | One writer at a time (file lock) | MVCC; many concurrent writers |
| **Background processes** | None | 7+ (postmaster, autovacuum, WAL writer, checkpointer, ...) |
| **Query parallelism** | No | Yes (parallel seq scan used here, 2 workers) |

### 4.2 Query performance (warm cache, 500k rows)

| Query | SQLite (median) | Postgres (median) |
|---|---|---|
| `COUNT(*) WHERE val < 5000` | **11 ms** (covering index) | 26 ms (parallel seq scan) |
| `AVG(val)` full scan | **20 ms** | 21 ms |
| `GROUP BY val LIMIT 5` | **30 ms** | 36 ms |

SQLite was *faster* on this workload. Why?

1. **No IPC.** SQLite runs in the same process — no client/server round trips, no parsing handshake, no row-format serialization across a socket.
2. **Small scale.** 500k rows fit comfortably in OS cache for both engines, so I/O isn't the bottleneck.
3. **Covering index.** SQLite used `idx_big_val` as a covering index for the count; Postgres has a **visibility map** check on top of every heap row, so a covering index-only scan needs `VACUUM` to mark pages all-visible. Without that, it preferred a parallel seq scan.
4. **MVCC overhead.** Every Postgres row has 23 bytes of `xmin`/`xmax`/etc. metadata that SQLite simply doesn't carry.

This **does not** mean SQLite is "better" — Postgres pays this overhead in exchange for crash safety, MVCC, concurrent writers, replication, parallelism, partitioning, and many other features SQLite lacks. At 50 million rows or with concurrent writers, the ranking would flip.

### 4.3 mmap impact (SQLite)

| Query | mmap = 0 (median) | mmap = 256 MB (median) |
|---|---|---|
| `COUNT(*) WHERE val < 5000` | 11 ms | 10 ms |
| `AVG(val)` | 20 ms | 21 ms |
| `GROUP BY val` | 30 ms | 29 ms |

**Observation:** essentially **no measurable difference** on this workload.

**Why:**
- The OS page cache is already serving the file, so reads from the SQLite page cache (`pread`) and reads from the mmap region (`memcpy` from a mapped page) take similar time.
- The 77 MB DB easily fits in macOS's free RAM, so no actual disk I/O happens after the first pass.
- mmap's win comes from avoiding the `pread` syscall per page and avoiding a memcpy from kernel buffer → user buffer. Those wins matter most when:
  - The DB is much larger than `cache_size` but smaller than RAM.
  - Workload is **random reads** with **cold cache**.
  - Multiple processes share the same DB file (mmap pages are shared via the kernel).
- macOS doesn't expose a non-`sudo` way to drop OS caches (`purge` requires root), so a clean cold-cache comparison wasn't possible from this lab.

**Postgres analogue:** Postgres does not use `mmap` for its data files. It uses `shared_buffers` (a fixed-size shared memory region populated via `read()`) plus reliance on the OS page cache. The two systems have philosophically different approaches to caching: SQLite leans on the OS, Postgres maintains its own buffer pool with explicit eviction policies and pinning.

---

## 5. Commands Used (consolidated)

### SQLite
```bash
sqlite3 chinook.db
sqlite3 chinook.db "PRAGMA page_size; PRAGMA page_count; PRAGMA mmap_size;"
sqlite3 chinook.db ".timer on" "SELECT ..."
ls -lh chinook.db
ps aux | grep sqlite     # empty — no daemon
```

### PostgreSQL
```bash
psql -d postgres -c "CREATE DATABASE dbms_lab;"
psql -d dbms_lab -c "SHOW block_size;"
psql -d dbms_lab -c "SHOW shared_buffers;"
psql -d dbms_lab           # then \timing on, run queries
psql -d dbms_lab -c "SELECT relpages, pg_size_pretty(pg_relation_size('big_data')) FROM pg_class WHERE relname='big_data';"
ps aux | grep postgres
```

---

## 6. Conclusions

1. **SQLite is a library, Postgres is a server.** This single difference explains most of what follows: latency, concurrency model, process footprint, configurability.
2. **Page size matters less than you'd think at this scale.** Both engines fit the working set in RAM; the 8× difference in page size (1 KB vs 8 KB) didn't translate into an 8× anything.
3. **mmap was effectively a no-op here** because warm OS cache already served everything. Its real benefits show up under cold cache / large-file / multi-process conditions.
4. **For a single-user, embedded, ≤100 MB workload, SQLite is genuinely faster.** Pick Postgres for concurrency, durability guarantees, scale, replication, and rich query features — not for raw single-query latency on tiny data.
