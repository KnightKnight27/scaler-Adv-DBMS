# SQLite3 vs PostgreSQL — Storage & Query Lab Report

**Author:** Pratham Jain  
**Date:** 2026-05-07
**Environment:** Windows 10 (build 26220), PowerShell 5.1
**Versions:** SQLite 3.44.2 · PostgreSQL 16.13 (compiled with VC++ 1944, 64-bit)
**Sample dataset:** identical `users` table in both engines — 200,000 rows, 6 columns (`id`, `name`, `email`, `age`, `city`, `bio`), with secondary indexes on `city` and `age`. The `bio` column is a fixed ~140-byte string per row to inflate row size for measurable I/O.

> All commands and timings shown below were captured **on this machine**. Lab artefacts (SQL scripts, `.db` files, raw output) live in the `lab/` folder.

---

## 1. Setup

### 1.1 Sample data — identical schema for both engines

```sql
CREATE TABLE users (
    id    INTEGER PRIMARY KEY,
    name  TEXT NOT NULL,
    email TEXT NOT NULL,
    age   INTEGER,
    city  TEXT,
    bio   TEXT
);
-- 200,000 rows generated via recursive CTE / generate_series
CREATE INDEX idx_users_city ON users(city);
CREATE INDEX idx_users_age  ON users(age);
```



### 1.2 SQLite installation check

```powershell
PS> sqlite3 -version
3.44.2 2023-11-24 11:41:44 ebead0e7230cd33bcec9f95d2183069565b9e709bf745c9b5db65cc0cbf9alt1 (64-bit)
```

### 1.3 PostgreSQL installation (winget, silent)

```powershell
PS> winget install -e --id PostgreSQL.PostgreSQL.16 --source winget `
        --accept-source-agreements --accept-package-agreements --silent
...
Successfully installed
PS> & "C:\Program Files\PostgreSQL\16\bin\psql.exe" --version
psql (PostgreSQL) 16.13
PS> Get-Service postgresql-x64-16
Name              Status   StartType
----              ------   ---------
postgresql-x64-16 Running  Automatic
```

---

## 2. SQLite3 Exploration

### 2.1 File size on disk (`ls -lh` equivalent)

PowerShell does not have `ls -lh`; the equivalent is `Get-ChildItem` with a computed size column.

```powershell
PS lab> Get-ChildItem sample*.db |
        Select-Object Name, Length, @{N='SizeMB';E={[math]::Round($_.Length/1MB,2)}}
```


| File            | Bytes      | Size (MB) |
| --------------- | ---------- | --------- |
| `sample.db`     | 50,700,288 | **48.35** |
| `sample_8k.db`  | 49,463,296 | 47.17     |
| `sample_16k.db` | 49,201,152 | 46.92     |


`sample_8k.db` and `sample_16k.db` are copies built from `sample.db` with `VACUUM INTO 'name.db';` followed by `PRAGMA page_size = N; VACUUM;` to rewrite the file with a different page size — used in §2.3.

### 2.2 PRAGMA — page_size, page_count, mmap_size

```powershell
PS lab> sqlite3 sample.db "PRAGMA page_size; PRAGMA page_count; PRAGMA freelist_count;
                           PRAGMA cache_size; PRAGMA mmap_size; PRAGMA journal_mode;
                           PRAGMA synchronous; PRAGMA encoding;"
