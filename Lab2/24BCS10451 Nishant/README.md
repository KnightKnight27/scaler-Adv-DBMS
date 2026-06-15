# Lab 2: SQLite3 vs PostgreSQL Exploration
 **Role Number:** `24BCS10451`
 **Name:** `Nishant Dasgupta`

## 1) SQLite3 Exploration

### Setup

Database used: `lab2_sample.db` with table `users` with 120,000 rows.

### Commands Used

```bash
# Create/prepare database and data
sqlite3 lab2_sample.db "PRAGMA journal_mode=WAL; CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY, name TEXT, email TEXT); DELETE FROM users; WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<120000) INSERT INTO users(name,email) SELECT 'user_'||x, 'user_'||x||'@mail.com' FROM c;"

# Observe file sizes
ls -lh lab2_sample.db lab2_sample.db-wal lab2_sample.db-shm

# Page size and page count
sqlite3 lab2_sample.db "PRAGMA page_size; PRAGMA page_count; PRAGMA mmap_size;"

# Timing without mmap
time sqlite3 lab2_sample.db "PRAGMA mmap_size=0; SELECT * FROM users;" > /dev/null

# Timing with mmap
time sqlite3 lab2_sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;" > /dev/null

# Process check
ps aux | grep sqlite
```

### Observations

- File sizes (`ls -lh`):
  - `lab2_sample.db`: `4.6M`
  - `lab2_sample.db-shm`: `40K`
  - `lab2_sample.db-wal`: `0B` (after checkpoint/close state)
- `PRAGMA page_size`: `8192` bytes
- `PRAGMA page_count`: `610`
- Initial `PRAGMA mmap_size`: `0`
- Query timing (`SELECT * FROM users`):
  - Without mmap (`mmap_size=0`): `real 0.05s`
  - With mmap (`mmap_size=268435456`): `real 0.04s`

Result: For this dataset and run, mmap tuning did not show a visible wall time change. The workload was likely cache-friendly.

---

## 2) PostgreSQL (PSQL) Setup and Exploration

### Setup

PostgreSQL version observed:

- `PostgreSQL 15.7 (Homebrew) ... 64-bit`

Sample table used: `users_pg` with 120,000 rows.

### Commands Used

```bash
# Version
psql -d postgres -c "SELECT version();"

# Create/prepare table and data
psql -d postgres -c "DROP TABLE IF EXISTS users_pg; CREATE TABLE users_pg(id BIGINT PRIMARY KEY, name TEXT, email TEXT); INSERT INTO users_pg SELECT g, 'user_'||g, 'user_'||g||'@mail.com' FROM generate_series(1,120000) g; ANALYZE users_pg;"

# Page size (block size)
psql -d postgres -At -c "SHOW block_size;"

# Approx page count and tuple count
psql -d postgres -c "SELECT relpages, reltuples FROM pg_class WHERE relname='users_pg';"

# Query execution plan and execution time
psql -d postgres -c "EXPLAIN ANALYZE SELECT * FROM users_pg;"

# End-to-end wall time from client side
time psql -d postgres -c "SELECT * FROM users_pg;" > /dev/null
```

### Observations

- Block size (`SHOW block_size`): `16384` bytes
- `relpages`: `740`
- `reltuples`: `120000`
- `EXPLAIN ANALYZE` execution time: `~8.1 ms`
- Client wall time (`time psql ...`): `real 0.19s`

---

## 3) Comparison: SQLite3 vs PostgreSQL

| Metric | SQLite3 | PostgreSQL |
|---|---:|---:|
| Page size | 8192 bytes | 16384 bytes |
| Page count (sample table/db) | 610 (db-level from `PRAGMA`) | 740 (`relpages` for `users_pg`) |
| Query performance (`SELECT *`, 120k rows) | ~0.05s (CLI wall time) | ~8.1 ms server execution, ~0.19s client wall time |
| mmap impact | Small improvement (`0.05s` vs `0.04s`) | No direct per-query mmap toggle; memory handled by PostgreSQL + OS |

### Analysis

- SQLite favors quick, local, single-file scans with minimal setup overhead.
- PostgreSQL surfaces richer diagnostics (`EXPLAIN ANALYZE`, planner statistics) that help tune performance.
- SQLite allows explicit mmap tuning with `PRAGMA mmap_size`, while PostgreSQL relies on shared buffers and OS caching.
- Client-side wall time can be inflated by I/O and formatting; server timing is the clearer metric for PostgreSQL.

---

## 4) Conclusion

- Both engines handled the 120k-row scan comfortably.
- SQLite delivered fast local reads with minimal configuration.
- PostgreSQL offered stronger visibility into execution behavior and tuning signals.
- In this run, changing SQLite mmap settings did not significantly alter runtime.
