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
| SQLite version | `<fill from sqlite3 --version>` |
| PostgreSQL version | `<fill from psql --version>` |
| Sample dataset (SQLite) | Chinook (public sample DB) |
| Sample dataset (PostgreSQL) | `users` table, 100,000 rows generated via `generate_series` |

> Reproducibility: every command run is in [`sqlite_experiments.sh`](./sqlite_experiments.sh)
> and [`psql_experiments.sh`](./psql_experiments.sh).

---

## 3. SQLite3 — Observations

### 3.1 File on disk
```bash
ls -lh chinook.db
# → <fill: e.g. -rw-r--r-- 1 user staff 1.0M ... chinook.db>
```

### 3.2 PRAGMA values
```sql
PRAGMA page_size;       -- <fill, e.g. 4096>
PRAGMA page_count;      -- <fill, e.g. 264>
PRAGMA freelist_count;  -- <fill>
PRAGMA encoding;        -- <fill, e.g. UTF-8>
PRAGMA mmap_size;       -- <fill, default is often 0 = disabled>
```

**Sanity check:** `page_size × page_count` ≈ on-disk file size.
`<fill>` × `<fill>` = `<fill>` bytes ≈ `ls -lh` size.

### 3.3 Query timing — `SELECT * FROM Track`

| Run | `mmap_size` | wall time | rows |
|---|---|---|---|
| No mmap | `0` | `<fill, e.g. 0.042s>` | `<fill>` |
| With mmap | `268435456` (256 MB) | `<fill, e.g. 0.029s>` | `<fill>` |

**Heavier 3-table join** (`Track ⨝ Album ⨝ Artist`):

| Run | `mmap_size` | wall time |
|---|---|---|
| No mmap | `0` | `<fill>` |
| With mmap | `256 MB` | `<fill>` |

### 3.4 What I observed about `mmap_size`
- With `mmap_size = 0`, SQLite uses standard `read()` syscalls — every page
  read is copied from the OS page cache into SQLite's own buffer.
- With a non-zero `mmap_size`, SQLite maps the database file into the
  process's address space; reads become memory accesses, no copy.
- `<fill: did you actually see a speedup? by how much? was it stable across
  runs or noisy?>`

### 3.5 `ps aux` while a long query ran
```
<paste the relevant line(s) from `ps aux | grep sqlite3` here>
```
- VSZ jumped to roughly the mmap window size when `mmap_size` was set,
  confirming the file was actually mapped.

---

## 4. PostgreSQL — Observations

### 4.1 Server config
```
SHOW block_size;            -- 8192 (8 KB) — fixed at compile time
SHOW shared_buffers;        -- <fill, default 128MB on Homebrew>
SHOW effective_cache_size;  -- <fill>
```

### 4.2 Table layout (`users`, 100k rows)
```sql
SELECT relpages, reltuples,
       pg_size_pretty(pg_relation_size('users')) AS heap,
       pg_size_pretty(pg_total_relation_size('users')) AS total
FROM pg_class WHERE relname='users';
```
| relpages (page_count) | reltuples (rows) | heap size | total size (incl. indexes) |
|---|---|---|---|
| `<fill>` | `<fill>` | `<fill>` | `<fill>` |

**Sanity check:** `block_size × relpages` ≈ heap size.
`8192 × <fill>` = `<fill>` bytes ≈ heap.

### 4.3 Query timing — `SELECT * FROM users`

| Cache state | wall time | rows |
|---|---|---|
| Cold (fresh server restart) | `<fill, e.g. 380 ms>` | 100000 |
| Warm (second run) | `<fill, e.g. 90 ms>` | 100000 |

### 4.4 EXPLAIN (ANALYZE, BUFFERS)
```
<paste the EXPLAIN output here>
```
Key things to call out from the plan:
- Whether it was a **Seq Scan** or used an index.
- `shared hit` vs `shared read` blocks → how much was already cached.
- Planning vs execution time.