```

Output:

```
4096        -- page_size       (bytes per page)
12378       -- page_count      (4096 * 12378 = 50,700,288  ✓ matches file size)
0           -- freelist_count
-2000       -- cache_size      (negative => -2000 KiB = 2 MB cache)
0           -- mmap_size       (DEFAULT — memory-mapping is OFF)
delete      -- journal_mode    (rollback journal, not WAL)
2           -- synchronous     (FULL)
UTF-8       -- encoding
```

**Key observation:** SQLite ships with `mmap_size = 0`. By default it does **not** memory-map the database file — every page read is a buffered `read()` syscall that copies the page from kernel cache into SQLite's user-space cache.

### 2.3 Page-size variants (4 KB / 8 KB / 16 KB)

The same data laid out with different page sizes:


| Page size | Page count | File size | Bytes / row (incl. overhead) |
| --------- | ---------- | --------- | ---------------------------- |
| **4 KB**  | 12,378     | 48.35 MB  | 253                          |
| **8 KB**  | 6,038      | 47.17 MB  | 247                          |
| **16 KB** | 3,003      | 46.92 MB  | 246                          |


Larger pages → fewer pages → marginally smaller file (less per-page header overhead, denser B-tree fan-out). The savings are small here because the bio column dominates row size.

### 2.4 mmap_size experiment — query timing with mmap OFF vs ON



```sql
.timer ON
PRAGMA mmap_size = 0;            -- or 268435456 for 256 MB
SELECT SUM(length(bio)) FROM users;   -- forces full-table scan, 1-row result
SELECT SUM(length(bio)) FROM users;   -- run 2 (warm cache)
SELECT SUM(length(bio)) FROM users;   -- run 3
SELECT city, COUNT(*), AVG(age) FROM users GROUP BY city ORDER BY city;
SELECT COUNT(*) FROM users WHERE age = 25;
```

Two trials were run alternating order to mitigate OS-cache priming bias. Average wall-clock times in milliseconds:


| Query                                   | mmap = 0 (OFF) | mmap = 256 MB | Speedup |
| --------------------------------------- | -------------- | ------------- | ------- |
| Full scan `SUM(length(bio))` (avg of 6) | **148**        | **107**       | ~1.4×   |
| `GROUP BY city, COUNT(*), AVG(age)`     | **665**        | **220**       | ~3.0×   |
| Index probe `WHERE age = 25`            | 1              | 1             | ≈ 1×    |


Raw output excerpt (`Run Time: real X.XXX user X.XXX sys X.XXX`):

```
=== TRIAL 1: NO MMAP ===
PRAGMA mmap_size = 0; PRAGMA mmap_size; --> 0
SELECT SUM(length(bio)) FROM users;     32288895    Run Time: real 0.132
SELECT SUM(length(bio)) FROM users;     32288895    Run Time: real 0.151
SELECT SUM(length(bio)) FROM users;     32288895    Run Time: real 0.179
GROUP BY city,COUNT(*),AVG(age)         (5 rows)    Run Time: real 0.673
WHERE age = 25                          3334        Run Time: real 0.001

=== TRIAL 1: WITH MMAP (256 MB) ===
PRAGMA mmap_size = 268435456;           --> 268435456
SELECT SUM(length(bio)) FROM users;     32288895    Run Time: real 0.120
SELECT SUM(length(bio)) FROM users;     32288895    Run Time: real 0.091
SELECT SUM(length(bio)) FROM users;     32288895    Run Time: real 0.096
GROUP BY city,COUNT(*),AVG(age)         (5 rows)    Run Time: real 0.214
WHERE age = 25                          3334        Run Time: real 0.001
```

**Why mmap is faster:**

1. With mmap, SQLite reads the file by following pointers into a memory-mapped region. There is no `read()` syscall and no kernel-to-user copy — the kernel page cache pages are referenced directly.
2. The `GROUP BY` query gets the biggest win (3×) because the previously cached `bio` data still has to be re-touched to compute aggregates; saving the per-page memcpy pays off cumulatively.
3. The index probe (`age = 25`) reads only ~6 leaf pages, so the syscall savings are immeasurable — both methods finish in ~1 ms.
4. **Caveat:** the entire 48 MB DB fits in the OS page cache, so this is a *warm* benchmark on a fast NVMe-class disk. On a cold cache or a multi-GB DB, the mmap win can be larger because mmap also enables read-ahead prefetching.

### 2.5 Process inspection (`ps aux | grep sqlite` equivalent)

PowerShell equivalent:

```powershell
PS> Get-Process | Where-Object { $_.ProcessName -match 'sqlite' } |
    Select-Object Id, ProcessName,
        @{N='WS_MB';E={[math]::Round($_.WorkingSet/1MB,2)}},
        @{N='VM_MB';E={[math]::Round($_.VirtualMemorySize/1MB,2)}}, StartTime
```

Output while a `sqlite3 sample.db` REPL is open:

```
   Id  ProcessName  WS_MB  VM_MB  StartTime
   --  -----------  -----  -----  ---------
10360  sqlite3       6.66  88.93  07-05-2026 08:31:09
```

A single process — that is the entire SQLite "server". With `mmap_size = 0` the working set is small and there is no mapped region for the DB.

---

## 3. PostgreSQL Exploration

PostgreSQL is a multi-process, client/server engine, so there is no single "DB file size" — there is an entire cluster directory under `data/`. The relevant unit of analysis is the *relation* (table or index).

### 3.1 Page size / page count / file location

```sql
-- run from psql -U postgres -d postgres
SHOW block_size;                                            -- compile-time default
SELECT relname, relpages, reltuples::bigint AS rows,
       pg_size_pretty(pg_relation_size(oid)) AS size
