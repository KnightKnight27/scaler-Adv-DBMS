# Lab 2 — SQLite3 vs PostgreSQL

**Roll Number:** 24BCS10115
**Name:** Gauri Shukla
**Course:** Advanced DBMS

---

## 1. Goal

Explore on-disk storage layout (page size, page count) and query execution
behavior of two very different database engines — **SQLite3** (an embedded,
single-file database) and **PostgreSQL** (a client–server RDBMS) — and compare
how each handles I/O via memory mapping vs the buffer cache.

---

## 2. Environment

| Item | Value |
|---|---|
| Machine | MacBook (Apple Silicon), macOS Darwin 24.6 |
| SQLite version | 3.43.2 (2023-10-10) |
| PostgreSQL version | 16.13 (Homebrew) on aarch64-apple-darwin24.6.0 |
| Sample dataset (SQLite) | Chinook (public sample DB, 984 KB) |
| Sample dataset (PostgreSQL) | `users` table, 100,000 rows generated via `generate_series` |

> Reproducibility: every command run is in [`sqlite_experiments.sh`](./sqlite_experiments.sh)
> and [`psql_experiments.sh`](./psql_experiments.sh).

---

## 3. SQLite3 — Observations

### 3.1 File on disk
```
$ ls -lh chinook.db
-rw-r--r--@ 1 gaurishukla  staff   984K May  9 20:27 chinook.db

$ file chinook.db
chinook.db: SQLite 3.x database, last written using SQLite version 3045001,
            file counter 46, database pages 246, cookie 0x16, schema 4,
            UTF-8, version-valid-for 46
```

### 3.2 PRAGMA values
```sql
PRAGMA page_size;       -- 4096
PRAGMA page_count;      -- 246
PRAGMA freelist_count;  -- 0
PRAGMA encoding;        -- UTF-8
PRAGMA journal_mode;    -- delete  (rollback journal, default)
PRAGMA mmap_size;       -- 0       (mmap disabled by default)
```

**Sanity check:** `page_size × page_count` ≈ on-disk file size.
`4096 × 246 = 1,007,616 bytes = 984 KB` — matches the `ls -lh` output exactly.

### 3.3 Query timings

**`SELECT * FROM Track`** (3,503 rows):

| Run | `mmap_size` | wall time (`real`) | rows returned |
|---|---|---|---|
| No mmap | `0` | **0.010 s** | 3503 |
| With mmap | `268435456` (256 MB) | **0.007 s** | 3503 |

**3-table join** (`Track ⨝ Album ⨝ Artist`, ~3,503 rows):

| Run | `mmap_size` | wall time (`real`) |
|---|---|---|
| No mmap | `0` | **0.005 s** |
| With mmap | `256 MB` | **0.005 s** |

### 3.4 What I observed about `mmap_size`
- On the full `SELECT * FROM Track` scan, enabling mmap (256 MB) reduced the
  wall time from **10 ms → 7 ms** — about a **30 % reduction**, consistent with
  removing one buffer copy (kernel page cache → SQLite page buffer) per page.
- On the 3-way join the absolute wall time is already ~5 ms, dominated by
  query planning, formatting and process startup rather than I/O. The mmap
  effect was below the measurement noise floor at this dataset size.
- On a ~1 MB database the OS page cache trivially holds the whole file after
  the first read, so the mmap win is small in absolute terms. The relative
  speedup would grow on a larger DB where each page genuinely costs an I/O.

### 3.5 `ps aux` while a query ran
```
USER               PID  %CPU %MEM      VSZ    RSS   TT  STAT STARTED      TIME COMMAND
(no sqlite3 row captured — query finished in <1 s, before ps snapshot)
```
The Track-vs-Track Cartesian-product query finished faster than the 1-second
sleep used to take the snapshot, so `ps` captured the system after the
process exited. With a larger query (or a longer sleep) the row would show
the mapped region in `VSZ` jumping by roughly the configured `mmap_size`.

