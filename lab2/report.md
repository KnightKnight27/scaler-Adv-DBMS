# Lab Report: SQLite3 vs PostgreSQL — Storage Internals & Query Performance

`LLM was used to polish and structure the content rest all the cmnds and practical was done by me `

**Name:** Vimal
**Date:** 2026-05-07
**Environment:** Ubuntu 24.04 (WSL2 on Windows)
**Database:** Chinook Sample Database (digital music store)

---

## Environment Setup

SQLite3 was already present on the system. PostgreSQL was installed via `apt` and run as a native service on Ubuntu WSL2.

```bash
# SQLite3 — installed via apt
sudo apt update && sudo apt install -y sqlite3
# sqlite3 is already the newest version (3.45.1-1ubuntu2.5)

# PostgreSQL — installed via apt
sudo apt install -y postgresql postgresql-contrib
# PostgreSQL Version: 16.13

# Start PostgreSQL service
sudo service postgresql start
```

Sample database: **Chinook** — a realistic dataset modelling a digital music store with tables for artists, albums, tracks, customers, invoices, and employees. Downloaded directly from GitHub.

```bash
# SQLite version
wget https://github.com/lerocha/chinook-database/raw/master/ChinookDatabase/DataSources/Chinook_Sqlite.sqlite

# PostgreSQL version
wget https://raw.githubusercontent.com/lerocha/chinook-database/master/ChinookDatabase/DataSources/Chinook_PostgreSql.sql
sudo -u postgres createdb chinook
sudo cp ~/Chinook_PostgreSql.sql /tmp/
sudo -u postgres psql chinook -f /tmp/Chinook_PostgreSql.sql
```

---

## Part 1 — SQLite3

### File Size Observation

```bash
ls -lh ~/Chinook_Sqlite.sqlite
```

```
-rw-r--r-- 1 vimal vimal 984K May  7 05:32 /home/vimal/Chinook_Sqlite.sqlite
```

| Property  | Value      |
| --------- | ---------- |
| File Size | **984 KB** |

SQLite stores the entire database — all tables, indexes, and metadata — in a **single flat file** on disk.

---

### Storage Internals — PRAGMA Commands

```bash
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA page_size; PRAGMA page_count;"
```

```
4096
246
```

| PRAGMA       | Value                 |
| ------------ | --------------------- |
| `page_size`  | **4096 bytes (4 KB)** |
| `page_count` | **246 pages**         |

> **Observation:** Total storage = 4096 × 246 = **1,007,616 bytes (~984 KB)**, which matches the file size exactly. The 4 KB page size aligns with the default Linux kernel memory page size, so when the OS loads a page into memory, it maps directly to one SQLite page with zero waste.

---

### Additional PRAGMA Values

```bash
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA journal_mode; PRAGMA cache_size; PRAGMA integrity_check;"
```

```
delete
-2000
ok
```

```bash
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA freelist_count; PRAGMA synchronous; PRAGMA encoding;"
```

```
0
2
UTF-8
```

| PRAGMA            | Value      | Meaning                                                                                        |
| ----------------- | ---------- | ---------------------------------------------------------------------------------------------- |
| `journal_mode`    | `delete`   | Classic rollback journal — SQLite writes changes to a `-journal` file and deletes it on commit |
| `cache_size`      | `-2000`    | SQLite uses up to 2000 × page_size = **~8 MB** of in-memory page cache                         |
| `integrity_check` | `ok`       | No corruption detected in the database file                                                    |
| `freelist_count`  | `0`        | No free (deleted/unused) pages — database is fully compacted                                   |
| `synchronous`     | `2` (FULL) | SQLite waits for data to reach disk before returning from a write — maximum durability         |
| `encoding`        | `UTF-8`    | All text stored in UTF-8 encoding                                                              |

---

### mmap_size Experiment

```bash
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA mmap_size; PRAGMA mmap_size=30000000; PRAGMA mmap_size;"
```