FROM pg_class
WHERE relname IN ('users','idx_users_city','idx_users_age','users_pkey')
ORDER BY relname;

SELECT pg_size_pretty(pg_relation_size('users'))      AS heap_size,
       pg_size_pretty(pg_indexes_size('users'))       AS indexes_size,
       pg_size_pretty(pg_total_relation_size('users')) AS total_size,
       pg_relation_size('users') / 8192               AS heap_pages,
       pg_total_relation_size('users') / 8192         AS total_pages;

SELECT current_setting('data_directory') AS data_dir;
SELECT pg_relation_filepath('users')     AS users_file;
SELECT pg_size_pretty(pg_database_size('postgres')) AS db_size;
```

Output:

```
 block_size : 8192                            -- 8 KB per page (compile-time)

  relname        | relpages | rows   | size
  ---------------+----------+--------+---------
  idx_users_age  |    176   | 200000 | 1408 kB
  idx_users_city |    172   | 200000 | 1376 kB
  users          |   6241   | 200000 | 49 MB     <-- heap, 6241 * 8192 = 51,126,272 bytes
  users_pkey     |    551   | 200000 | 4408 kB

  heap_size : 49 MB     indexes_size : 7192 kB     total_size : 56 MB
  heap_pages : 6241     total_pages  : 7146

  data_dir   : C:/Program Files/PostgreSQL/16/data
  users_file : base/5/16397                       -- relative to data_dir
  db_size    : 64 MB                              -- the whole postgres DB
```

**Key observations:**

- PostgreSQL's page size (`block_size`) is **8192 bytes** — twice SQLite's default. This is fixed at compile time (`./configure --with-blocksize=N`); it cannot be changed at runtime per-database.
- Total relation size (`pg_total_relation_size`) includes the heap, all indexes, the visibility map, and the free-space map. For our `users` table that is 56 MB (vs. SQLite's 48 MB for the same data + same two indexes).
- The heap file is `data/base/5/16397` and is split into 1 GB segments by PostgreSQL when needed.

### 3.2 Server-side memory settings (PG's analogue of `mmap_size`)

PostgreSQL does *not* have a per-connection `mmap_size`. Instead the server pre-allocates a fixed shared-memory pool used by all backends. The relevant knobs:


| Parameter                         | Setting (raw) | Human-readable | Role                                                      |
| --------------------------------- | ------------- | -------------- | --------------------------------------------------------- |
| `block_size`                      | 8192          | 8 KB           | Page size on disk and in shared buffers                   |
| `shared_buffers`                  | 16384         | **128 MB**     | Server-managed page cache (uses POSIX shm / mmap on Unix) |
| `effective_cache_size`            | 524288        | **4 GB**       | Optimizer hint about *combined* OS + PG cache             |
| `work_mem`                        | 4096          | 4 MB           | Per-operation sort/hash memory                            |
| `wal_buffers`                     | 512           | 4 MB           | WAL ring buffer                                           |
| `max_parallel_workers_per_gather` | 2             | —              | Parallel-query worker cap                                 |


So PG's "mmap" is more like *one* large region per cluster, mapped permanently for the duration of the postmaster's life. It's not toggled per query.

### 3.3 Query timing (`\timing on` + `EXPLAIN (ANALYZE, BUFFERS)`)



```
=== Trial 1 ===                                              === Trial 2 ===
SUM(length(bio))    32288895    Time: 74.102 ms             Time: 109.988 ms
SUM(length(bio))    32288895    Time: 71.407 ms             Time:  62.072 ms
SUM(length(bio))    32288895    Time: 63.578 ms             Time:  59.421 ms
GROUP BY city,...   (5 rows)    Time: 51.849 ms             Time:  55.863 ms
GROUP BY city,...   (5 rows)    Time: 53.762 ms             Time:  42.017 ms
WHERE age = 25      3334        Time:  0.779 ms             Time:   1.014 ms
WHERE age = 25      3334        Time:  0.334 ms             Time:   0.590 ms
```

Averages (warm cache):


| Query                               | PostgreSQL avg |
| ----------------------------------- | -------------- |
| Full scan `SUM(length(bio))`        | **73 ms**      |
| `GROUP BY city, COUNT(*), AVG(age)` | **51 ms**      |
| Index probe `WHERE age = 25`        | **0.7 ms**     |


`EXPLAIN (ANALYZE, BUFFERS)` reveals the structural reasons:

```
SUM(length(bio)) full scan:
  Finalize Aggregate
    -> Gather  (Workers Planned: 2, Workers Launched: 2)
        -> Partial Aggregate
            -> Parallel Seq Scan on users
              Buffers: shared hit=6241    -- exactly the table page count
  Execution Time: 65.318 ms

