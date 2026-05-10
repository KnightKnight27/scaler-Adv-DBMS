# scaler-Adv-DBMS
# SQLite3 vs PostgreSQL — Storage & Performance Analysis

**Name:** Sarthak midha
**Database:** Custom-generated `users` table - 500,000 rows  
**Platform:** macOS (Darwin 25.0.0), Apple Silicon  

---

## 1. Environment Setup

| Component | Details |
|-----------|---------|
| SQLite version | `sqlite3 --version` → 3.x (system) |
| PostgreSQL version | 17 (Homebrew `/opt/homebrew/opt/postgresql@17`) |
| Sample database | Custom-generated `users` table, 500,000 rows |
| Schema | `id, name, email, age, city, created_at` |

The same schema and row count was used in both databases to ensure a fair comparison.

---

## 2. SQLite3 Experiments

### 2.1 File Size

```bash
ls -lh users.db
```

```
-rw-r--r--@ 1 sarthakmidha  staff    34M 09 May 00:53 users.db
```

**Observation:** The entire database - 500,000 rows across 6 columns - is stored as a single 34MB file on disk. SQLite's file-based architecture means there is no daemon, no shared memory, and no separate data directory.

---

### 2.2 Page Size & Page Count

```bash
sqlite3 users.db "PRAGMA page_size; PRAGMA page_count;"
```

```
4096
8758
```

| Metric | Value |
|--------|-------|
| Page size | 4096 bytes (4 KB) |
| Page count | 8,758 |
| Computed total | 8758 × 4096 = **34.0 MB** ✓ |

**Observation:** SQLite defaults to a 4KB page size, which matches the macOS virtual memory page size. The computed total aligns exactly with the file size observed in `ls -lh`, confirming no fragmentation overhead.

---

### 2.3 mmap_size Experiment

Memory-mapped I/O (`mmap`) allows SQLite to map the database file directly into the process address space, letting the OS kernel handle page faults instead of going through `read()` syscalls.

#### Without mmap (disabled)

```bash
sqlite3 users.db "PRAGMA mmap_size=0;" && time sqlite3 users.db "SELECT * FROM users;" > /dev/null
```

```
0
sqlite3 users.db "SELECT * FROM users;" > /dev/null  0.25s user 0.01s system 95% cpu 0.279 total
```

#### With mmap (256 MB)

```bash
sqlite3 users.db "PRAGMA mmap_size=268435456;" && time sqlite3 users.db "PRAGMA mmap_size=268435456; SELECT * FROM users;" > /dev/null
```

```
268435456
sqlite3 users.db "PRAGMA mmap_size=268435456; SELECT * FROM users;" > /dev/null  0.28s user 0.02s system 81% cpu 0.360 total
```

| Mode | User Time | System Time | CPU % | Total Time |
|------|-----------|-------------|-------|------------|
| mmap disabled | 0.25s | 0.01s | 95% | **0.279s** |
| mmap = 256 MB | 0.28s | 0.02s | 81% | **0.360s** |

**Observation:** mmap was marginally slower in total time (0.360s vs 0.279s) but used noticeably less CPU (81% vs 95%). The total-time increase is explained by the OS page cache: the first query (without mmap) had already warmed the cache, so the second run had no cold I/O to benefit from. The mmap memory-mapping setup added overhead without a corresponding I/O reduction. The CPU drop from 95% → 81% is meaningful — with mmap enabled, the kernel handles page faults transparently, offloading work from the SQLite process. On a cold cache or with a much larger database, mmap would show a more significant speed advantage.

---

### 2.4 Process Inspection

```bash
sqlite3 users.db "SELECT * FROM users;" > /dev/null &
ps aux | grep sqlite
```

```
pratyushmohanty  33652   0.0  0.0 435299904   1344 s013  S+    1:15AM   0:00.01 grep sqlite
```

**Observation:** The sqlite3 process completed in ~0.3s — too fast to capture with `ps aux` in a separate terminal. Only the `grep` process itself appeared. This highlights SQLite's single-process, in-process architecture: there is no persistent daemon. The database process starts, executes the query, and exits immediately.