```
0
30000000
30000000
```

| State          | Value               |
| -------------- | ------------------- |
| Default        | `0` (disabled)      |
| After enabling | `30000000` (~30 MB) |

> **Observation:** mmap is disabled by default (`mmap_size = 0`). Every page read is a buffered `read()` syscall that copies the page from kernel cache into SQLite's user-space cache. When enabled, SQLite maps the database file directly into the process's virtual address space — instead of issuing `read()` syscalls, the process accesses memory addresses backed by the file and the kernel handles page faults transparently. This setting is **per-connection** and resets each session.

---

### Query Timing — Without mmap

```bash
time sqlite3 ~/Chinook_Sqlite.sqlite "SELECT * FROM Invoice;"
```

```
real    0m0.004s
user    0m0.000s
sys     0m0.002s
```

---

### Query Timing — With mmap Enabled

```bash
time sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA mmap_size=30000000; SELECT * FROM Invoice;"
```

```
real    0m0.003s
user    0m0.000s
sys     0m0.002s
```

| Mode         | Real Time  |
| ------------ | ---------- |
| Without mmap | **0.004s** |
| With mmap    | **0.003s** |

> **Observation:** A marginal 1ms improvement was observed with mmap. The gain is minimal because the Chinook database is only 984 KB — it fits easily in the OS page cache regardless. mmap benefits become significant with larger databases (hundreds of MB) where repeated `read()` syscalls accumulate meaningful overhead. The real advantage of mmap shows up at scale — on a ~48 MB database one study observed a ~3× speedup on GROUP BY queries. On our small dataset, both approaches hit the same ceiling.

---

### SQLite Built-in Timer

```bash
sqlite3 ~/Chinook_Sqlite.sqlite <<'EOF'
.timer on
SELECT * FROM Invoice;
EOF
```

```
Run Time: real 0.002  user 0.002109  sys 0.000000
```

> **Observation:** The higher `user` time vs `sys` time shows the CPU was spending more time in user-space computation (row formatting, output) than in kernel-mode I/O — consistent with a small, already-cached table.

---

### Process Monitoring

```bash
ps aux | grep sqlite
```

```
vimal  1696  0.0  0.0  4092  1920  pts/0  S+  05:34  0:00  grep --color=auto sqlite
```

> **Observation:** No SQLite3 process was found — only the `grep` itself. This confirms SQLite3's **serverless, embedded architecture**: it runs entirely within the calling process and exits immediately after the query completes. There is no background daemon, no server, no IPC overhead — but also no concurrent writers.

---

## Part 2 — PostgreSQL

### Installation & Service

```bash
sudo apt install -y postgresql postgresql-contrib
sudo service postgresql start
```

**Version:** PostgreSQL 16.13
**Cluster path:** `/var/lib/postgresql/16/main`
**Default encoding:** UTF8

---

### Storage Internals — Block Size & Page Count

```bash
sudo -u postgres psql chinook -c "SHOW block_size;"
sudo -u postgres psql chinook -c "SELECT relpages FROM pg_class WHERE relname = 'invoice';"
```

```
 block_size
------------
 8192

 relpages
----------
        6
```

| Property                   | Value                 |
| -------------------------- | --------------------- |
| Block Size                 | **8192 bytes (8 KB)** |
| Page Count (Invoice table) | **6 pages**           |

> **Observation:** PostgreSQL's block size is fixed at **compile time** and cannot be changed without recompiling the binary. The 8 KB default is double SQLite's 4 KB, optimized for server workloads where larger fetches per I/O reduce disk reads on big table scans.

---

### Relation Size, Index Size, and File Path

```bash
sudo -u postgres psql chinook -c "SELECT pg_size_pretty(pg_relation_size('invoice'));"
sudo -u postgres psql chinook -c "SELECT pg_size_pretty(pg_indexes_size('invoice'));"
sudo -u postgres psql chinook -c "SELECT pg_size_pretty(pg_total_relation_size('invoice'));"
sudo -u postgres psql chinook -c "SELECT pg_relation_filepath('invoice');"
```

