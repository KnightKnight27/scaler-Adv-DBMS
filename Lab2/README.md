# Lab 2 — SQLite3 vs PostgreSQL: Page Size, Pages, and mmap

| | |
|---|---|
| **Role Number** | 24bcs10394 |
| **Name** | Ridaa Mahrooz Mirza |
| **Environment** | Ubuntu 24.04 (WSL2 on Windows 11) |
| **SQLite version** | 3.45.1 |
| **PostgreSQL version** | 16.13 |
| **Workload** | `users(id, name, email, age)` table populated with **1,000,000 rows** |

All raw output from the experiments is preserved in `Lab2/results/sqlite_output.txt` and `Lab2/results/postgres_output.txt`. The shell scripts that produced them are committed alongside this report.

---

## How to reproduce

```bash
bash Lab2/setup.sh        # one-time apt install of sqlite3 + postgresql
bash Lab2/run_all.sh      # runs both experiments, tees output to Lab2/results/
```

Individual scripts:
- `Lab2/sqlite_experiment.sh` — SQLite PRAGMAs, mmap-on/off timings, `ps aux`
- `Lab2/postgres_experiment.sh` — `SHOW`s, `pg_class.relpages`, `\timing`, `ps aux`

---

## 1. SQLite3 — observations

### Commands used

```bash
sqlite3 /tmp/lab2_sqlite.db                      # open / create DB
.timer on                                        # CLI query timing
PRAGMA page_size; PRAGMA page_count;             # storage layout
PRAGMA freelist_count; PRAGMA cache_size;
PRAGMA journal_mode; PRAGMA mmap_size;
PRAGMA mmap_size = 0;          -- disable mmap
PRAGMA mmap_size = 268435456;  -- 256 MB mmap window
ls -lh /tmp/lab2_sqlite.db                       # file size on disk
ps aux | grep sqlite                             # process listing
```

### Storage layout

| PRAGMA | Value |
|---|---|
| `page_size` | **4096** bytes (4 KB) |
| `page_count` | **10,502** |
| `freelist_count` | 0 |
| `cache_size` | -2000 (i.e. 2 MB cache, KB-as-negative convention) |
| `journal_mode` | `delete` (default rollback journal) |
| `mmap_size` | **0** (disabled by default) |
| File on disk | **42 MB** (`-rw-r--r-- ridaa ridaa 42M /tmp/lab2_sqlite.db`) |

Sanity check: `page_size × page_count` = `4096 × 10502` = **43.0 MB** ≈ disk size. The DB file is essentially a flat array of 4 KB pages.

### Query timing

| Query | mmap = 0 | mmap = 256 MB | Speed-up |
|---|---|---|---|
| `SELECT COUNT(*), AVG(age) FROM users` | **0.055 s** | **0.040 s** | ~27 % faster |
| `SELECT COUNT(*) FROM users WHERE age > 30` | **0.038 s** | **0.033 s** | ~13 % faster |

### `ps aux | grep sqlite`

Only the running shell script appears — **no `sqlite` daemon process exists**. SQLite is a library that runs *in-process* with whatever opens the DB file; there is no separate server.

### Key SQLite findings

1. **Default page size is 4 KB**, set when the DB is first created and immutable for the life of the file (changing it requires `VACUUM INTO` into a new DB).
2. **`mmap_size = 0` by default** — SQLite uses ordinary `read()`/`write()` syscalls. mmap is opt-in.
3. Enabling a 256 MB mmap window gave a **modest 13–27 % speed-up** at this dataset size. The benefit would grow with larger DBs that don't fit in the small (2 MB) page cache.
4. The 1M-row table fits in **42 MB** — extremely compact thanks to SQLite's variable-length record encoding.
5. **No background processes** to worry about; concurrency is handled with file locks (or WAL).

---

## 2. PostgreSQL — observations

### Commands used

