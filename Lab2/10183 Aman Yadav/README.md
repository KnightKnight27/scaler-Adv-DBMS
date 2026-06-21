# Lab 2: SQLite3 vs PostgreSQL Exploration

**Role Number:** `10183`
**Name:** `Aman Yadav`

---

## 1) SQLite3 Exploration

### Setup

A local database file `sample.db` was created with a single table `employees`
populated with 200,000 rows. The table has a small fixed schema so that the
storage footprint is dominated by the row data and not by metadata.

### Commands Used

```bash
# Install (Ubuntu/Debian)
sudo apt-get install sqlite3

# Build the database with 200k rows
sqlite3 sample.db <<'SQL'
PRAGMA journal_mode=WAL;
CREATE TABLE IF NOT EXISTS employees (
    id      INTEGER PRIMARY KEY,
    fname   TEXT,
    dept    TEXT,
    salary  INTEGER
);
DELETE FROM employees;
WITH RECURSIVE gen(n) AS (
    SELECT 1 UNION ALL SELECT n+1 FROM gen WHERE n < 200000
)
INSERT INTO employees(fname, dept, salary)
SELECT 'emp_'||n, 'dept_'||(n%50), 30000 + (n%70000) FROM gen;
SQL

# Inspect file sizes
ls -lh sample.db sample.db-wal sample.db-shm

# Storage metadata via PRAGMA
sqlite3 sample.db "PRAGMA page_size;"
sqlite3 sample.db "PRAGMA page_count;"
sqlite3 sample.db "PRAGMA mmap_size;"
sqlite3 sample.db "PRAGMA freelist_count;"

# Query timing — mmap disabled
time sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM employees;" > /dev/null

# Query timing — mmap enabled (256 MB region)
time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM employees;" > /dev/null

# Inspect any live sqlite3 process
ps aux | grep sqlite3
```

### Observations

| Metric | Value |
|---|---|
| `sample.db` size (`ls -lh`) | `7.4M` |
| `sample.db-wal` after checkpoint | `0B` |
| `PRAGMA page_size` | `4096` bytes |
| `PRAGMA page_count` | `1893` |
| `PRAGMA mmap_size` (default) | `0` |
| `PRAGMA freelist_count` | `0` |
| `SELECT *` — without mmap | `real 0m0.094s` |
| `SELECT *` — with mmap (256 MB) | `real 0m0.076s` |

**mmap notes.** With `mmap_size=0`, every page read goes through an explicit
`pread()` syscall and is then copied from the kernel's page cache into the
SQLite-side buffer. Setting `mmap_size=268435456` lets SQLite map the data file
into the process's virtual address space; subsequent page accesses become
ordinary memory reads (faulted in by the OS) and the extra kernel→user copy is
removed. On the 200k-row table the wall time drops by roughly 19%.

Increasing `mmap_size` above the file size has no further effect — SQLite only
maps as much as actually exists. Setting it back to `0` and re-running the
query restores the original timing, confirming the gain comes from the mapping
itself and not from caching effects across runs.

---

## 2) PostgreSQL Exploration

### Setup

A `postgres` server was started locally and a fresh table `employees_pg` was
created with the same shape and 200,000 rows for an apples-to-apples
comparison.

### Commands Used

```bash
# Install (Ubuntu/Debian)
sudo apt-get install postgresql postgresql-contrib
sudo systemctl start postgresql

# Verify version
psql -d postgres -c "SELECT version();"

# Create and populate
psql -d postgres <<'SQL'
DROP TABLE IF EXISTS employees_pg;
CREATE TABLE employees_pg (
    id      BIGINT PRIMARY KEY,
    fname   TEXT,
    dept    TEXT,
    salary  INTEGER
);
INSERT INTO employees_pg
SELECT g, 'emp_'||g, 'dept_'||(g%50), 30000 + (g%70000)
FROM generate_series(1, 200000) g;
ANALYZE employees_pg;
SQL

# Page (block) size
psql -d postgres -At -c "SHOW block_size;"

# Page count and row estimate from system catalog
psql -d postgres -c "
SELECT relpages, reltuples::bigint AS rows
FROM pg_class WHERE relname = 'employees_pg';
"

# Total table size on disk
psql -d postgres -c "SELECT pg_size_pretty(pg_relation_size('employees_pg'));"

# Execution timing with buffer stats
psql -d postgres -c "EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM employees_pg;"

# End-to-end wall time
time psql -d postgres -c "SELECT * FROM employees_pg;" > /dev/null
```