GROUP BY city:
  Finalize GroupAggregate
    -> Gather Merge (2 workers)
        -> Sort  (qsort, 25 kB)
            -> Partial HashAggregate     -- HashAggregate per worker
                -> Parallel Seq Scan on users
                  Buffers: shared hit=6241
  Execution Time: 103 / 51 ms

WHERE age = 25:
  Aggregate
    -> Index Only Scan using idx_users_age
        Heap Fetches: 0                  -- visibility map covered everything
        Buffers: shared hit=6
  Execution Time: 0.96 ms
```

Three things to notice:

1. **All 6,241 heap pages are `shared hit`**, never `read` — the table fits entirely in `shared_buffers`.
2. The full scan and GROUP BY were **parallelised across 3 processes** (leader + 2 workers).
3. The `age = 25` query did an **Index Only Scan with zero Heap Fetches** — the visibility map (kept up to date by the prior `ANALYZE`/autovacuum) confirmed all matching tuples are visible, so we never touched the heap.

### 3.4 Process inspection (`ps aux | grep postgres` equivalent)

```
   Id  ProcessName  WS_MB  VM_MB
   --  -----------  -----  -----
12016  postgres     22.91  311.78    -- postmaster
14516  postgres     12.25  268.78    -- background worker
20060  postgres     12.13  268.78    -- background writer
22836  postgres     11.12  270.43    -- WAL writer
25292  postgres     12.69  266.75    -- autovacuum launcher
29128  postgres     11.57  266.75    -- logical replication launcher
31212  postgres     15.65  266.75    -- checkpointer / stats collector
```

Seven processes for an **idle** server. Each query that uses parallelism temporarily spawns 1–2 more `postgres` workers and then they are returned to the pool. SQLite, by contrast, runs entirely inside the calling application's process.

---

## 4. Side-by-side comparison

### 4.1 Page size and page count for the same dataset


| Engine                      | Page size | Pages (heap) | Heap size | + Indexes | Total on disk |
| --------------------------- | --------- | ------------ | --------- | --------- | ------------- |
| **SQLite** (4 KB pages)     | 4,096 B   | 12,378       | 48.35 MB  | included  | **48.35 MB**  |
| SQLite (8 KB pages)         | 8,192 B   | 6,038        | 47.17 MB  | included  | 47.17 MB      |
| SQLite (16 KB pages)        | 16,384 B  | 3,003        | 46.92 MB  | included  | 46.92 MB      |
| **PostgreSQL** 16 (default) | 8,192 B   | 6,241        | 49 MB     | 7.2 MB    | **56 MB**     |


Notes:

- SQLite stores tables and indexes in the **same file**; PostgreSQL keeps each relation in **its own file** under `base/<oid>/`.
- For the same 200 K rows, PostgreSQL's heap size is essentially identical to SQLite at the same 8 KB page size (47–49 MB). The extra ~7 MB is per-relation: the primary-key B-tree, two secondary indexes, the visibility map and free-space map.
- SQLite's 4 KB default makes its page count look much larger (12,378) than PostgreSQL's (6,241), but the actual byte count is similar — pages are just half the size.

### 4.2 Query performance summary

All times are warm-cache averages, milliseconds, single client.


| Query                        | SQLite (no mmap) | SQLite (mmap=256 MB) | PostgreSQL 16 |
| ---------------------------- | ---------------- | -------------------- | ------------- |
| Full scan `SUM(length(bio))` | **148**          | **107**              | **73**        |
| `GROUP BY city, COUNT, AVG`  | **665**          | **220**              | **51**        |
| Index probe `WHERE age = 25` | 1.0              | 1.0                  | 0.7           |


PostgreSQL's win comes mostly from:

1. **Parallel sequential scans** (3 workers) — SQLite is single-threaded per connection.
2. **HashAggregate** for GROUP BY vs SQLite's sort-based aggregate.
3. **Index-Only Scan with visibility map** for the index probe — comparable to SQLite's covering-index probe.

### 4.3 mmap impact (SQLite-specific)


| Workload               | OFF (`mmap=0`) | ON (`mmap=256 MB`) | Speedup |
| ---------------------- | -------------- | ------------------ | ------- |
| Full scan aggregate    | 148 ms         | 107 ms             | ~1.4×   |
| GROUP BY + AVG         | 665 ms         | 220 ms             | ~3.0×   |
| Index probe (~6 pages) | 1 ms           | 1 ms               | 1×      |


Why? With `mmap_size = 0` SQLite calls `read()` for every page; the kernel must copy the page from page cache into SQLite's heap. With `mmap_size > 0`, SQLite's pager points directly into the kernel's page cache via the file mapping. Saved per-page work × millions of pages touched = real speedup, especially for scans.

PostgreSQL has no equivalent toggle — it always uses its own shared-memory buffer pool sized by `shared_buffers`. Per query it cannot be tuned.

### 4.4 Architecture & memory footprint


| Aspect              | SQLite                               | PostgreSQL 16                                 |
| ------------------- | ------------------------------------ | --------------------------------------------- |
| Process model       | In-process library, 1 process        | postmaster + ≥ 6 background processes         |
| Process working set | ~7 MB (one `sqlite3.exe`)            | ~98 MB across 7 idle backends                 |
| Shared memory       | None; per-connection page cache      | `shared_buffers = 128 MB` shared by all       |
| Concurrency         | One writer at a time (file lock)     | MVCC, many writers, parallel queries          |
| File layout         | One `.db` file per database          | Cluster dir; one file per relation, 1 GB segs |
| Page size           | Per-DB, runtime-tunable (1 KB–64 KB) | Cluster-wide, compile-time fixed (8 KB)       |
| Memory-map control  | `PRAGMA mmap_size` (per connection)  | Implicit via `shared_buffers`                 |
| Write durability    | rollback journal (or WAL)            | WAL + checkpointer + bg writer                |


### 4.5 Summary table — at a glance


| Metric                           | SQLite (default)                     | PostgreSQL 16 (default)               |
| -------------------------------- | ------------------------------------ | ------------------------------------- |
| **Page size**                    | 4 KB                                 | 8 KB                                  |
| **Page count (200 K rows heap)** | 12,378                               | 6,241                                 |
| **Heap on disk**                 | 48.35 MB (table+indexes in one file) | 49 MB (heap only)                     |
| **Total on disk**                | 48.35 MB                             | 56 MB (heap + indexes + maps)         |
| **mmap default**                 | OFF (`mmap_size = 0`)                | N/A — server-managed shared buffers   |
| **Full-scan time**               | 148 ms (no mmap) / 107 ms (mmap)     | 73 ms (parallel)                      |
| **GROUP BY time**                | 665 ms / 220 ms                      | 51 ms                                 |
| **Index probe time**             | 1 ms                                 | 0.7 ms                                |
| **Server processes**             | 1 (in-process)                       | 7 idle (more during parallel queries) |


---

## 5. Conclusions

1. **Same data, similar bytes.** Both engines store our 200 K rows in roughly 48–49 MB of heap. SQLite is slightly more compact overall because its page count varies by tuning (4–16 KB) and indexes share the same file. PostgreSQL's per-relation files plus visibility/free-space maps add ~15 % overhead but enable MVCC and concurrent writes.
2. **Page size matters less than expected here**, because both DBs fit entirely in the OS page cache. On larger-than-RAM datasets, larger pages reduce per-page overhead and improve scan throughput; smaller pages reduce read amplification for point lookups. SQLite lets you experiment per-DB; PostgreSQL fixes it at compile time.
3. `**mmap_size` is the single most impactful SQLite tuning knob for read workloads.** Going from `0` to `256 MB` cut our GROUP BY time by ~3× and our full-scan time by ~1.4× with zero code changes. It costs only virtual address space.
4. **PostgreSQL is meaningfully faster on full scans and aggregates**, because of parallel query execution and a proper hash-based GROUP BY. SQLite remains the right choice when you want zero-config, single-file, embedded, or low-concurrency workloads.
5. **Index probes are equally fast** in both engines (~1 ms for ~3,300 matching rows out of 200 K). Once an index is involved, the algorithmic class dominates and engine-level differences mostly disappear.

---



