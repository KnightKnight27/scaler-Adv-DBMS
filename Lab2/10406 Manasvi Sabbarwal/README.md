# Lab 2: SQLite3 vs PostgreSQL Exploration

**Role Number:** `24BCS10406`
**Name:** `Manasvi Sabbarwal`
**Platform:** macOS (Darwin 25.3.0, aarch64) — SQLite 3.51.0, PostgreSQL 16.13 (Homebrew)

---

## 1) SQLite3 Exploration

### Setup

I created a local single-file database `sample.db` containing one table
`employees` with **200,000 rows**. The schema is intentionally narrow so the
file size reflects row data, not metadata overhead.

```sql
CREATE TABLE employees (
    id      INTEGER PRIMARY KEY,
    fname   TEXT,
    dept    TEXT,
    salary  INTEGER
);
```

### Commands Used

```bash
# install (already shipped on macOS, otherwise via Homebrew)
brew install sqlite

# build the database with 200k generated rows
sqlite3 sample.db <<'SQL'
PRAGMA journal_mode=WAL;
CREATE TABLE IF NOT EXISTS employees (
    id INTEGER PRIMARY KEY, fname TEXT, dept TEXT, salary INTEGER
);
WITH RECURSIVE gen(n) AS (
    SELECT 1 UNION ALL SELECT n+1 FROM gen WHERE n < 200000
)
INSERT INTO employees(fname, dept, salary)
SELECT 'emp_'||n, 'dept_'||(n%50), 30000 + (n%70000) FROM gen;
SQL

# look at the on-disk footprint
ls -lh sample.db sample.db-wal sample.db-shm

# storage metadata via PRAGMA
sqlite3 sample.db "PRAGMA page_size;"
sqlite3 sample.db "PRAGMA page_count;"
sqlite3 sample.db "PRAGMA mmap_size;"
sqlite3 sample.db "PRAGMA freelist_count;"

# query timing — mmap disabled
time sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM employees;" > /dev/null

# query timing — mmap enabled (256 MB region)
time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM employees;" > /dev/null

# inspect any sqlite3 process while a long query runs
ps aux | grep sqlite3
```

### Observations

| Metric | Value |
|---|---|
| `sample.db` size (`ls -lh`) | `5.8M` |
| `sample.db-wal` (after checkpoint) | `0B` |
| `sample.db-shm` | `32K` |
| `PRAGMA page_size` | `4096` bytes |
| `PRAGMA page_count` | `1490` |
| `PRAGMA mmap_size` (default) | `0` |
| `PRAGMA freelist_count` | `0` |
| `SELECT *` — `mmap_size=0` (run 1 / run 2) | `0.061s` / `0.060s` |
| `SELECT *` — `mmap_size=268435456` (run 1 / run 2) | `0.061s` / `0.054s` |

Sanity check: `1490 pages × 4096 B/page ≈ 6.10 MB`, which matches the 5.8 MB
reported by `ls -lh` (the small difference is empty pages / file rounding).

### mmap Notes

- With `mmap_size=0`, every page access goes through an explicit `pread()`
  syscall and the bytes are copied from the kernel page cache into the
  SQLite-side page buffer.
- Setting `mmap_size=268435456` (256 MB) tells SQLite to memory-map the file
  into the process's virtual address space. After that, page reads become
  ordinary memory loads — the kernel still faults pages in, but the
  kernel→user copy disappears.
- On my machine the gain was modest at this data size (~10% best case, run
  to run noise is comparable to the speedup). The page cache stayed warm
  between runs, which is why the second non-mmap run is already fast.
- Increasing `mmap_size` above the file size has no extra effect — SQLite
  only maps as much as exists. Setting it back to `0` reverts to `pread()`.

---

## 2) PostgreSQL Exploration

### Setup

I started a local Postgres 16 server (on port 5433 because port 5432 was
already taken on this machine) and built an analogous table
`employees_pg` with the same 200,000 rows.

### Commands Used

```bash
# install + start
brew install postgresql@16
brew services start postgresql@16   # default port 5432
# ...or run on a custom port if 5432 is taken:
pg_ctl -D /opt/homebrew/var/postgresql@16 -l /tmp/pg.log -o "-p 5433" start

# verify version
psql -p 5433 -d postgres -c "SELECT version();"

# create + populate
psql -p 5433 -d postgres <<'SQL'
DROP TABLE IF EXISTS employees_pg;
CREATE TABLE employees_pg (
    id BIGINT PRIMARY KEY, fname TEXT, dept TEXT, salary INTEGER
);
INSERT INTO employees_pg
SELECT g, 'emp_'||g, 'dept_'||(g%50), 30000 + (g%70000)
FROM generate_series(1, 200000) g;
ANALYZE employees_pg;
SQL

# block (page) size
psql -p 5433 -d postgres -At -c "SHOW block_size;"

# page count + tuple estimate from the system catalog
psql -p 5433 -d postgres -c "
  SELECT relpages, reltuples::bigint AS rows
  FROM pg_class WHERE relname = 'employees_pg';"

# total relation size on disk
psql -p 5433 -d postgres -c "SELECT pg_size_pretty(pg_relation_size('employees_pg'));"

# query timing + buffer accounting
psql -p 5433 -d postgres -c "EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM employees_pg;"

# end-to-end client wall time
time psql -p 5433 -d postgres -c "SELECT * FROM employees_pg;" > /dev/null
```

### Observations