```bash
sudo service postgresql start                            # bring server up
sudo -u postgres createdb labtest
psql -d labtest
\timing on                                               # client-side query timer
SHOW block_size; SHOW shared_buffers;                    # storage / memory config
SHOW effective_cache_size; SHOW work_mem;
SELECT relname, relpages, reltuples,
       pg_size_pretty(pg_relation_size('users')),
       pg_size_pretty(pg_total_relation_size('users'))
FROM pg_class WHERE relname = 'users';
SHOW data_directory;                                     # where files live
SELECT pg_relation_filepath('users');                    # path of the table heap
DISCARD ALL;                                             # invalidate session caches
ps aux | grep postgres                                   # backend processes
```

### Storage layout

| Setting / metric | Value |
|---|---|
| `block_size` | **8192** bytes (8 KB) — compile-time constant |
| `shared_buffers` | 128 MB |
| `effective_cache_size` | 4 GB |
| `work_mem` | 4 MB |
| `relpages` (table heap pages) | **9,345** |
| `reltuples` | 1,000,000 |
| Heap size | **73 MB** |
| Total size (heap + PK index) | **95 MB** |
| Data directory | `/var/lib/postgresql/16/main` |
| Heap file path | `base/16388/16390` |

Sanity check: `block_size × relpages` = `8192 × 9345` = **73 MB** ≈ heap size on disk.

### Query timing

| Query | Cold (after `DISCARD ALL`) | Warm cache | Speed-up |
|---|---|---|---|
| `SELECT COUNT(*), AVG(age) FROM users` | **45.287 ms** | **32.325 ms** | ~29 % faster |
| `SELECT COUNT(*) FROM users WHERE age > 30` | **35.671 ms** | **24.765 ms** | ~31 % faster |

### `ps aux | grep postgres`

Six resident background processes are visible:

```
postgres  /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main ...   (postmaster)
postgres  postgres: 16/main: checkpointer
postgres  postgres: 16/main: background writer
postgres  postgres: 16/main: walwriter
postgres  postgres: 16/main: autovacuum launcher
postgres  postgres: 16/main: logical replication launcher
```

This is the **client/server architecture** in action — totally absent in SQLite.

### Key PostgreSQL findings

1. **Default block size is 8 KB**, twice SQLite's. Changing it requires recompiling Postgres from source.
2. PostgreSQL **does not use mmap** for the main heap. It manages its own buffer pool (`shared_buffers = 128 MB`) and relies on the OS page cache as a second tier — a "double buffering" design, chosen so that WAL ordering and crash recovery stay under PG's control.
3. The same 1M-row table takes **73 MB** in PG vs **42 MB** in SQLite — Postgres pays a per-tuple overhead (24-byte heap tuple header, alignment padding, MVCC metadata).
4. Cold-vs-warm gap is ~30 %, showing the buffer pool / OS cache effect.

---

## 3. Side-by-side comparison

| Property | SQLite3 (3.45) | PostgreSQL (16.13) |
|---|---|---|
| **Page / block size** | 4 KB (`PRAGMA page_size`) | 8 KB (`SHOW block_size`) |
| **Page / block count for 1M-row `users`** | 10,502 | 9,345 |
| **Heap size on disk for 1M rows** | **42 MB** | **73 MB** (95 MB with PK index) |
| **Bytes per row (heap)** | ~44 B | ~76 B (per-tuple header + alignment) |
| **Default cache** | 2 MB page cache + OS cache | 128 MB `shared_buffers` + OS cache |
| **mmap support** | Optional, opt-in via `PRAGMA mmap_size` | Not used for heap; PG uses its own buffer pool |
| **Effect of mmap (this run)** | 13–27 % faster on aggregate scans | N/A |
| **Cold COUNT/AVG (1M rows)** | 55 ms (no mmap) | 45 ms (cold) |
| **Warm COUNT/AVG (1M rows)** | 40 ms (mmap on) | 32 ms |
| **Architecture** | Embedded library, no daemon | Multi-process client/server |
| **Background processes seen** | None (`ps aux` shows only the CLI) | postmaster + checkpointer + bgwriter + walwriter + autovacuum + logical-rep launcher |
| **Concurrency model** | File locks (or WAL); single-writer | MVCC, many concurrent writers |
| **Where it lives on disk** | One `.db` file | Cluster directory `/var/lib/postgresql/16/main` with per-relation files |