---

## 3. PostgreSQL Experiments

### 3.1 Database & Table Size

```bash
psql -d postgres -c "SELECT pg_size_pretty(pg_database_size('postgres')) AS db_size, pg_size_pretty(pg_relation_size('users')) AS table_size;"
```

```
 db_size | table_size
---------+------------
 60 MB   | 41 MB
(1 row)
```

**Observation:** The `users` table itself is 41MB, while the total database is 60MB. The extra ~19MB accounts for system catalogs, indexes, TOAST tables, and PostgreSQL's internal metadata — overhead that doesn't exist in SQLite's single-file model.

---

### 3.2 Page Size & Page Count

```bash
psql -d postgres -c "SHOW block_size;"
psql -d postgres -c "SELECT relname, relpages FROM pg_class WHERE relname = 'users';"
```

```
 block_size
------------
 8192
(1 row)

 relname | relpages
---------+----------
 users   |     5286
(1 row)
```

| Metric | Value |
|--------|-------|
| Block (page) size | 8192 bytes (8 KB) |
| Page count | 5,286 |
| Computed total | 5286 × 8192 = **43.3 MB** (~41 MB reported) |

**Observation:** PostgreSQL uses 8KB blocks by default — double SQLite's 4KB pages. This means fewer pages are needed for the same data (5,286 vs 8,758), which reduces B-tree depth and can improve index performance. The slight difference between computed (43.3MB) and reported (41MB) is normal: `pg_relation_size` measures allocated heap pages while some blocks may be partially filled.

---

### 3.3 Query Timing

#### Shell-level timing

```bash
time psql -d postgres -c "SELECT * FROM users;" > /dev/null
```

```
psql -d postgres -c "SELECT * FROM users;" > /dev/null  1.19s user 0.05s system 91% cpu 1.365 total
```

#### Server-level timing (EXPLAIN ANALYZE — warm cache)

```bash
psql -d postgres -c "EXPLAIN ANALYZE SELECT * FROM users;" > /dev/null
psql -d postgres -c "EXPLAIN ANALYZE SELECT * FROM users;"
```

```
                                                  QUERY PLAN
---------------------------------------------------------------------------------------------------------------
 Seq Scan on users  (cost=0.00..10286.00 rows=500000 width=51) (actual time=0.005..24.317 rows=500000 loops=1)
 Planning Time: 0.200 ms
 Execution Time: 38.198 ms
(3 rows)
```

| Metric | Value |
|--------|-------|
| Shell total time | 1.365s |
| Server execution time | 38.198 ms |
| Planning time | 0.200 ms |
| Scan type | Sequential Scan |

**Observation:** The gap between shell time (1.365s) and server execution time (38ms) reflects PostgreSQL's client-server architecture. The 1.365s includes: psql process startup, TCP/Unix socket connection, query serialization, transfer of 500,000 rows from server to client, and output formatting. The actual database engine took only 38ms to scan all 500,000 rows — significantly faster than the wall-clock time suggests. The sequential scan is expected since there is no `WHERE` clause and no index to leverage.

---

### 3.4 Shared Buffers (mmap Equivalent)

```bash
psql -d postgres -c "SHOW shared_buffers;"
```

```
 shared_buffers
----------------
 128MB
(1 row)
```

**Observation:** PostgreSQL does not expose `mmap` as a user-facing toggle. Instead, it maintains its own buffer pool (`shared_buffers`, set to 128MB here) — a shared memory region across all connections where frequently accessed pages are cached. PostgreSQL also benefits from the OS page cache on top of this. For our 41MB table, the entire table fits within `shared_buffers`, which is why the warm-cache query (EXPLAIN ANALYZE) completed in just 38ms.

---

### 3.5 Process Inspection

```bash
ps aux | grep postgres
```

