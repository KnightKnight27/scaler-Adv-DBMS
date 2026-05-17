# SQLite vs PostgreSQL — Storage & Query Comparison

A hands-on comparison of **SQLite 3.53.1** and **PostgreSQL 15.15** running the same
two-table dataset (`students`, `courses`, 10 rows each). The goal is to see how
two databases with very different design philosophies actually behave on the
*same* tiny workload — both in how they store data on disk and in what it costs
to read it back.

> **Why this dataset is small on purpose.** 10 rows fits in cache on any modern
> machine, so the numbers below say nothing about what either engine does at
> scale. They *do* expose the fixed costs each engine pays per query — and
> those fixed costs are where the architectural differences leak through.

---

## 1. Setup

| | SQLite | PostgreSQL |
|---|---|---|
| Version | 3.53.1 (precompiled win-x64 tools) | 15.15, Visual C++ build 1944 |
| Page / block size | 4 KB | 8 KB |
| Storage layout | single file `sample.db` | per-relation files under `base/<dboid>/` |
| Schema | `students(id, name, age, dept)`, `courses(id, student_id, course_name, grade)` | identical |
| Rows | 10 + 10 | 10 + 10 |

Schema is in [schema.sql](schema.sql); the two test queries are in [queries.sql](queries.sql).
Raw benchmark logs are in [bench_sqlite_storage.txt](bench_sqlite_storage.txt),
[bench_sqlite_queries.txt](bench_sqlite_queries.txt),
[bench_sqlite_mmap.txt](bench_sqlite_mmap.txt),
[bench_pg_storage.txt](bench_pg_storage.txt),
[bench_pg_queries.txt](bench_pg_queries.txt).

---

## 2. Storage architecture

### SQLite — one file, three pages

```
sample.db                 12,288 bytes (3 pages × 4 KB)
  page 1   schema header + sqlite_master + students rows
  page 2   courses rows
  page 3   freelist / overflow
```

`.dbinfo` confirms: `database page size: 4096`, `database page count: 3`,
`number of tables: 2`, `number of indexes: 0`, schema size 239 bytes.
Everything — schema, both tables, and SQLite's own metadata — lives inside that
single 12 KB file. Move the file, you move the database.

### PostgreSQL — many files per database

```
C:/Program Files/PostgreSQL/15/data/base/32768/
  ├─ 32769         students main heap   (8,192 bytes — one 8 KB block)
  ├─ 32769_fsm     free-space map
  ├─ 32769_vm      visibility map
  ├─ 32776         courses main heap    (8,192 bytes — one 8 KB block)
  ├─ 32776_fsm
  ├─ 32776_vm
  ├─ ... 300+ system-catalog files (pg_class, pg_attribute, pg_proc, ...)
  └─ ...
```

- The `advdbms` database directory contains **307 files**.
- Each user table is **8 KB** of heap (one 8 KB block, the minimum), and Postgres
  reports **32 KB total** per table once you include the free-space map and
  visibility map fork files.
- `pg_database_size('advdbms')` is **7,597 KB** — almost entirely the system
  catalogs (`pg_class`, `pg_attribute`, `pg_proc`, etc.) that every database
  carries.

So, on identical 10-row data, SQLite needs **~12 KB**; PostgreSQL needs
**~7.6 MB** just to hold the catalogs that make it *PostgreSQL* (concurrent
access, MVCC, planner stats, replication slots, vacuum maps). That ~600× ratio
is fixed overhead, not per-row cost.

### Why each engine made that choice

| | SQLite | PostgreSQL |
|---|---|---|
| Concurrency | one writer at a time, file-level lock | per-row MVCC, many concurrent readers + writers |
| Crash recovery | rollback journal / WAL on the same file | separate WAL directory, fsync per checkpoint |
| Vacuum / bloat | occasional `VACUUM` rewrites the file | per-relation vacuum touches only that file's blocks |
| Replication | "copy the file" | dedicated slot + WAL streaming |
| Goal | embedded — ship one app, one file | client-server — many sessions, many tables, online ops |