---

## 4. Analysis

### Page size

SQLite's 4 KB matches the typical OS page, keeping reads aligned with the kernel's IO unit. Postgres's 8 KB is a deliberate trade-off: larger pages amortize the **24-byte tuple header** + line-pointer overhead better, and reduce per-page indexing cost, at the price of slightly more wasted space for small rows.

### Page count and storage density

| | Pages | Bytes / page | Total | Bytes / row |
|---|---|---|---|---|
| SQLite | 10,502 | 4,096 | 43 MB | ~44 |
| PostgreSQL | 9,345 | 8,192 | 73 MB | ~76 |

PG stores the **same 1M rows in fewer pages** (because each page is twice as big) but consumes **~70 % more bytes overall** because of MVCC heap-tuple bookkeeping (`xmin`, `xmax`, `cmin/cmax`, t_ctid, infomask, alignment padding). SQLite's compact varint encoding wins on raw size.

### Query performance

For 1M rows on a single CPU, both engines finish a full table aggregate in **tens of milliseconds**.

- **SQLite is slightly quicker on the wire** because the query runs *in-process*: zero IPC, zero parsing-then-network roundtrip. It's effectively a function call.
- **PostgreSQL has client/server overhead** (libpq round-trip, planner, executor pipeline) but compensates with a much larger default buffer pool.
- The warm-vs-cold gap on PG (~30 %) is real — once `shared_buffers` is hot, repeated aggregates get noticeably faster.

For OLTP point-lookups by primary key (not measured here), both are sub-millisecond.

### mmap impact

| | Default | Effect |
|---|---|---|
| SQLite | `mmap_size = 0` (off) | Turning it on (256 MB) gave **13–27 % faster** scans of a 42 MB DB. The DB already fits in the OS page cache, so most of the gain is from skipping the user-space copy that `read()` performs. The bigger the DB *and* the more it doesn't fit in cache, the larger the win. |
| PostgreSQL | Not applicable | PG explicitly avoids `mmap` for heap files. Its rationale: WAL-protected writes need ordered, crash-safe IO that PG controls itself; relying on `mmap` would let the kernel write dirty pages back at unpredictable times and break recovery guarantees. |

So the question "what is the impact of mmap?" has different answers for each engine: **a measurable optimization for SQLite, an architectural non-feature for PostgreSQL.**

### Architecture (`ps aux`)

The `ps` output is the cleanest illustration of the difference:

- **SQLite** has *no* persistent process. Every reader/writer opens the file directly. It's library code — like calling `libsqlite3.so` from your program.
- **PostgreSQL** has a postmaster plus a fixed set of helper processes (checkpointer, bgwriter, walwriter, autovacuum) that run continuously, regardless of whether any client is connected. Each connection spawns an additional backend worker.

This is why PG handles concurrent writes / crash recovery / replication elegantly, and why SQLite is so trivially deployable — opposite ends of the same spectrum.

---

## 5. Summary

| Question | Answer |
|---|---|
| Which has a smaller default page? | SQLite (4 KB vs 8 KB) |
| Which uses fewer pages for the same 1M rows? | PostgreSQL (9,345 vs 10,502) — but only because each page is twice as large |
| Which is more compact on disk? | **SQLite** by a large margin (42 MB vs 73 MB heap) |
| Which is faster on a single-process aggregate? | Roughly tied; SQLite's in-process model gives a small edge cold-cache, PG catches up warm |
| Does mmap help SQLite? | Yes, modestly (13–27 %) at this scale; bigger wins on larger DBs |
| Does mmap help PostgreSQL? | PG doesn't use mmap; it keeps its own buffer pool |
| Background daemons? | SQLite: zero. Postgres: ~6 always-on processes |

**Practical takeaway:** SQLite is a lean embedded engine that wins on simplicity and storage density; PostgreSQL is a full server that pays a per-row overhead in exchange for MVCC concurrency, replication, and crash safety. They're optimised for different deployment shapes, not for outperforming each other on a benchmark.
