# SQLite3 vs PostgreSQL — Storage & Query Performance Lab

**Submission**

| Field | Value |
|---|---|
| Role Number | 24BCS10151 |
| Name | Shaurya Verma |
| Date | 2026-05-09 |
| Platform | macOS (Darwin 25.3.0, aarch64) |
| SQLite version | 3.51.0 |
| PostgreSQL version | 16.13 (Homebrew) |

---

## 1. Sample Dataset

Both engines were loaded with the **same logical dataset** so the comparison is apples-to-apples.

| Table | Rows | Columns of note |
|---|---:|---|
| `users` | 100,000 | `id`, `name`, `email`, `age`, `city`, `bio` (~120-char text), `created_at` |
| `orders` | 200,000 | `id`, `user_id` (FK), `product`, `amount`, `order_date` |
| Indexes | — | `idx_users_city`, `idx_orders_user` |

Seed scripts: [seed_sqlite.sql](seed_sqlite.sql) and [seed_pg.sql](seed_pg.sql).

---

## 2. SQLite3 Exploration

### 2.1 File size on disk

```bash
$ ls -lh sample.db
-rw-r--r--@ 1 shaurya  staff    31M May  9 22:09 sample.db
```

The entire database (both tables + indexes) is a **single 31 MB file**.

### 2.2 PRAGMA inspection

```bash
$ sqlite3 sample.db "PRAGMA page_size; PRAGMA page_count; PRAGMA mmap_size; \
                     PRAGMA cache_size; PRAGMA journal_mode; PRAGMA encoding;"
```

| PRAGMA | Value | Meaning |
|---|---:|---|
| `page_size` | **4096** bytes | Size of each B-tree page on disk |
| `page_count` | **7969** | Total pages → 7969 × 4096 ≈ 32.6 MB on disk |
| `mmap_size` | **0** | Memory-mapping is **OFF by default** |
| `cache_size` | 2000 pages | Page cache (≈ 8 MB) |
| `journal_mode` | `delete` | Rollback journal (default) |
| `encoding` | UTF-8 | — |

`page_size × page_count` matches the on-disk file size, confirming the file *is* literally a sequence of 4 KB pages.

### 2.3 mmap experiment

`mmap_size` controls how many bytes of the database SQLite memory-maps instead of `read()`-ing through its own page cache. With `mmap_size=0`, every page comes through the OS via syscalls; with a non-zero size, pages are served directly from the kernel page cache as memory loads.

```sql
PRAGMA mmap_size = 0;            -- disabled
PRAGMA mmap_size = 268435456;    -- 256 MB
```

### 2.4 SQLite query timings

Run via `sqlite3 .timer on`. Same DB, same hardware, only `mmap_size` changed.

| Query | mmap = 0 | mmap = 256 MB | Speedup |
|---|---:|---:|---:|
| `SELECT COUNT(*) FROM users;` | 0.002 s | 0.001 s | ~2× |
| `SELECT COUNT(*), SUM(amount) FROM orders WHERE user_id BETWEEN 1000 AND 90000;` | **0.054 s** | **0.029 s** | **~1.9×** |
| `SELECT u.city, COUNT(o.id), SUM(o.amount) FROM users u JOIN orders o ON o.user_id=u.id GROUP BY u.city;` | 0.064 s | 0.061 s | marginal |

**Observation:** mmap helps most when the query touches a large contiguous portion of the file (the range scan over `orders`). For a hash-join that's already CPU-bound (GROUP BY) the win shrinks because the bottleneck is no longer I/O.

### 2.5 `ps aux | grep sqlite`

```
$ ps aux | grep -i sqlite | grep -v grep
(no resident process)
```

SQLite is an **embedded library** — there is no server. A `sqlite3` process exists only for the duration of a CLI command; once it exits, nothing remains in memory.

---

## 3. PostgreSQL Exploration

### 3.1 On-disk layout

