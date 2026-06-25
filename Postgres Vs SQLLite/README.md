# scaler-Adv-DBMS

Hands-on comparison of **SQLite 3.53.1** and **PostgreSQL 15.15** on the same
two-table dataset (`students`, `courses`, 10 rows each), looking at storage
layout and query latency.

The full writeup with numbers and analysis is in [comparison.md](comparison.md).

## Layout

| File | What it is |
|---|---|
| [schema.sql](schema.sql) | DDL + 20 INSERTs, used by both engines |
| [queries.sql](queries.sql) | The two test queries (filter + join) |
| [sample.db](sample.db) | SQLite database produced from `schema.sql` |
| [comparison.md](comparison.md) | Storage architecture, query timings, mmap test, takeaways |
| `bench_sqlite_storage.txt` | `.dbinfo` / pragma output |
| `bench_sqlite_queries.txt` | 10× Q1 + 10× Q2 SQLite timings, plus `EXPLAIN QUERY PLAN` |
| `bench_sqlite_mmap.txt` | Same queries with `PRAGMA mmap_size = 256 MB` |
| `bench_pg_storage.txt` | Postgres block size, per-table file paths/sizes, db size |
| `bench_pg_queries.txt` | 10× Q1 + 10× Q2 Postgres timings, plus `EXPLAIN (ANALYZE, BUFFERS)` |
| `.tools/sqlite3.exe` | Bundled SQLite CLI (Windows x64) used for the SQLite half |

## Reproduce

```bash
# SQLite
./.tools/sqlite3.exe sample.db < schema.sql

# PostgreSQL (role 'postgres' / password 'postgres' on localhost)
PGPASSWORD=postgres psql -U postgres -d postgres -h localhost \
  -c "DROP DATABASE IF EXISTS advdbms; CREATE DATABASE advdbms;"
PGPASSWORD=postgres psql -U postgres -d advdbms -h localhost -f schema.sql
```

## TL;DR

- SQLite holds the whole database in **one 12 KB file**; PostgreSQL's
  `advdbms` directory carries **307 files / 7.6 MB** of catalogs before any
  user data.
- SQLite is ~10× faster wall-clock (~0.17 ms vs ~1.7 ms for the simple query),
  but Postgres' *execution* time per `EXPLAIN ANALYZE` is **0.134 ms** — same
  order. The gap is socket + scram auth + protocol + per-statement planning,
  not the engine.
- `PRAGMA mmap_size` made no measurable difference at 12 KB — exactly as
  SQLite's docs predict for cache-hot workloads.