```
 pg_relation_size   →  48 kB
 pg_indexes_size    →  48 kB
 pg_total_relation_size  →  120 kB
 pg_relation_filepath    →  base/16384/16410
```

| Metric                        | Value                                                   |
| ----------------------------- | ------------------------------------------------------- |
| Heap (raw data)               | **48 kB** (6 pages × 8 KB)                              |
| Indexes                       | **48 kB**                                               |
| Total (heap + indexes + maps) | **120 kB**                                              |
| Physical file path            | `base/16384/16410` under `/var/lib/postgresql/16/main/` |

> **Observation:** Unlike SQLite's single-file design, PostgreSQL keeps **each relation in its own file** under `data/base/<db_oid>/`. The difference between `pg_relation_size` (48 kB) and `pg_total_relation_size` (120 kB) is 72 kB of index, visibility map, and free-space map overhead — the infrastructure cost on top of raw data that PostgreSQL maintains for fast lookups and MVCC.

---

### Query Execution Time

```bash
time sudo -u postgres psql chinook -c "SELECT * FROM invoice;" > /dev/null
```

```
real    0m0.030s
user    0m0.002s
sys     0m0.004s
```

> **Observation:** PostgreSQL took **0.030s** vs SQLite3's **0.004s** for the same 412-row table. This does **not** mean PostgreSQL is slower — the overhead comes from the **client-server connection model**: Unix socket connection, authentication, query parsing, planning, and result serialization across process boundaries. The `EXPLAIN (ANALYZE, BUFFERS)` output below confirms actual execution was only **0.061ms**.

---

### EXPLAIN ANALYZE — Query Planner

#### Full Table Scan with Buffer Stats

```bash
sudo -u postgres psql chinook -c "EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM invoice;"
```

```
Seq Scan on invoice  (cost=0.00..10.12 rows=412 width=66)
                     (actual time=0.006..0.028 rows=412 loops=1)
  Buffers: shared hit=6
Planning:
  Buffers: shared hit=87
Planning Time: 0.201 ms
Execution Time: 0.061 ms
```

> **Key observation:** `Buffers: shared hit=6` means all 6 pages of the Invoice table were already in `shared_buffers` — zero disk reads. The planner correctly chose a sequential scan for a small 412-row table. Planning itself touched 87 catalog pages to resolve the query.

#### Aggregation with GROUP BY

```bash
sudo -u postgres psql chinook -c "EXPLAIN ANALYZE SELECT billing_country, COUNT(*), ROUND(AVG(total)::numeric, 2) FROM invoice GROUP BY billing_country ORDER BY COUNT(*) DESC;"
```

```
Sort  (cost=14.12..14.18 rows=24 width=47) (actual time=0.131..0.132 rows=24 loops=1)
  Sort Key: (count(*)) DESC
  Sort Method: quicksort  Memory: 26kB
  ->  HashAggregate  (cost=13.21..13.57 rows=24 width=47) (actual time=0.108..0.114 rows=24 loops=1)
        Group Key: billing_country
        Batches: 1  Memory Usage: 32kB
        ->  Seq Scan on invoice  (cost=0.00..10.12 rows=412 width=13) (actual time=0.006..0.027 rows=412 loops=1)
Planning Time: 0.285 ms
Execution Time: 0.195 ms
```

> PostgreSQL chose a **HashAggregate** — it read all 412 rows in one pass and built a hash map keyed by `billing_country`, using only 32 kB of memory to group into 24 distinct countries. The final sort used quicksort on just 24 rows (26 kB). Total execution: **0.195ms**.

---

### Memory Configuration

```bash
sudo -u postgres psql chinook -c "SHOW shared_buffers;" -c "SHOW work_mem;" \
  -c "SHOW maintenance_work_mem;" -c "SHOW effective_cache_size;"
```

