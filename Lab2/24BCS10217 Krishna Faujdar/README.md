# Lab 2: SQLite3 vs PostgreSQL Exploration

**Role Number:** `24BCS10217`
**Name:** `Krishna Faujdar`

---

## 1) SQLite3 Exploration

### Setup

Database used: `lab2.db` with table `users` containing 100,000 rows.

### Commands Used

```bash
# Create database and populate with 100k rows
sqlite3 lab2.db "PRAGMA journal_mode=WAL;
  CREATE TABLE IF NOT EXISTS users(
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT
  );
  DELETE FROM users;
  WITH RECURSIVE c(x) AS (
    SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x < 100000
  )
  INSERT INTO users(name, email)
  SELECT 'user_'||x, 'user_'||x||'@example.com' FROM c;"

# Observe file size
ls -lh lab2.db lab2.db-wal lab2.db-shm

# Page size, page count, current mmap_size
sqlite3 lab2.db "PRAGMA page_size; PRAGMA page_count; PRAGMA mmap_size;"

# Time query WITHOUT mmap
time sqlite3 lab2.db "PRAGMA mmap_size=0; SELECT * FROM users;" > /dev/null

# Time query WITH mmap (256 MB)
time sqlite3 lab2.db "PRAGMA mmap_size=268435456; SELECT * FROM users;" > /dev/null

# Check running sqlite processes
ps aux | grep sqlite
```

### Observations

| Item | Value |
|------|-------|
| `lab2.db` file size (`ls -lh`) | `3.8M` |
| `lab2.db-wal` (after close) | `0B` |
| `PRAGMA page_size` | `4096` bytes |
| `PRAGMA page_count` | `985` |
| `PRAGMA mmap_size` (default) | `0` |
| `SELECT * FROM users` — without mmap | `real 0m0.048s` |
| `SELECT * FROM users` — with mmap (256 MB) | `real 0m0.041s` |

**mmap observation:** Enabling mmap reduced wall time slightly (~15%). With mmap, SQLite maps database pages into the process address space via `mmap()`, letting the OS page cache serve reads without an extra kernel-to-user copy. On small warm datasets the difference is modest; it becomes more pronounced on larger databases or cold-cache runs.

---

## 2) PostgreSQL (PSQL) Setup and Exploration

### Setup

PostgreSQL version: `PostgreSQL 16.3` (Linux/macOS).

Sample table: `users_lab2` with 100,000 rows.

### Commands Used

```bash
# Confirm version
psql -d postgres -c "SELECT version();"

# Create and populate table
psql -d postgres -c "
  DROP TABLE IF EXISTS users_lab2;
  CREATE TABLE users_lab2 (
    id   BIGINT PRIMARY KEY,
    name TEXT,
    email TEXT
  );
  INSERT INTO users_lab2
    SELECT g, 'user_'||g, 'user_'||g||'@example.com'
    FROM generate_series(1, 100000) g;
  ANALYZE users_lab2;
"

# Block (page) size
psql -d postgres -At -c "SHOW block_size;"

# Page count and row count from catalog
psql -d postgres -c "
  SELECT relpages, reltuples::bigint
  FROM pg_class
  WHERE relname = 'users_lab2';
"

# Execution plan with timing
psql -d postgres -c "EXPLAIN ANALYZE SELECT * FROM users_lab2;"

# Client-side wall time
time psql -d postgres -c "SELECT * FROM users_lab2;" > /dev/null
```

### Observations

| Item | Value |
|------|-------|
| `SHOW block_size` | `8192` bytes |
| `relpages` | `841` |
| `reltuples` | `100000` |
| `EXPLAIN ANALYZE` execution time | `~8.3 ms` |
| Client wall time (`time psql ...`) | `real 0m0.162s` |

**Note:** Client wall time includes TCP/socket handshake, result serialization, and terminal output. `EXPLAIN ANALYZE` execution time reflects actual server-side query execution only.

---

## 3) Comparison: SQLite3 vs PostgreSQL

| Metric | SQLite3 | PostgreSQL |
|--------|--------:|----------:|
| Page / block size | 4096 bytes | 8192 bytes |
| Page count (100k rows) | 985 (`PRAGMA page_count`) | 841 (`relpages` in `pg_class`) |
| Query time — `SELECT *` 100k rows | ~0.048s (without mmap) | ~8.3 ms server / ~0.162s client |
| mmap support | Yes — `PRAGMA mmap_size` | Managed by OS + `shared_buffers`; no per-query toggle |
| mmap impact | ~15% faster with 256 MB mmap | Not directly configurable per query |
| Architecture | Embedded, single-file, serverless | Client–server, process-based |
| Observability | `PRAGMA` commands, `sqlite_stat*` | `EXPLAIN ANALYZE`, `pg_stat_*`, `pg_class` |

### Analysis

- **Page size:** PostgreSQL defaults to 8 KB blocks, double SQLite's 4 KB default. Larger blocks reduce tree depth in B-tree indexes and are better suited for sequential scans on large tables. SQLite's 4 KB default aligns with the common OS page size, making single-page operations cheaper.

- **Page count:** PostgreSQL's 841 pages vs SQLite's 985 pages for the same 100k rows reflects the larger block size packing more rows per page.

- **Query performance:** Both are fast on 100k rows. SQLite's embedded model eliminates any IPC overhead, giving low absolute latency for local workloads. PostgreSQL's `EXPLAIN ANALYZE` timing is server-side only and is comparable; the extra client wall time comes from the client–server round trip and output handling.

- **mmap impact:** SQLite's `PRAGMA mmap_size` directly controls how much of the database file is memory-mapped. With mmap, file reads become page-fault-driven rather than `read()` syscall-driven, removing one kernel-to-user copy. PostgreSQL manages its own `shared_buffers` pool and relies on the OS for additional page caching; there is no equivalent single-session mmap toggle.

---

## 4) Conclusion

- SQLite is ideal for embedded, single-user, or local-storage scenarios: zero configuration, low overhead, and direct mmap control.
- PostgreSQL excels in multi-user, server-grade workloads with richer diagnostics (`EXPLAIN ANALYZE`, buffer statistics, system catalog views).
- For sequential scans on 100k rows, both systems deliver sub-second performance; differences are primarily in architecture and observability rather than raw speed at this scale.
- mmap in SQLite provides a measurable but modest speedup on warm datasets; the benefit grows with database size and cold-cache conditions.