```
pratyushmohanty  33174  ... postgres: pratyushmohanty postgres [local] idle
pratyushmohanty  32001  ... postgres: logical replication launcher
pratyushmohanty  32000  ... postgres: autovacuum launcher
pratyushmohanty  31999  ... postgres: walwriter
pratyushmohanty  31997  ... postgres: background writer
pratyushmohanty  31996  ... postgres: checkpointer
pratyushmohanty  31993  ... /opt/homebrew/opt/postgresql@17/bin/postgres -D /opt/homebrew/var/postgresql@17
```

**Observation:** PostgreSQL runs as a persistent multi-process daemon. Even when idle, 7 processes are active: the main postmaster, a checkpointer (flushes dirty pages to disk), a background writer (proactively writes pages), a WAL writer (Write-Ahead Log for crash recovery), an autovacuum launcher (reclaims dead tuples), a logical replication launcher, and one idle client connection. This is the fundamental architectural difference from SQLite — PostgreSQL is always running and manages shared resources across concurrent clients.

---

## 4. Comparison Analysis

### 4.1 Side-by-Side Metrics

| Metric | SQLite3 | PostgreSQL |
|--------|---------|------------|
| Page / Block size | 4,096 bytes (4 KB) | 8,192 bytes (8 KB) |
| Page count (500k rows) | 8,758 | 5,286 |
| Table size on disk | 34 MB | 41 MB |
| Query time — shell | 0.279s | 1.365s |
| Query time — engine only | ~0.279s (no separation) | 38.198 ms |
| mmap / cache mechanism | `PRAGMA mmap_size` (explicit) | `shared_buffers` + OS cache |
| Process model | Single process, no daemon | Multi-process daemon (7+ processes) |
| Storage model | Single `.db` file | Data directory with many files |

### 4.2 Page Size

SQLite uses 4KB pages and PostgreSQL uses 8KB blocks. The larger block size in PostgreSQL means each I/O operation brings more data into memory, which is advantageous for sequential scans on wide tables. SQLite's 4KB default aligns with the OS virtual memory page, making its mmap implementation a natural fit.

### 4.3 Page Count & Storage Efficiency

Despite identical row counts, PostgreSQL's table is larger on disk (41MB vs 34MB). PostgreSQL stores additional metadata per row (tuple header, visibility information for MVCC), which SQLite does not. SQLite's format is more compact for simple read-heavy workloads without concurrent writers.

### 4.4 Query Performance

Comparing raw shell times (0.279s SQLite vs 1.365s PostgreSQL) is misleading — the PostgreSQL number includes client-server communication and data transfer for 500,000 rows. The server-side execution time of 38ms shows PostgreSQL's engine is faster at the query level, benefiting from its larger block size and buffer pool. SQLite's 0.279s covers the entire operation (process start, I/O, formatting) as there is no client-server separation.

### 4.5 mmap vs shared_buffers

SQLite's `mmap_size` is a transparent I/O optimization: when enabled, the OS maps the database file into the process address space, and page faults replace `read()` syscalls. In this experiment, mmap showed reduced CPU usage (81% vs 95%) but no total-time improvement because the OS page cache had already warmed the data. On a cold cache with a large database, mmap provides measurable speedup.

PostgreSQL does not use mmap in the same sense. Its `shared_buffers` is a dedicated in-memory buffer pool shared across all connections, managed entirely by PostgreSQL. This gives it fine-grained control over eviction and dirty-page management — essential for a multi-user system — at the cost of requiring pre-allocated memory even when idle.

---

## 5. Conclusion

SQLite and PostgreSQL solve different problems. SQLite is optimized for simplicity and low overhead: a single file, no daemon, and direct process access to data. It is ideal for local, single-user, or embedded workloads. PostgreSQL is architected for concurrency, durability, and performance at scale: a persistent daemon with dedicated background workers, a shared buffer pool, WAL-based crash recovery, and MVCC. The trade-off is visible in the numbers — PostgreSQL's 38ms server-side execution against SQLite's 0.279s end-to-end time reflects not a performance gap but a fundamental difference in architecture. For the same 500,000-row sequential scan, PostgreSQL's engine is faster internally, but its client-server boundary adds latency that SQLite, as an in-process library, never incurs.
