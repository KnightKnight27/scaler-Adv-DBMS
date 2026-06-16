# Lab 2: SQLite3 vs PostgreSQL Exploration

**Role Number:** `24BCS10222`
**Name:** `Mohit Kumar`

---

## 1) SQLite3 Exploration

### Setup

Database: `test.db` with table `records` containing 150,000 rows.

### Commands Used

```bash
# Create database with 150k rows
sqlite3 test.db "
  PRAGMA journal_mode=WAL;
  CREATE TABLE IF NOT EXISTS records(
    id    INTEGER PRIMARY KEY,
    uname TEXT,
    email TEXT
  );
  DELETE FROM records;
  WITH RECURSIVE seq(n) AS (
    SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n < 150000
  )
  INSERT INTO records(uname, email)
  SELECT 'user_'||n, 'u'||n||'@test.com' FROM seq;
"

# File sizes after WAL checkpoint
ls -lh test.db test.db-wal test.db-shm

# Storage metadata
sqlite3 test.db "PRAGMA page_size;"
sqlite3 test.db "PRAGMA page_count;"
sqlite3 test.db "PRAGMA mmap_size;"

# Query time — mmap disabled
time sqlite3 test.db "PRAGMA mmap_size=0; SELECT * FROM records;" > /dev/null

# Query time — mmap enabled (128 MB)
time sqlite3 test.db "PRAGMA mmap_size=134217728; SELECT * FROM records;" > /dev/null

# Check any running sqlite3 processes
ps aux | grep sqlite3
```

### Observations

| Item | Value |
|------|-------|
| `test.db` file size | `5.5M` |
| `test.db-wal` (post-checkpoint) | `0B` |
| `PRAGMA page_size` | `4096` bytes |
| `PRAGMA page_count` | `1477` |
| `PRAGMA mmap_size` (default) | `0` |
| Query time — without mmap | `real 0m0.071s` |
| Query time — with mmap (128 MB) | `real 0m0.058s` |

**mmap observation:** With `mmap_size=134217728` (128 MB), query time dropped from ~71 ms to ~58 ms (~18% faster). When mmap is enabled, SQLite maps the database file into the process's virtual address space. Page reads become memory accesses (served by the OS page cache via page faults) rather than explicit `read()` syscalls, eliminating the kernel-to-user copy overhead. The gain is more noticeable at 150k rows than at smaller sizes because more pages need to be fetched.

---

## 2) PostgreSQL (PSQL) Setup and Exploration

### Setup

PostgreSQL version: `PostgreSQL 16.2`

Table: `records_pg` with 150,000 rows.

### Commands Used

```bash
# Check version
psql -d postgres -c "SELECT version();"

# Create and populate table
psql -d postgres -c "
  DROP TABLE IF EXISTS records_pg;
  CREATE TABLE records_pg (
    id    BIGINT PRIMARY KEY,
    uname TEXT,
    email TEXT
  );
  INSERT INTO records_pg
    SELECT g, 'user_'||g, 'u'||g||'@test.com'
    FROM generate_series(1, 150000) g;
  ANALYZE records_pg;
"

# Block size
psql -d postgres -At -c "SHOW block_size;"

# Page count and estimated row count
psql -d postgres -c "
  SELECT relpages, reltuples::bigint AS rows
  FROM pg_class
  WHERE relname = 'records_pg';
"

# Execution plan with actual timing
psql -d postgres -c "EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM records_pg;"

# Wall time including client overhead
time psql -d postgres -c "SELECT * FROM records_pg;" > /dev/null
```

### Observations

| Item | Value |
|------|-------|
| `SHOW block_size` | `8192` bytes |
| `relpages` | `1063` |
| `reltuples` | `150000` |
| `EXPLAIN ANALYZE` execution time | `~11.4 ms` |
| Client wall time | `real 0m0.198s` |

**Note:** `EXPLAIN (ANALYZE, BUFFERS)` also showed `Buffers: shared hit=1063` on a warm run, confirming all pages were served from `shared_buffers` with no disk I/O.

---

## 3) Comparison: SQLite3 vs PostgreSQL

| Metric | SQLite3 | PostgreSQL |
|--------|--------:|----------:|
| Page / block size | 4096 bytes | 8192 bytes |
| Page count (150k rows) | 1477 | 1063 (`relpages`) |
| `SELECT *` — 150k rows | ~71 ms (no mmap) / ~58 ms (mmap) | ~11.4 ms server / ~198 ms client wall |
| mmap control | `PRAGMA mmap_size` (per session) | Via OS + `shared_buffers`; no per-query toggle |
| mmap impact | ~18% speedup with 128 MB | N/A — internal buffer pool manages caching |
| Setup complexity | Zero — single file | Requires server process, roles, config |
| Concurrency | Limited (WAL mode helps) | Full multi-user with MVCC |
| Observability | `PRAGMA`, `sqlite_stat*` tables | `EXPLAIN ANALYZE`, `pg_stat_*`, `pg_class` |

### Analysis

- **Page size difference:** PostgreSQL's 8 KB blocks pack more rows per page than SQLite's 4 KB pages. This is why PostgreSQL needs only 1063 pages vs SQLite's 1477 for the same 150k rows — fewer pages means fewer I/O operations for a full scan.

- **mmap in SQLite:** Enabling `mmap_size` removes the `read()` syscall path for cached data. Instead, page faults on the mmapped region let the OS serve data from the page cache directly into the process's address space. The ~18% improvement at 150k rows is more noticeable than it would be at smaller sizes.

- **PostgreSQL server-side vs client wall time:** The `EXPLAIN ANALYZE` time (~11 ms) reflects only the query engine execution. The client wall time (~198 ms) includes socket setup, result serialization, network transfer, and terminal output. For benchmarking, `EXPLAIN ANALYZE` is the right number to compare.

- **PostgreSQL `BUFFERS` output:** The `shared hit=1063` confirms zero disk reads on a warm cache — all pages were in `shared_buffers`. This is analogous to SQLite's page cache serving data without disk access.

---

## 4) Conclusion

- SQLite's simplicity makes it excellent for embedded and local-storage use cases; `PRAGMA mmap_size` gives direct, low-level control over memory-mapped I/O.
- PostgreSQL's larger block size, MVCC, and rich diagnostics (`EXPLAIN ANALYZE`, buffer statistics) make it the right choice for concurrent, server-grade workloads.
- At 150k rows, mmap in SQLite gave an ~18% query speedup by eliminating syscall overhead for cached reads.
- PostgreSQL's server-side execution was the fastest in absolute terms (~11 ms), but requires accounting for client overhead when measuring end-to-end latency.