### Observations

| Metric | Value |
|---|---|
| `SHOW block_size` | `8192` bytes |
| `relpages` | `1429` |
| `reltuples` | `200000` |
| `pg_relation_size` | `~11 MB` |
| `EXPLAIN (ANALYZE, BUFFERS)` execution time | `~14.7 ms` |
| `Buffers: shared hit` (warm run) | `1429` |
| Client wall time (`time psql ...`) | `real 0m0.241s` |

`Buffers: shared hit=1429` on the warm run confirms every page was served from
`shared_buffers` with zero physical disk reads. The cold run showed a mix of
`shared read` entries and a longer execution time (~38 ms) before the buffer
pool was warm.

---

## 3) Comparison: SQLite3 vs PostgreSQL

| Aspect | SQLite3 | PostgreSQL |
|---|---|---|
| Page / block size | 4096 B | 8192 B |
| Page count (200k rows) | 1893 | 1429 |
| File / relation size | ~7.4 MB | ~11 MB |
| `SELECT *` timing | ~94 ms (no mmap) / ~76 ms (mmap) | ~14.7 ms server / ~241 ms client |
| Caching model | OS page cache + optional `mmap` | Internal `shared_buffers` + OS page cache |
| mmap control | Per-connection `PRAGMA mmap_size` | No per-query toggle |
| Architecture | Embedded, single file | Client–server with MVCC |
| Observability | `PRAGMA`, `sqlite_stat*` | `EXPLAIN (ANALYZE, BUFFERS)`, `pg_class`, `pg_stat_*` |

### Analysis

- **Page size.** PostgreSQL's 8 KB block packs more rows per page than
  SQLite's 4 KB page, so for the same 200k rows PostgreSQL needs fewer pages
  (1429 vs 1893). Fewer pages means fewer block-level lookups during a full
  scan. SQLite's 4 KB matches the typical OS page size, which keeps single-page
  reads cheap and aligns naturally with the kernel's mmap granularity.

- **On-disk size.** PostgreSQL stores more per-row metadata (xmin/xmax/cmin
  visibility headers, alignment padding) for MVCC, which is why the relation
  is larger on disk (~11 MB) than the SQLite file (~7.4 MB) for the same logical
  rows.

- **Query performance.** PostgreSQL's server-side execution time (~14.7 ms) is
  the fastest absolute number, but the end-to-end `time psql` run is dominated
  by socket setup, result serialization, and terminal output. SQLite skips all
  of that — its number *is* the end-to-end number, which is why the gap looks
  smaller in wall-clock terms.

- **mmap impact.** Enabling `mmap_size` in SQLite gave a ~19% speedup at this
  data size. The improvement comes from removing the kernel→user copy in the
  read path; the OS page cache still does the actual caching. PostgreSQL has no
  comparable per-query knob — it manages its own buffer pool, and the way you
  influence caching is by sizing `shared_buffers` and warming the pool. The
  `Buffers: shared hit` counter from `EXPLAIN ANALYZE` is the equivalent
  diagnostic.

- **When to pick which.** SQLite wins for embedded, single-writer, local-file
  workloads where setup cost matters. PostgreSQL wins as soon as multiple
  clients, durable transactions across a network, or rich query planning
  features are needed.

---

## 4) Conclusion

- SQLite's 4 KB pages and single-file layout keep the engine simple; mmap is a
  cheap one-line speedup when the data fits in RAM.
- PostgreSQL's 8 KB pages, MVCC headers, and server architecture cost more on
  disk and in setup but pay off in concurrency and observability.
- At 200k rows, mmap shaved ~19% off SQLite's full-scan time; PostgreSQL's
  warm-cache execution stayed at ~15 ms server-side regardless.
- The right comparison metric depends on what you're measuring: server-side
  execution (`EXPLAIN ANALYZE`) for engine performance, wall-clock time for
  end-to-end user experience.
