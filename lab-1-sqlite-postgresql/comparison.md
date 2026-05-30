# SQLite vs PostgreSQL

## What I set up

I created two identical tables (`students` and `courses`) with 10 rows each in both SQLite 3.43.2 and PostgreSQL 18.4 on macOS 15.7.7 (Apple Silicon). The same two queries were run on both engines:

**Q1 (JOIN):**
```sql
SELECT s.name, c.course_name, c.grade
FROM students s
JOIN courses c ON s.id = c.student_id;
```

**Q2 (COUNT):**
```sql
SELECT COUNT(*) FROM students WHERE dept = 'CS';
```

Everything lives in the `sqlite-postgresql/` folder. Run `./run_comparison.sh` to recreate the setup and timings on your machine.

## Storage layout

| Metric | SQLite | PostgreSQL |
|---|---|---|
| Page / block size | 4096 bytes (`PRAGMA page_size`) | 8192 bytes (`SHOW block_size`) |
| Page count | 3 for the whole database (`PRAGMA page_count`) | 1 per table after `ANALYZE` (`pg_class.relpages`) |
| On disk size | 12,288 bytes (single `sample.db` file) | 8192 bytes per table |
| Storage model | One file holds schema, tables, and indexes | Separate heap files per relation, plus WAL and catalog |
| Memory mapping | Off by default (`mmap_size = 0`) | Uses shared buffers and the OS page cache |

SQLite stores everything in a single file split into fixed 4 KB pages. With just two small tables, the whole database fits in 3 pages (12 KB on disk). PostgreSQL allocates at least one 8 KB block per table even when the rows themselves are tiny, so each table reports `relpages = 1` and `pg_relation_size` of 8192 bytes.

That difference is intentional. SQLite optimizes for a single app shipping one file. PostgreSQL splits storage per relation so it can handle concurrent access, vacuum, tablespaces, and replication without rewriting one monolithic file.

## Query times on SQLite

I toggled memory mapped I/O with `PRAGMA mmap_size`. Setting it to 0 forces ordinary `read()` calls. Setting it to 268435456 enables up to 256 MB of mmap.

| Query | mmap = 0 | mmap = 256 MB |
|---|---|---|
| Q1 JOIN | real 0.000 s | real 0.000 s |
| Q2 COUNT | real 0.000 s | real 0.000 s |

Both configurations reported zero wall clock time. That does not mean the queries are free; it means the timer resolution on macOS cannot distinguish sub millisecond runs. The entire 12 KB database sits in the OS page cache after the first access, so mmap never gets a chance to show its value here anyway. Mmap pays off when the working set is large enough that skipping syscalls and extra buffer copies actually matters.

## Query times on PostgreSQL

| Query | Time |
|---|---|
| Q1 JOIN | 1.357 ms |
| Q2 COUNT | 0.244 ms |
| Q2 COUNT (`EXPLAIN ANALYZE`) | Planning 0.014 ms + Execution 0.014 ms |

Running each query five times gave a rough range of 1.2 to 1.9 ms for the JOIN and 0.9 to 1.2 ms for the COUNT. Some of that spread is normal jitter on a laptop.

The `EXPLAIN ANALYZE` output for Q2 showed a sequential scan on `students` with a filter on `dept = 'CS'`, removing 5 of 10 rows. The page was already in memory (`Buffers: shared hit=1`). For a table this small, a seq scan is the right call; building an index would cost more than it saves.

## Side by side

| Query | SQLite (mmap off) | SQLite (mmap on) | PostgreSQL |
|---|---|---|---|
| Q1 JOIN | ~0 ms (below timer resolution) | ~0 ms | ~1.4 ms |
| Q2 COUNT | ~0 ms | ~0 ms | ~0.2 to 1.2 ms |

PostgreSQL is slower on paper, but the gap is almost entirely fixed overhead: parsing, planning, protocol, and MVCC bookkeeping. SQLite runs in process with no IPC and a lighter planner, so microsecond scale work finishes before the timer can measure it.

On this dataset, that overhead dominates Postgres's numbers. That is expected and not a fair fight for raw speed. It becomes a fair comparison only when you need what Postgres provides: concurrent writers, richer types, roles, replication, and a full SQL planner you can inspect.

## What stood out

1. **Page size is a design choice, not a bug.** SQLite defaults to 4 KB and lets you set it per database. PostgreSQL uses 8 KB blocks compiled into the server. Neither is wrong; they target different workloads.

2. **Postgres carries real per query cost even on tiny data.** My JOIN landed around 1.4 ms. SQLite's equivalent finished too fast to measure. That gap shrinks on larger, more complex queries where planning and execution actually do work.

3. **Mmap changed nothing at this scale.** The file is 12 KB. The OS caches it. Turning mmap on or off is invisible here.

4. **Storage shape tells you the product goal.** One SQLite file you can copy and ship. Postgres files you manage as part of a server. The layout matches how each engine expects to be used.

5. **Postgres shows its work.** `EXPLAIN ANALYZE` told me exactly what happened: seq scan, filter, one buffer hit. SQLite has `.explain` and query plans too, but the screenshot exercise made Postgres's planner output the clearest teaching moment.

For interactive sessions, open SQLite with `sqlite3 sample.db` (use `.timer on` for timings) and Postgres with `psql -d sampledb` (use `\timing on`).