```
 shared_buffers        →  128MB
 work_mem              →  4MB
 maintenance_work_mem  →  64MB
 effective_cache_size  →  4GB
```

| Parameter              | Value      | Purpose                                                                                                             |
| ---------------------- | ---------- | ------------------------------------------------------------------------------------------------------------------- |
| `shared_buffers`       | **128 MB** | PostgreSQL's internal page cache shared across all connections — the server-side equivalent of SQLite's `mmap_size` |
| `work_mem`             | **4 MB**   | Memory budget per sort or hash operation per query                                                                  |
| `maintenance_work_mem` | **64 MB**  | Memory for heavy ops like VACUUM and CREATE INDEX                                                                   |
| `effective_cache_size` | **4 GB**   | Planner hint — estimated OS page cache size, influences index vs seq scan decisions                                 |

> PostgreSQL does **not** have a per-connection `mmap_size` toggle. Instead it pre-allocates `shared_buffers` as a fixed shared-memory pool used by all backends for the lifetime of the postmaster. It's not toggled per query.

---

### Database Size on Disk

```bash
sudo -u postgres psql chinook -c "SELECT pg_size_pretty(pg_database_size('chinook'));"
```

```
 pg_size_pretty
----------------
 10023 kB
```

| Property      | Value                   |
| ------------- | ----------------------- |
| Total DB Size | **10,023 kB (~9.8 MB)** |

> PostgreSQL consumed **~10 MB** for the same dataset SQLite stored in **984 KB** — roughly 10×. The extra footprint comes from system catalogs (`pg_class`, `pg_attribute`, etc.), WAL files, per-table relation files, visibility maps, free space maps, and pre-allocated index structures.

---

### Process Architecture

```bash
ps aux | grep postgres
```

```
postgres  4173  0.0  0.3  220288  31104  ?  Ss  05:34  0:00  postgres -D /var/lib/postgresql/16/main
postgres  4174  0.0  0.2  220564  19384  ?  Ss  05:34  0:00  postgres: 16/main: checkpointer
postgres  4175  0.0  0.0  220436   7480  ?  Ss  05:34  0:00  postgres: 16/main: background writer
postgres  4177  0.0  0.1  220288  10168  ?  Ss  05:34  0:00  postgres: 16/main: walwriter
postgres  4178  0.0  0.1  221880   8632  ?  Ss  05:34  0:00  postgres: 16/main: autovacuum launcher
postgres  4179  0.0  0.1  221864   8120  ?  Ss  05:34  0:00  postgres: 16/main: logical replication launcher
vimal     4400  0.0  0.0    4096   1920  ?  S+  05:52  0:00  grep --color=auto postgres
```

**6 background daemons** before a single query even runs. Each exists for a specific reason:

| Process                        | Role                                                              |
| ------------------------------ | ----------------------------------------------------------------- |
| `postmaster`                   | Main server process — accepts connections, spawns backends        |
| `checkpointer`                 | Periodically flushes dirty pages from `shared_buffers` to disk    |
| `background writer`            | Proactively writes dirty pages ahead of checkpoints to smooth I/O |
| `walwriter`                    | Flushes the Write-Ahead Log to disk — what makes `COMMIT` durable |
| `autovacuum launcher`          | Spawns workers to run VACUUM and ANALYZE on tables that need it   |
| `logical replication launcher` | Manages replication slots for streaming changes to replicas       |

> Compare this to SQLite's **zero background processes**. SQLite runs entirely inside the calling process — no network, no IPC, no daemon overhead. The trade-off: PostgreSQL's process model enables MVCC, crash recovery, and hundreds of concurrent clients; SQLite allows only one writer at a time.

---

## Part 3 — Comparison Report

### Summary Table