### 4.5 After a `DELETE … WHERE id % 4 = 0` and `VACUUM`
| Stage | relpages | heap size |
|---|---|---|
| Before delete | `<fill>` | `<fill>` |
| After delete (no vacuum) | `<fill>` | `<fill>` |
| After VACUUM | `<fill>` | `<fill>` |

PostgreSQL keeps the file size (relpages doesn't shrink without `VACUUM FULL`)
because dead tuples leave reusable space inside existing pages.

---

## 5. Comparison: SQLite3 vs PostgreSQL

| Dimension | SQLite3 | PostgreSQL |
|---|---|---|
| **Architecture** | Embedded library; one process, one file | Client–server, multi-process |
| **Page size** | Configurable (`PRAGMA page_size`); default **4096** bytes; can be 512 – 65536 | Fixed at **compile time** (`SHOW block_size`); default **8192** bytes |
| **Where stored** | Single `.db` file | `$PGDATA` directory; one file per ~1 GB chunk per relation |
| **Page count source** | `PRAGMA page_count` | `pg_class.relpages` |
| **Buffer/cache strategy** | OS page cache + optional `mmap` | Dedicated `shared_buffers` + OS page cache (no mmap) |
| **mmap support** | Yes — `PRAGMA mmap_size` controls window size | **No** — Postgres deliberately doesn't use mmap for the heap |
| **Effect of mmap (observed)** | `<fill: e.g. ~30% faster on full-table scan, less consistent on small queries>` | N/A; warm-vs-cold cache is the closest analogue — `<fill: warm run was ~Nx faster>` |
| **Query timing tools** | `.timer on` and shell `time` | `\timing on`, `EXPLAIN (ANALYZE, BUFFERS)` |
| **Concurrency** | Whole-DB lock (single writer, multiple readers) | MVCC; many concurrent writers |
| **Process footprint** | Same process as the app; no daemon | Background postmaster + per-connection backend processes (visible in `ps aux`) |
| **Best for** | Embedded/local apps, mobile, single-user analytics | Multi-user web apps, concurrent workloads, large datasets |

### 5.1 Where the page-size difference matters
SQLite's smaller default page (4 KB vs Postgres's 8 KB) means more pages for
the same data, which can mean more I/O operations on a full scan — but each
read is cheaper. Postgres's larger page amortizes per-page overhead and pairs
well with sequential scans, which dominate analytical workloads.

### 5.2 Where the mmap difference matters
- SQLite's `mmap` removes one copy (kernel buffer → user buffer) per page read,
  which on a single-file embedded DB is a clear win for read-heavy workloads.
- PostgreSQL avoids mmap because `shared_buffers` lets it manage eviction,
  dirty-page write-back, and crash recovery (WAL) deterministically. mmap'ed
  writes can hit disk at arbitrary times, which would break PG's durability
  contract.

### 5.3 What "mmap impact" looks like in practice (this lab)
- **SQLite:** `<fill: e.g. SELECT * FROM Track went from 42 ms → 29 ms with
  mmap_size = 256 MB. The join query saw a similar relative speedup.>`
- **PostgreSQL:** No mmap, but warm-cache run was `<fill: ~Nx>` faster than
  cold-cache, showing the role `shared_buffers` + OS cache play.

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
psql -d adbms_lab2 -c "EXPLAIN (ANALYZE, BUFFERS) SELECT age, COUNT(*) FROM users GROUP BY age;"
ps aux | grep postgres
```

---

## 7. Conclusion

- Both engines store data as **fixed-size pages**, but only SQLite lets you
  change the page size and lets you opt into `mmap` at runtime.
- PostgreSQL's design — `shared_buffers`, MVCC, WAL — trades the simplicity of
  mmap for durability and concurrency guarantees that a server-grade RDBMS needs.
- For a single-process, read-heavy workload SQLite + `mmap` is hard to beat in
  raw speed; for anything multi-user, durable, or large-scale, PostgreSQL's
  buffer-pool architecture is the right tool.

---

*Submitted as part of Advanced DBMS — Lab 2.*