### 3.6 EXPLAIN QUERY PLAN
```
QUERY PLAN
|--SCAN t
|--SEARCH al USING INTEGER PRIMARY KEY (rowid=?)
`--SEARCH ar USING INTEGER PRIMARY KEY (rowid=?)
```
SQLite does a full scan of `Track` and then a rowid-based lookup into `Album`
and `Artist` for each row — exactly what you'd expect when joining on the
primary keys.

---

## 4. PostgreSQL — Observations

### 4.1 Server config
```
SHOW block_size;            -- 8192        (8 KB — fixed at compile time)
SHOW shared_buffers;        -- 128MB       (default Homebrew config)
SHOW effective_cache_size;  -- 4GB         (planner's view of OS+PG cache)
```

### 4.2 Table layout (`users`, 100k rows)
```sql
SELECT relpages, reltuples,
       pg_size_pretty(pg_relation_size('users'))      AS heap,
       pg_size_pretty(pg_total_relation_size('users')) AS total
FROM pg_class WHERE relname='users';
```
| relpages (page_count) | reltuples (rows) | heap size | total size (incl. indexes) |
|---|---|---|---|
| 1022 | 100000 | 8176 kB | 10 MB |

**Sanity check:** `block_size × relpages` ≈ heap size.
`8192 × 1022 = 8,372,224 bytes = 8176 KB` — matches exactly.

### 4.3 Query timing — `SELECT * FROM users`

| Cache state | wall time | rows |
|---|---|---|
| **Cold** (after `brew services restart postgresql@16`) | **38.214 ms** | 100000 |
| **Warm** (second run, same session) | **34.926 ms** | 100000 |

The cold–warm gap is small (~3 ms) because restarting Postgres only flushes
its own `shared_buffers` — the **OS page cache** still held the data, so the
"cold" run wasn't actually cold at the kernel level. To produce a truly cold
run you'd need to also drop the OS page cache (e.g. `sudo purge` on macOS),
which the script intentionally avoids since it requires sudo.

### 4.4 `EXPLAIN (ANALYZE, BUFFERS)` on a heavier query
```
SELECT age, COUNT(*) AS c FROM users GROUP BY age ORDER BY c DESC LIMIT 10;

 Limit  (cost=2524.56..2524.59 rows=10) (actual time=22.510..22.512 rows=10)
   Buffers: shared hit=1025
   ->  Sort (actual time=22.502..22.503 rows=10)
         Sort Key: (count(*)) DESC
         Sort Method: top-N heapsort  Memory: 25kB
         ->  HashAggregate (actual time=22.376..22.384 rows=81)
               Group Key: age
               Buffers: shared hit=1022
               ->  Seq Scan on users (actual time=0.038..8.664 rows=100000)
                     Buffers: shared hit=1022
 Planning Time: 0.713 ms
 Execution Time: 22.761 ms
```
Key takeaways:
- **`Seq Scan`** — no index on `age`, so a full sequential scan was the right
  plan for an aggregate over the whole table.
- **`Buffers: shared hit=1022`** — every one of the 1,022 heap pages was
  served from `shared_buffers` (no `read=`), confirming the table was fully
  cached after the earlier `SELECT *`.
- **`top-N heapsort`** — Postgres recognized the `ORDER BY ... LIMIT 10` and
  used a bounded heap instead of sorting all 81 groups.

### 4.5 `ps aux` during a query
```
USER         PID   %CPU  COMMAND
gaurishukla  70500  97.8  postgres: gaurishukla adbms_lab2 [local] SELECT
gaurishukla  70486   0.0  postgres: logical replication launcher
gaurishukla  70485   0.0  postgres: autovacuum launcher
gaurishukla  70484   0.0  postgres: walwriter
gaurishukla  70482   0.0  postgres: background writer
gaurishukla  70481   0.0  postgres: checkpointer
gaurishukla  70479   0.0  /opt/homebrew/opt/postgresql@16/bin/postgres -D ...
```
Unlike SQLite (one process), Postgres runs a **postmaster** + several
background workers (checkpointer, walwriter, autovacuum, etc.) **plus** a
dedicated backend per client connection. The 97.8 % CPU row is my actual
query backend — the others are always-on infrastructure.

### 4.6 Storage after `DELETE` and `VACUUM`
```sql
DELETE FROM users WHERE id % 4 = 0;   -- 25,000 rows removed
```

| Stage | relpages | reltuples | heap size |
|---|---|---|---|
| Before delete | 1022 | 100000 | 8176 kB |
| After delete (no vacuum) | 1022 | 100000 | 8176 kB |
| After `VACUUM users` | 1022 | 75000 | 8176 kB |

Two things to notice:
1. **`reltuples` didn't change** until `VACUUM` ran — Postgres updates that
   stat lazily, so the planner can be working off slightly stale numbers.
2. **`relpages` and on-disk size didn't shrink** after `VACUUM`. Plain
   `VACUUM` only marks dead-tuple space inside pages as reusable; it doesn't
   return pages to the OS. Only `VACUUM FULL` (which rewrites the whole
   table) actually shrinks the file.

The verbose vacuum output also confirmed it freed `25000 dead item
identifiers` and rewrote 276 index pages — useful internal mechanics that
aren't visible to the application.

---

## 5. Comparison: SQLite3 vs PostgreSQL

| Dimension | SQLite3 | PostgreSQL |
|---|---|---|
| **Architecture** | Embedded library; one process, one file | Client–server, multi-process (postmaster + per-connection backends + bg workers) |
| **Page size** | Configurable per DB (`PRAGMA page_size`); default **4096 B**; can be 512 – 65536 | Fixed at **compile time** (`SHOW block_size`); default **8192 B** |
| **Where stored** | Single `.db` file (`chinook.db` was 984 KB) | `$PGDATA` directory; one file per ~1 GB chunk per relation |
| **Page count** | `PRAGMA page_count` → **246** for chinook | `pg_class.relpages` → **1022** for `users` |
| **Buffer/cache strategy** | OS page cache + optional `mmap` | Dedicated `shared_buffers` (128 MB default) + OS page cache (no mmap) |
| **mmap support** | Yes — `PRAGMA mmap_size` controls window size | **No** — Postgres deliberately doesn't mmap the heap |
| **Effect of mmap (observed)** | `SELECT * FROM Track`: 10 ms → 7 ms with `mmap_size=256MB` (~30 % faster). Heavier join was below noise floor at this DB size. | N/A — closest analogue is shared-buffer warmup: cold 38.2 ms → warm 34.9 ms (small because OS cache wasn't actually flushed) |
| **Query timing tools** | `.timer on`, shell `time` | `\timing on`, `EXPLAIN (ANALYZE, BUFFERS)` |
| **Concurrency** | Whole-DB write lock (single writer, multiple readers) | MVCC; many concurrent writers with row-level locking |
| **Process footprint (`ps aux`)** | Same process as the app — no daemon, no helper processes | Postmaster + checkpointer + bg writer + walwriter + autovacuum + logical-rep + per-connection backend (visible above) |
| **Storage reclamation after delete** | `VACUUM` rebuilds the file; freelist tracks reusable pages | Plain `VACUUM` marks space reusable in-place but doesn't shrink the file; `VACUUM FULL` rewrites |
| **Best for** | Embedded/local apps, mobile, CLI tools, single-user analytics | Multi-user web apps, concurrent workloads, large datasets, data integrity guarantees |

### 5.1 Where the page-size difference matters
SQLite's smaller default page (4 KB vs Postgres's 8 KB) means **more pages
for the same data** — the 984 KB Chinook DB needs 246 pages, while a
PostgreSQL table of similar size would need ~123 pages of 8 KB each. Smaller
pages reduce write amplification for tiny updates (less data dirtied per
row change), while larger pages amortize per-page overhead and pair better
with sequential scans, which dominate analytical workloads. Both choices are
defensible — they target different deployment shapes.

### 5.2 Where the mmap difference matters
- **SQLite's `mmap`** removes one copy (kernel buffer → SQLite page buffer)
  per page read, which on a single-file embedded DB is a clear win for
  read-heavy workloads. The **30 % speedup** I measured on the full-table
  scan is the typical shape of that benefit.
- **PostgreSQL avoids mmap** because `shared_buffers` lets it manage
  eviction, dirty-page write-back, and crash recovery (WAL) deterministically.
  mmap'ed writes can be flushed by the kernel at arbitrary times, which would
  break Postgres's durability contract: WAL must reach disk **before** the
  data page does. With its own buffer pool Postgres controls that ordering;
  with mmap the OS controls it.

### 5.3 What "mmap impact" looks like in practice (this lab)
- **SQLite:** `SELECT * FROM Track` went from **10 ms → 7 ms** with
  `mmap_size = 256 MB` (-30 %). The 3-way join didn't change measurably at
  this dataset size.
- **PostgreSQL:** No mmap; warm-cache run was **~9 % faster** than the
  cold-cache run (34.9 ms vs 38.2 ms). The gap is small only because
  restarting Postgres flushes `shared_buffers` but not the OS page cache —
  the role of `shared_buffers` in absolute terms is bigger than these
  numbers suggest.

---

## 6. Commands used

All raw commands are inside [`sqlite_experiments.sh`](./sqlite_experiments.sh)
and [`psql_experiments.sh`](./psql_experiments.sh). Highlights:

**SQLite:**
```bash
sqlite3 chinook.db "PRAGMA page_size; PRAGMA page_count; PRAGMA mmap_size;"
time sqlite3 chinook.db "PRAGMA mmap_size=0;        SELECT * FROM Track;" > /dev/null
time sqlite3 chinook.db "PRAGMA mmap_size=268435456; SELECT * FROM Track;" > /dev/null
ps aux | grep sqlite3
```

**PostgreSQL:**
```bash
psql -d adbms_lab2 -c "SHOW block_size;"
psql -d adbms_lab2 -c "SELECT relpages, reltuples FROM pg_class WHERE relname='users';"
psql -d adbms_lab2 -c "\timing on" -c "SELECT * FROM users;"
psql -d adbms_lab2 -c "EXPLAIN (ANALYZE, BUFFERS) SELECT age, COUNT(*) FROM users GROUP BY age ORDER BY 2 DESC LIMIT 10;"
ps aux | grep postgres
```

---

## 7. Conclusion

- Both engines store data as **fixed-size pages**, but only SQLite lets you
  change the page size per database and lets you opt into `mmap` at runtime.
- **SQLite + `mmap`** gave a measurable ~30 % speedup on a full-table scan
  by skipping one buffer copy per page.
- **PostgreSQL** trades the simplicity of mmap for `shared_buffers` + WAL,
  which is what makes durability and concurrent writes work correctly.
- **Operational footprint** is the most visible day-to-day difference:
  SQLite is a single library call; Postgres is a small fleet of cooperating
  processes you can watch in `ps aux`.
- For a single-process, read-heavy workload SQLite + `mmap` is hard to beat
  in raw speed; for anything multi-user, durable, or large-scale,
  PostgreSQL's buffer-pool architecture is the right tool.

---

*Submitted as part of Advanced DBMS — Lab 2.*