| Metric | Value |
|---|---|
| `SHOW block_size` | `8192` bytes |
| `SHOW wal_block_size` | `8192` bytes |
| `relpages` | `1471` |
| `reltuples` | `200000` |
| `pg_relation_size` | `11 MB` |
| `shared_buffers` (default) | `128 MB` |
| Execution Time (cold first run) | `13.411 ms` |
| Execution Time (warm second run) | `11.542 ms` |
| `Buffers: shared hit` | `1471` (every page from buffer pool) |
| Client wall time `time psql` | `0.255s` / `0.255s` |

Sanity check: `1471 pages × 8192 B/page ≈ 12.05 MB`, very close to the
`pg_relation_size` of 11 MB (the difference is unfilled space at the end of
the last page).

`Buffers: shared hit=1471` on both runs means **every page was served from
`shared_buffers` with zero physical disk reads** — the buffer pool was warm
right after `INSERT`. There is no per-query mmap toggle in Postgres; caching
behavior is governed by `shared_buffers` plus the OS page cache.

---

## 3) Comparison: SQLite3 vs PostgreSQL

| Aspect | SQLite3 | PostgreSQL |
|---|---|---|
| Default page / block size | **4096 B** | **8192 B** |
| Page count for 200k rows | **1490** | **1471** |
| File / relation size | **~5.8 MB** | **~11 MB** |
| `SELECT *` timing | ~60 ms (no mmap) / ~54 ms (mmap) | 11–13 ms server / ~255 ms client |
| Caching strategy | OS page cache + optional `mmap` | Internal `shared_buffers` (128 MB) + OS page cache |
| mmap control | Per-connection `PRAGMA mmap_size` | No per-query equivalent — tune `shared_buffers` |
| Architecture | Embedded library, single file | Client–server with MVCC, WAL, autovacuum |
| Concurrency model | One writer at a time (WAL mode allows readers in parallel) | MVCC — many readers and writers concurrently |
| Observability | `PRAGMA`, `EXPLAIN QUERY PLAN`, `sqlite_stat*` | `EXPLAIN (ANALYZE, BUFFERS)`, `pg_class`, `pg_stat_*` |

### Analysis

- **Page size.** Postgres's 8 KB block holds roughly twice as many rows per
  page as SQLite's 4 KB page. Even though Postgres is storing more *bytes*
  per row (because of MVCC headers — see below), it still ends up with a
  *lower* page count for the same row count (1471 vs 1490). SQLite's 4 KB
  page lines up with the macOS/Linux VM page size, which keeps mmap
  granularity simple.

- **On-disk size.** The Postgres table is roughly **2× larger on disk** than
  the SQLite database for the same logical rows. The difference is MVCC
  bookkeeping: each Postgres tuple carries `xmin`, `xmax`, `cmin`, `cmax`,
  a `ctid`, and alignment padding — all of which SQLite doesn't need
  because it doesn't do row-level versioning.

- **Query performance.** Comparing engines fairly here is tricky. The
  Postgres `EXPLAIN ANALYZE` number (~11 ms) measures only server-side
  execution and is the fastest absolute time. But a client invoking
  `psql -c "SELECT *"` from outside takes ~255 ms because it pays for
  process spawn, libpq connect, network/socket round-trip, result
  serialization, and terminal output. SQLite has no client/server boundary
  — the ~60 ms wall time *is* the end-to-end time, so the wall-clock gap
  between the two is much smaller than the engine-level gap.

- **mmap impact.** On SQLite, switching from `mmap_size=0` to a 256 MB
  mapping shaved ~10% off the full-scan time on this 200k-row table. The
  gain comes from removing the kernel→user copy in the read path, not from
  caching (the page cache covered that already). For larger tables the
  effect grows, since the per-page copy cost compounds. Postgres has no
  equivalent per-query knob — it owns its own buffer pool, and warming it
  (which happens automatically after the first scan) is the analogous
  optimization.

- **WAL & journaling.** SQLite in WAL mode produces `sample.db-wal` and
  `sample.db-shm` sidecars; after a checkpoint the WAL was truncated to
  `0B`. Postgres has its own write-ahead log under `pg_wal/` with
  `wal_block_size = 8192`. Both engines use WAL for durability; only
  Postgres also uses it as the basis for streaming replication.

- **When to pick which.** SQLite is unbeatable for embedded, single-process,
  zero-setup workloads — config files, CLI tools, mobile apps, test
  fixtures. Postgres is the right choice the moment you need multiple
  concurrent writers, durable transactions over a network, advanced query
  planning, extensions, or replication.

---

## 4) Conclusion

- SQLite stores the same 200k rows in **~5.8 MB** across **1490 4-KB pages**;
  Postgres uses **~11 MB** across **1471 8-KB pages**. The size gap is
  MVCC overhead, not query-engine inefficiency.
- Enabling `mmap_size` in SQLite gave a small but real ~10% speedup at this
  data size by skipping the `pread()` copy. Postgres's equivalent lever is
  `shared_buffers`, and on a warm pool every page hit was from cache.
- Server-side execution time strongly favors Postgres (~11 ms vs ~60 ms),
  but client wall-clock time tells a different story (~255 ms vs ~60 ms)
  because Postgres pays for the client/server round-trip on every query.
- The right metric depends on what you're measuring: `EXPLAIN ANALYZE` for
  engine performance, `time psql` / `time sqlite3` for end-to-end user
  experience.
