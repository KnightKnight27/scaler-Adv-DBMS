# Task 2 - SQLite3 and PostgreSQL Exploration

## Submission Details (for PR)

- **Role Number:** `24BCS10031`
- **Name:** `Prabal Patra`

---

## 1) SQLite3 Exploration

### Setup

Sample database used: `sample.db` with table `users` containing 100,000 rows.

### Commands Used

```bash
# Create/prepare database and data
sqlite3 sample.db "PRAGMA journal_mode=WAL; CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY, name TEXT, email TEXT); DELETE FROM users; WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<100000) INSERT INTO users(name,email) SELECT 'user_'||x, 'user_'||x||'@mail.com' FROM c;"

# Observe file sizes
ls -lh sample.db sample.db-wal sample.db-shm

# Page size and page count
sqlite3 sample.db "PRAGMA page_size; PRAGMA page_count; PRAGMA mmap_size;"

# Timing without mmap
time sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM users;" > /dev/null

# Timing with mmap
time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;" > /dev/null

# Process check
ps aux | grep sqlite
```

### Observations

- File sizes (`ls -lh`):
  - `sample.db`: `3.9M`
  - `sample.db-shm`: `33K`
  - `sample.db-wal`: `0B` (after checkpoint/close state)
- `PRAGMA page_size`: `4096` bytes
- `PRAGMA page_count`: `957`
- Initial `PRAGMA mmap_size`: `0`
- Query timing (`SELECT * FROM users`):
  - Without mmap (`mmap_size=0`): `real 0.02s`
  - With mmap (`mmap_size=268435456`): `real 0.02s`

Result: On this dataset and system state, mmap tuning did not show visible wall-time difference (both runs were very fast and likely cache-friendly).

---

## 2) PostgreSQL (PSQL) Setup and Exploration

### Setup

PostgreSQL version observed:

- `PostgreSQL 18.3 (Homebrew) ... 64-bit`

Sample table used: `users_task2` with 100,000 rows.

### Commands Used

```bash
# Version
psql -d postgres -c "SELECT version();"

# Create/prepare table and data
psql -d postgres -c "DROP TABLE IF EXISTS users_task2; CREATE TABLE users_task2(id BIGINT PRIMARY KEY, name TEXT, email TEXT); INSERT INTO users_task2 SELECT g, 'user_'||g, 'user_'||g||'@mail.com' FROM generate_series(1,100000) g; ANALYZE users_task2;"

# Page size (block size)
psql -d postgres -At -c "SHOW block_size;"

# Approx page count and tuple count
psql -d postgres -c "SELECT relpages, reltuples FROM pg_class WHERE relname='users_task2';"

# Query execution plan and execution time
psql -d postgres -c "EXPLAIN ANALYZE SELECT * FROM users_task2;"

# End-to-end wall time from client side
time psql -d postgres -c "SELECT * FROM users_task2;" > /dev/null
```

### Observations

- Block size (`SHOW block_size`): `8192` bytes
- `relpages`: `834`
- `reltuples`: `100000`
- `EXPLAIN ANALYZE` execution time: `~6.719 ms`
- Client wall time (`time psql ...`): `real 0.15s`

---

## 3) Comparison: SQLite3 vs PostgreSQL

| Metric | SQLite3 | PostgreSQL |
|---|---:|---:|
| Page size | 4096 bytes | 8192 bytes |
| Page count (sample table/db) | 957 (db-level from `PRAGMA`) | 834 (`relpages` for `users_task2`) |
| Query performance (`SELECT *`, 100k rows) | ~0.02s (CLI wall time) | ~6.7 ms server execution, ~0.15s client wall time |
| mmap impact | No visible change (`0.02s` vs `0.02s`) | No direct per-query mmap toggle like SQLite; memory behavior managed via PostgreSQL + OS caching |

### Analysis

- SQLite is lightweight and very fast for local single-file workloads.
- PostgreSQL provides detailed internal stats (`EXPLAIN ANALYZE`, buffers, planner info), useful for deeper performance tuning.
- Direct mmap tuning is an explicit SQLite lever (`PRAGMA mmap_size`), while PostgreSQL abstracts memory management differently.
- Wall time can include client overhead and output handling; engine execution time (`EXPLAIN ANALYZE`) gives a cleaner server-side view for PostgreSQL.

---

## 4) Conclusion

- Both systems performed well on a 100k-row sequential scan workload.
- SQLite showed excellent simplicity and low latency for local reads.
- PostgreSQL offered stronger observability and server-grade execution insights.
- For this experiment, mmap configuration in SQLite did not materially affect observed query runtime.