| Parameter                      | SQLite3                      | PostgreSQL                      |
| ------------------------------ | ---------------------------- | ------------------------------- |
| **Version**                    | 3.45.1                       | 16.13                           |
| **Page / Block Size**          | 4096 bytes (4 KB)            | 8192 bytes (8 KB)               |
| **Page Count (Invoice)**       | 246 (entire DB)              | 6 (Invoice table only)          |
| **Heap Size (Invoice)**        | 984 KB (full DB file)        | 48 kB                           |
| **Index Size (Invoice)**       | included in DB file          | 48 kB (separate)                |
| **Total Size (Invoice)**       | 984 KB (full DB)             | 120 kB                          |
| **Total DB Size**              | 984 KB                       | 10,023 kB (~9.8 MB)             |
| **Physical file layout**       | Single `.db` file            | `base/16384/16410` per relation |
| **SELECT \* FROM invoice**     | 0.004s                       | 0.030s (conn overhead)          |
| **Actual execution time**      | 0.002s (`.timer on`)         | 0.061ms (`EXPLAIN ANALYZE`)     |
| **Buffer hits (invoice scan)** | N/A                          | `shared hit=6` (all in cache)   |
| **With mmap enabled**          | 0.003s                       | N/A                             |
| **journal_mode**               | `delete` (rollback journal)  | WAL (Write-Ahead Log)           |
| **synchronous**                | `2` (FULL)                   | fsync on commit                 |
| **encoding**                   | UTF-8                        | UTF-8                           |
| **freelist_count**             | `0` (fully compacted)        | N/A                             |
| **In-memory cache**            | `cache_size` = -2000 (~8 MB) | `shared_buffers` = 128 MB       |
| **Background processes**       | 0                            | 6 daemons                       |
| **Query planner**              | Rule-based                   | Cost-based (`EXPLAIN ANALYZE`)  |
| **Architecture**               | Serverless / embedded        | Client-server daemon            |
| **Concurrent writers**         | 1 (file-level lock)          | Many (row-level MVCC)           |

---

### Analysis

#### Page Size

SQLite3 uses a default page size of **4 KB**, aligning with the Linux kernel's memory page size so that one OS memory page maps to exactly one SQLite page with zero waste. PostgreSQL uses **8 KB blocks**, fixed at compile time. The larger block size suits server workloads where larger fetches per I/O reduce disk reads on large table scans. SQLite lets you experiment per-database at runtime; PostgreSQL fixes it cluster-wide at compile time.

#### Page Count & File Layout

SQLite reports `PRAGMA page_count` for the **entire database file** (246 pages = 984 KB), while PostgreSQL tracks pages **per table** via `pg_class.relpages`. The Invoice table in PostgreSQL uses only 6 pages (48 kB raw, 120 kB with indexes). Unlike SQLite's single-file design, PostgreSQL stores each relation in its own file (`base/16384/16410`), with separate visibility maps and free-space maps per relation. The `pg_indexes_size` (48 kB) vs `pg_relation_size` (48 kB) breakdown makes this overhead visible at the table level.

#### Query Performance & Buffer Stats

SQLite3 completed `SELECT * FROM Invoice` in **0.004s**, while PostgreSQL took **0.030s** — roughly 7.5× slower on the surface. However, `EXPLAIN (ANALYZE, BUFFERS)` reveals PostgreSQL's actual execution was only **0.061ms** — `Buffers: shared hit=6` confirms all 6 table pages were already in `shared_buffers` with zero disk reads. The remaining time is client-server connection overhead. The `EXPLAIN ANALYZE` on the GROUP BY query shows intelligent operator selection — **HashAggregate** for grouping (32 kB memory) and **quicksort** for the final 24-row sort (26 kB).

#### mmap Impact