The single-file model would be a liability for Postgres (you can't vacuum or
replicate just one table without touching everyone's pages). The 307-files
model would be absurd for SQLite (your phone's address book doesn't need a
free-space map fork).

---

## 3. Query performance

Two queries, each run **10 times** in each engine:

- **Q1** — single-table filter: `SELECT … FROM students WHERE dept='CS' ORDER BY id;`
- **Q2** — join: `SELECT s.name, s.dept, c.course_name, c.grade FROM students s JOIN courses c ON c.student_id = s.id ORDER BY s.id;`

### Headline numbers

| | SQLite (`.timer`) | PostgreSQL (psql `\timing`) | PostgreSQL (`EXPLAIN ANALYZE` exec time) |
|---|---:|---:|---:|
| Q1 avg | **0.168 ms** | 1.74 ms | **0.134 ms** |
| Q2 avg | **0.244 ms** | 2.81 ms | **0.135 ms** |

Range across 10 runs:
- SQLite Q1: 0.152 – 0.185 ms
- SQLite Q2: 0.183 – 0.371 ms
- PostgreSQL Q1 wall: 1.296 – 2.659 ms
- PostgreSQL Q2 wall: 1.777 – 4.868 ms

### What the gap actually is

Postgres looks ~10× slower at the wall clock, but `EXPLAIN (ANALYZE, BUFFERS)`
reports an **execution time of ~0.134 ms** — the same order of magnitude as
SQLite. Almost the entire wall-clock difference is **client/protocol
overhead**, not the engine:

- TCP connect to `localhost:5432` (still goes through the loopback stack)
- Auth handshake (scram-sha-256)
- Wire-protocol parse → planner → bind → execute → row description → row data → close
- `Planning Time: 0.9–1.1 ms` — Postgres re-plans the query each time we send it
  via `psql -c`; the planner itself is doing more work than the execution.

SQLite skips all of this: it's an in-process library call. There's no socket,
no auth, no separate planner pass for a freshly opened connection (the query
parses and runs in one go).

Interpretation: **for embedded, single-process workloads at this scale,
SQLite's "no protocol" path is the entire reason it wins.** It is not that the
PostgreSQL engine is slow.

### Query plans

SQLite (`EXPLAIN QUERY PLAN`):
```
Q1:   SCAN students                         -- 10-row table, no index needed
Q2:   SCAN c
      SEARCH s USING INTEGER PRIMARY KEY    -- rowid lookup is implicit on INTEGER PK
      USE TEMP B-TREE FOR ORDER BY
```

PostgreSQL (`EXPLAIN ANALYZE` highlights):
```
Q1:   Seq Scan on students  (rows=5)        Buffers: shared hit=1
      Sort Key: id                          Sort Method: quicksort  Memory: 25kB
Q2:   Hash Join                              Buffers: shared hit=2
      ->  Seq Scan on courses
      ->  Hash on Seq Scan students
      Sort Key: s.id                         Sort Method: quicksort  Memory: 25kB
```

Both engines pick the only sensible plan at this size — sequential scan + sort
for Q1, scan + hash join + sort for Q2. PostgreSQL's planner is *capable* of
much more (index scans, merge joins, parallel workers); here it correctly
decides the table is too small to bother.

---

## 4. The mmap experiment

Setting `PRAGMA mmap_size = 268435456;` (256 MB) on SQLite gave **no measurable
benefit**:

| Q1 avg | Q2 avg |
|---:|---:|
| 0.194 ms (mmap) vs 0.168 ms (default) | 0.339 ms (mmap) vs 0.244 ms (default) |

If anything, mmap was a hair *slower* — within noise, dominated by per-process
startup of the `sqlite3` CLI. This matches SQLite's own documentation:
memory-mapped I/O wins on **larger-than-RAM-cold or heavy-random-read**
workloads. A 12 KB database that fits in the OS page cache after the first
read is exactly the wrong shape for mmap to help.

---

## 5. Takeaways

1. **SQLite wins the speed test, but not because its engine is faster.** PostgreSQL's
   measured *execution* is sub-millisecond too; the 10× wall-clock difference is
   almost entirely socket + protocol + per-statement planning.
2. **The storage gap is real and structural.** PostgreSQL's per-database
   directory carries ~7.6 MB of catalog before you write a single user row. That
   is the price of features SQLite intentionally doesn't offer: row-level MVCC,
   concurrent writers, online vacuum, streaming replication.
3. **Each engine is right for a different workload.** SQLite was the right call
   for a 12 KB embedded database the moment we shipped it as one file. Postgres
   would be the right call the moment a second writer joined, or the moment
   "back up" stopped meaning "copy the file".
4. **Tiny benchmarks expose fixed costs, not throughput.** Don't draw
   throughput conclusions from this — re-run the same harness on, say, a
   10-million-row dataset and the planner overhead becomes a rounding error
   while index access patterns and MVCC vs single-writer locking start to dominate.

---

## 6. How to reproduce

```bash
# SQLite
./.tools/sqlite3.exe sample.db < schema.sql

# PostgreSQL (assumes role 'postgres' with password 'postgres' on localhost)
PGPASSWORD=postgres psql -U postgres -d postgres -h localhost \
  -c "DROP DATABASE IF EXISTS advdbms; CREATE DATABASE advdbms;"
PGPASSWORD=postgres psql -U postgres -d advdbms -h localhost -f schema.sql
```

The exact command sequences used to gather the numbers above are in the
`bench_*.txt` log files.