Postgres stores each table/index as **one or more 1 GB segment files** under `$PGDATA/base/<dbOID>/`, keyed by relation OID — not a single file like SQLite.

```bash
$ du -sh /opt/homebrew/var/postgresql@16
171M  /opt/homebrew/var/postgresql@16     # whole cluster (incl. catalogs, WAL)

postgres=# SELECT pg_size_pretty(pg_database_size('labdb'));
 52 MB                                    # just our database
```

| Object | Size | Pages (8 KB each) |
|---|---:|---:|
| `users` table | 22 MB | 2842 |
| `orders` table | 11 MB | 1471 |
| Whole `labdb` | 52 MB | — |

### 3.2 Page size & related parameters

```sql
SHOW block_size;             -- 8192   (compile-time constant)
SHOW shared_buffers;         -- 128MB  (Postgres's own buffer pool)
SHOW effective_cache_size;   -- 4GB    (planner hint about OS cache)
SHOW work_mem;               -- 4MB
```

| Parameter | Value | Notes |
|---|---:|---|
| `block_size` (page size) | **8192 B** | Fixed at compile time — cannot be changed via SQL |
| Page count, `users` | **2842** | `pg_relation_size('users') / 8192` |
| Page count, `orders` | **1471** | — |

### 3.3 PostgreSQL query timings

Run via `psql \timing on`.

| Query | Time | Notes |
|---|---:|---|
| `SELECT COUNT(*) FROM users;` | 21.7 ms | Seq scan, all in shared_buffers |
| `SELECT COUNT(*), SUM(amount) FROM orders WHERE user_id BETWEEN 1000 AND 90000;` | 20.8 ms | Index considered, seq scan chosen (89% selectivity) |
| `… JOIN … GROUP BY u.city;` | 36.1 ms | **Parallel Hash Join** (2 workers) |

`EXPLAIN (ANALYZE, BUFFERS)` confirmed `shared hit=2842` for the users scan — every page served from the buffer pool, zero disk reads.

### 3.4 `ps aux | grep postgres`

```
postgres: logical replication launcher
postgres: autovacuum launcher
postgres: walwriter
postgres: background writer
postgres: checkpointer
/opt/homebrew/opt/postgresql@16/bin/postgres -D ...   (postmaster)
```

Postgres is a **client–server RDBMS** — a postmaster process plus permanent helpers (WAL writer, checkpointer, autovacuum, …). One backend process is forked per client connection.

### 3.5 Note on mmap in Postgres

Postgres does **not** expose an `mmap_size` knob. It maintains its own page cache (`shared_buffers`) and reads via `pread()`. You cannot toggle mmap on/off the way you can in SQLite. The closest analogue is sizing `shared_buffers` (Postgres's cache) and `effective_cache_size` (planner's awareness of OS cache).

---

## 4. Comparison

### 4.1 Page size & count

| | SQLite | PostgreSQL |
|---|---|---|
| Default page size | **4 KB** (1 KB–64 KB configurable per DB at creation) | **8 KB** (compile-time constant) |
| Page count, our DB | 7969 (whole DB in one file) | users=2842, orders=1471 (per-relation) |
| Storage layout | Single file: `sample.db` | One file per relation, split into 1 GB segments under `base/<oid>/` |
| Inspect via | `PRAGMA page_size; PRAGMA page_count;` | `SHOW block_size; pg_relation_size()/8192` |

### 4.2 Query performance (same dataset, same machine)

| Query | SQLite (mmap=0) | SQLite (mmap=256MB) | PostgreSQL |
|---|---:|---:|---:|
| `COUNT(*) FROM users` | 0.002 s | 0.001 s | 0.022 s |
| Range aggregate on `orders` | 0.054 s | **0.029 s** | 0.021 s |
| Join + GROUP BY 5 cities | 0.064 s | 0.061 s | **0.036 s** |

Reading the table:

- **Trivial counts** are fastest in SQLite — no IPC, no parser daemon, no parallel-worker setup overhead. SQLite is an in-process function call; psql spawns a backend round-trip.
- **Range scans** narrow the gap; mmap roughly halves SQLite's time on the I/O-bound query.
- **Parallel join** is where Postgres wins — it dispatches a Parallel Hash Join across 2 workers (visible in `EXPLAIN ANALYZE`), something SQLite single-threaded execution cannot do.

### 4.3 mmap impact

| | SQLite | PostgreSQL |
|---|---|---|
| Has user-tunable mmap? | **Yes** — `PRAGMA mmap_size=N;` | No equivalent knob |
| Default | Off (`mmap_size=0`) | n/a (uses `shared_buffers` + OS page cache) |
| Measured effect (range query) | 0.054 s → 0.029 s (**~46% reduction**) | n/a |
| When it helps | Read-heavy workloads on a DB that fits or partly fits in RAM; reduces `read()` syscall + memcpy overhead | Postgres handles this internally via its buffer pool |

### 4.4 Architectural take-aways

| | SQLite | PostgreSQL |
|---|---|---|
| Process model | Embedded library, no daemon (`ps` shows nothing) | Multi-process server (postmaster + helpers + per-connection backends) |
| Concurrency | One writer at a time, file-level locks | MVCC, many concurrent writers |
| Parallel query | No | Yes (saw 2 workers used) |
| Configurable page size | Yes, at DB creation | No, compile-time |
| Best for | Embedded apps, single-user tools, tests, edge / mobile | Multi-user services, large datasets, parallel analytical queries |

---

## 5. Commands Used (cheat-sheet)

### SQLite

```bash
sqlite3 sample.db < seed_sqlite.sql
ls -lh sample.db
sqlite3 sample.db "PRAGMA page_size; PRAGMA page_count; PRAGMA mmap_size;"

# Timing without mmap
sqlite3 sample.db <<'EOF'
.timer on
PRAGMA mmap_size=0;
SELECT COUNT(*), SUM(amount) FROM orders WHERE user_id BETWEEN 1000 AND 90000;
EOF

# Timing with mmap
sqlite3 sample.db <<'EOF'
.timer on
PRAGMA mmap_size=268435456;   -- 256 MB
SELECT COUNT(*), SUM(amount) FROM orders WHERE user_id BETWEEN 1000 AND 90000;
EOF

ps aux | grep sqlite
```

### PostgreSQL

```bash
brew install postgresql@16
brew services start postgresql@16

createdb labdb
psql -d labdb -f seed_pg.sql

psql -d labdb -c "SHOW block_size;"
psql -d labdb -c "SELECT pg_size_pretty(pg_database_size('labdb'));"
psql -d labdb -c "SELECT pg_relation_size('users')/8192 AS pages;"

psql -d labdb <<'EOF'
\timing on
SELECT COUNT(*) FROM users;
EXPLAIN (ANALYZE, BUFFERS) SELECT u.city, COUNT(o.id), SUM(o.amount)
  FROM users u JOIN orders o ON o.user_id=u.id GROUP BY u.city;
EOF

ps aux | grep postgres
```

---

## 6. Conclusions

1. **Page size differs by design**: SQLite uses 4 KB, Postgres uses 8 KB. SQLite's page size is configurable per database at creation; Postgres's is compile-time.
2. **Storage layout differs fundamentally**: SQLite is one file containing every table and index; Postgres uses one file per relation under `base/<dbOID>/`, with 1 GB segmentation for large relations.
3. **mmap is a real lever in SQLite** — turning it on cut the range-scan query time roughly in half (54 ms → 29 ms). Postgres has no equivalent toggle because it manages its own buffer pool (`shared_buffers`).
4. **For tiny single-statement queries**, SQLite is faster purely because there's no client/server round-trip. **For complex analytical queries**, Postgres's parallel executor pulls ahead.
5. **The right tool depends on the workload**, not which is "better" in absolute terms: embedded/single-user → SQLite; multi-user/concurrent/analytical → PostgreSQL.
