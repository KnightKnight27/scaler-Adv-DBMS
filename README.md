# SQLite vs PostgreSQL — Page Layout & Query Time Comparison

## Setup

Two identical tables (`students`, `courses`) with 10 rows each were created in both SQLite 3.51.0 and PostgreSQL 18.3 on macOS. The same two queries were run on both:

- **Q1 (JOIN):** `SELECT s.name, c.course_name, c.grade FROM students s JOIN courses c ON s.id = c.student_id;`
- **Q2 (COUNT):** `SELECT COUNT(*) FROM students WHERE dept='CS';`

## Storage layout

| Metric | SQLite | PostgreSQL |
|---|---|---|
| Page / block size | 4096 bytes (`PRAGMA page_size`) | 8192 bytes (`SHOW block_size`) |
| Page count | 3 (whole DB, `PRAGMA page_count`) | 1 per table (`pg_class.relpages`) |
| On-disk size | 12 KB (single `sample.db` file) | 8192 bytes per table file |
| Storage model | Single file holds all tables, indexes, schema | One file per table/relation, plus WAL and catalog files |
| Default mmap | Off (`mmap_size = 0`) | N/A — uses shared buffers + OS page cache |

SQLite packs the entire database into one file divided into fixed-size pages. With two small tables, only 3 pages are used (one for the schema, one per table approximately). PostgreSQL allocates one 8 KB block per table even when the data is much smaller, so each table reports `relpages = 1`.

## Query times — SQLite (with and without mmap)

`PRAGMA mmap_size = 0` disables memory-mapped I/O (forces `read()` syscalls). `PRAGMA mmap_size = 268435456` enables 256 MB of mmap.

| Query | mmap = 0 | mmap = 256 MB |
|---|---|---|
| Q1 JOIN | real 0.000 s (user 0.000119, sys 0.000121) | real 0.001 s (user 0.000134, sys 0.000131) |
| Q2 COUNT | real 0.001 s (user 0.000136, sys 0.000149) | real 0.000 s (user 0.000104, sys 0.000095) |

The differences here are **within measurement noise** — the dataset is only 12 KB, which fits entirely in the OS page cache after the first read, so mmap can't show its real benefit. Mmap matters when the working set is large enough that avoiding `read()` syscalls and an extra user-space buffer copy actually saves work. On a 10-row DB it doesn't.

## Query times — PostgreSQL

| Query | Time |
|---|---|
| Q1 JOIN | 1.181 ms |
| Q2 COUNT | 0.551 ms |
| Q2 COUNT (`EXPLAIN ANALYZE`) | Planning 0.109 ms + Execution 0.067 ms |

## Side-by-side query times

| Query | SQLite (mmap off) | SQLite (mmap on) | PostgreSQL |
|---|---|---|---|
| Q1 JOIN | ~0.0–0.001 s | ~0.001 s | 0.001181 s |
| Q2 COUNT | ~0.001 s | ~0.0 s | 0.000551 s |

## Observations

1. **Page size differs by design.** SQLite defaults to 4 KB, PostgreSQL to 8 KB. Postgres's block size is a compile-time constant; SQLite's is tunable per-database via `PRAGMA page_size`.
2. **Postgres has higher per-query overhead.** Even on a tiny dataset, Postgres queries take ~0.5–1.2 ms because of network/socket protocol, planner work, and MVCC bookkeeping. SQLite is in-process — no IPC, simpler planner — so simple queries clock in under a millisecond.
3. **Mmap had no measurable effect at this scale.** Expected: the file is 12 KB and the OS already caches it. Mmap's win shows up on larger-than-RAM-cold or heavy-random-read workloads.
4. **Storage shape reflects intent.** SQLite's one-file model targets embedded use (single app, single file to ship). Postgres splits storage per relation to support concurrent readers/writers, online vacuum, tablespaces, and replication.
5. **Postgres's planner is visible.** `EXPLAIN ANALYZE` showed a `Seq Scan` on `students` with a `Filter: (dept = 'CS')` — sensible for a 10-row table where any index would be slower than a scan.