Enabling `mmap_size = 30000000` in SQLite reduced query time from **0.004s to 0.003s** — marginal on a ~1 MB database. The honest result: the OS page cache already keeps the file warm after the first read regardless of mmap. The real advantage shows at scale — on a ~48 MB database, mmap has been observed to cut GROUP BY time by ~3× by eliminating per-page `read()` syscall overhead and kernel-to-user memory copies. PostgreSQL has no equivalent toggle; its `shared_buffers` pool (128 MB) serves the same purpose automatically.

#### Disk Usage

PostgreSQL consumed **~10 MB** for the same dataset SQLite stored in **984 KB** — approximately 10×. At the table level, `pg_total_relation_size` (120 kB) vs `pg_relation_size` (48 kB) shows 72 kB of index and infrastructure overhead on top of 48 kB of raw data. SQLite's single-file design has minimal overhead, making it ideal for embedded and mobile applications where storage is constrained.

#### Architecture is the Root of Everything

Every performance characteristic in this lab traces back to one fact — SQLite is a **library**, PostgreSQL is a **server**. SQLite runs inside your process: no network, no IPC, no daemon overhead, but also no concurrency and no isolation between the engine and the application. PostgreSQL runs as its own process ecosystem: six daemons run continuously before a single query fires, every query crosses a process boundary — but in return you get MVCC, crash recovery via WAL, a cost-based query planner with buffer-level introspection, and the ability to serve hundreds of clients simultaneously.

---

### When to Use Each

| Use Case                                      | Recommended |
| --------------------------------------------- | ----------- |
| Mobile / embedded apps                        | SQLite3     |
| Local development / prototyping               | SQLite3     |
| Small single-user tools                       | SQLite3     |
| Multi-user web applications                   | PostgreSQL  |
| High-concurrency production systems           | PostgreSQL  |
| Complex queries with large datasets           | PostgreSQL  |
| Situations requiring ACID + concurrent writes | PostgreSQL  |

---

## Commands Reference

### SQLite3

```bash
ls -lh ~/Chinook_Sqlite.sqlite
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA page_size;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA page_count;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA mmap_size;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA mmap_size=30000000;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA journal_mode;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA cache_size;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA integrity_check;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA freelist_count;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA synchronous;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA encoding;"
time sqlite3 ~/Chinook_Sqlite.sqlite "SELECT * FROM Invoice;"
time sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA mmap_size=30000000; SELECT * FROM Invoice;"
sqlite3 ~/Chinook_Sqlite.sqlite <<'EOF'
.timer on
SELECT * FROM Invoice;
EOF
ps aux | grep sqlite
```

### PostgreSQL

```bash
sudo -u postgres psql chinook -c "SHOW block_size;"
sudo -u postgres psql chinook -c "SELECT relpages FROM pg_class WHERE relname = 'invoice';"
sudo -u postgres psql chinook -c "SELECT pg_size_pretty(pg_relation_size('invoice'));"
sudo -u postgres psql chinook -c "SELECT pg_size_pretty(pg_indexes_size('invoice'));"
sudo -u postgres psql chinook -c "SELECT pg_size_pretty(pg_total_relation_size('invoice'));"
sudo -u postgres psql chinook -c "SELECT pg_relation_filepath('invoice');"
sudo -u postgres psql chinook -c "SELECT pg_size_pretty(pg_database_size('chinook'));"
sudo -u postgres psql chinook -c "SHOW shared_buffers;"
sudo -u postgres psql chinook -c "SHOW work_mem;"
sudo -u postgres psql chinook -c "SHOW maintenance_work_mem;"
sudo -u postgres psql chinook -c "SHOW effective_cache_size;"
sudo -u postgres psql chinook -c "EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM invoice;"
sudo -u postgres psql chinook -c "EXPLAIN ANALYZE SELECT billing_country, COUNT(*), ROUND(AVG(total)::numeric, 2) FROM invoice GROUP BY billing_country ORDER BY COUNT(*) DESC;"
time sudo -u postgres psql chinook -c "SELECT * FROM invoice;" > /dev/null
ps aux | grep postgres
```
