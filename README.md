# scaler-Adv-DBMS
# Database Internals Lab — SQLite3 vs PostgreSQL

## Environment
- **OS**: Windows (64-bit)
- **SQLite3 version**: 3.45.3
- **PostgreSQL version**: 16.x
- **Dataset**: `users` table — 10,000 rows (`id`, `name`, `email`, `age`, `created_at`)

## Task 1 — SQLite3 Exploration

### Setup
Run the following in PowerShell to create and populate the sample database:

```powershell
# Create and populate sample database
@"
CREATE TABLE users (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  email TEXT NOT NULL,
  age INTEGER,
  created_at TEXT DEFAULT CURRENT_TIMESTAMP
);
WITH RECURSIVE cnt(x) AS (
  SELECT 1 UNION ALL SELECT x+1 FROM cnt WHERE x < 10000
)
INSERT INTO users (name, email, age)
SELECT 'User_' || x, 'user' || x || '@example.com', (x % 60) + 18
FROM cnt;
"@ | sqlite3 sample.db
```

### File Size

```powershell
Get-Item sample.db | Select-Object Name, @{Name="Size(KB)";Expression={[math]::Round($_.Length / 1KB, 2)}}
```
**Output:**
```
Name      Size(KB)
----      --------
sample.db   600.00
```
The database file is 600 KB for 10,000 rows. SQLite stores everything (schema + data) in a single flat file.

### PRAGMA Commands

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA journal_mode;
PRAGMA cache_size;
```

| PRAGMA | Value |
|--------|-------|
| `page_size` | 4096 |
| `page_count` | 150 |
| `journal_mode` | delete |
| `cache_size` | -2000 |

- **Page size**: 4096 bytes (4 KB) — the default unit of I/O in SQLite.
- **Page count**: 150 — total number of pages = 150 × 4096 = 614,400 bytes ≈ 600 KB, consistent with the file size.
- **cache_size**: -2000 means the cache is limited to approximately 2000 KB (negative value = kilobytes).

### mmap_size Experiments
`mmap_size` tells SQLite how many bytes of the database file to map directly into the process's virtual address space, bypassing `read()` syscalls and letting the OS page cache handle I/O.

```powershell
# Without mmap
sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM users;" > $null

# With mmap (64 MB)
sqlite3 sample.db "PRAGMA mmap_size=67108864; SELECT * FROM users;" > $null

# With mmap (256 MB)
sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;" > $null
```

**Timing Results — `SELECT * FROM users` (10,000 rows)**

| Run | mmap=0 (off) | mmap=64 MB | mmap=256 MB |
|-----|--------------|------------|-------------|
| 1 | 14 ms | 6 ms | 6 ms |
| 2 | 13 ms | 9 ms | 9 ms |
| 3 | 15 ms | 6 ms | 6 ms |
| **Avg** | **~14 ms** | **~7 ms** | **~7 ms** |

**Aggregate Query — `SELECT count(*), avg(age), min(id), max(id) FROM users`**

| Run | mmap=0 | mmap=64 MB | mmap=256 MB |
|-----|--------|------------|-------------|
| 1 | 5 ms | 3 ms | 4 ms |
| 2 | 11 ms | 6 ms | 3 ms |
| 3 | 13 ms | 3 ms | 3 ms |
| **Avg** | **~10 ms** | **~4 ms** | **~3 ms** |

**Observation**: Enabling `mmap_size` roughly halved query execution time. Beyond 64 MB (which already exceeds the 600 KB file), there was no additional gain — the entire file fits in the mapped region either way.

### Process Inspection

```powershell
Get-Process sqlite* -ErrorAction SilentlyContinue
```
*(No sqlite processes currently running)*

SQLite is an in-process library, not a server daemon. There is no background process to find; the library is linked directly into the calling program. The `Get-Process` command shows nothing because once the query finishes, the process exits.

---

## Task 2 — PostgreSQL Setup and Experiments

### Setup

```powershell
# Initialize and start PostgreSQL (Note: typically runs as a service on Windows)
# If using a local data directory instead of the service:
initdb.exe -D "C:\pgdata"
pg_ctl.exe start -D "C:\pgdata"

# Create database and populate table
psql -U postgres -c "CREATE DATABASE labdb;"
psql -U postgres -d labdb -c @"
CREATE TABLE users (
  id SERIAL PRIMARY KEY,
  name TEXT NOT NULL,
  email TEXT NOT NULL,
  age INTEGER,
  created_at TIMESTAMP DEFAULT NOW()
);
INSERT INTO users (name, email, age)
SELECT 'User_' || g, 'user' || g || '@example.com', (g % 60) + 18
FROM generate_series(1, 10000) AS g;
"@
```

### Page Size

```sql
SHOW block_size;
```
```text
 block_size
------------
 8192
```
PostgreSQL uses a block (page) size of 8192 bytes (8 KB) — twice the default SQLite page size. This is a compile-time constant and cannot be changed at runtime.

### Page Count

```sql
ANALYZE users;
SELECT relpages AS page_count FROM pg_class WHERE relname = 'users';
```
```text
 page_count
------------
         94
```
The `users` table spans 94 pages × 8192 bytes = 769,048 bytes ≈ 752 KB of heap storage. `ANALYZE` must be run first to update the catalog statistics.

### Table Size

```sql
SELECT pg_size_pretty(pg_total_relation_size('users')) AS table_size;
```
```text
 table_size
------------
 1024 kB
```
Total size including the primary key index is 1024 KB — larger than raw heap pages because PostgreSQL maintains separate index structures.

### Query Performance

```powershell
# External wall-clock timing in PowerShell
1..3 | ForEach-Object {
  (Measure-Command { psql -U postgres -d labdb -c "SELECT * FROM users;" > $null }).TotalMilliseconds
}
```

| Run | Wall-clock time (client overhead included) |
|-----|--------------------------------------------|
| 1 | 92 ms |
| 2 | 69 ms |
| 3 | 68 ms |

```sql
-- Internal server-side timing (no client overhead)
EXPLAIN ANALYZE SELECT * FROM users;
```
```text
Seq Scan on users  (cost=0.00..194.00 rows=10000 width=45)
                   (actual time=0.004..0.718 rows=10000 loops=1)
Planning Time: 0.144 ms
Execution Time: 0.973 ms
```
**Observation**: The server-side execution time is under 1 ms — the bulk of the wall-clock time (68–92 ms) is client connection overhead (TCP socket setup, authentication, result serialization).

### Key Configuration Values

```sql
SHOW shared_buffers;       -- 128MB
SHOW effective_cache_size; -- 4GB
```
PostgreSQL uses a shared memory buffer pool (`shared_buffers`) shared across all connections. There is no direct `mmap_size` equivalent; PostgreSQL instead relies on the OS page cache controlled by `effective_cache_size` for its query planner decisions.

---

## Task 3 — Comparison Report

### Page Size

| Property | SQLite3 | PostgreSQL |
|----------|---------|------------|
| Default page size | 4096 bytes (4 KB) | 8192 bytes (8 KB) |
| Configurable? | Yes — at `CREATE` time via `PRAGMA page_size` | No — compile-time constant |
| Effect | Smaller pages reduce wasted space for small rows; larger pages improve sequential scan throughput | Larger pages reduce I/O for wide rows and reduce B-tree depth |

SQLite's 4 KB default aligns with typical OS filesystem block sizes, minimising amplification on local SSDs. PostgreSQL's 8 KB default is optimised for server workloads with larger rows and indexes.

### Page Count

| Property | SQLite3 | PostgreSQL |
|----------|---------|------------|
| 10,000-row `users` table | 150 pages (4 KB each = 600 KB) | 94 pages (8 KB each = 752 KB heap, 1024 KB total with index) |
| How to query | `PRAGMA page_count` | `SELECT relpages FROM pg_class` (after `ANALYZE`) |
| Scope | Entire database file | Per-relation (table or index) |

SQLite's single-file design means `page_count` reflects the entire database. PostgreSQL tracks pages at a per-relation level, and the physical files are stored in a directory hierarchy under the data directory.

### Query Performance

| Metric | SQLite3 | PostgreSQL (server-side) | PostgreSQL (with client) |
|--------|---------|--------------------------|--------------------------|
| `SELECT * FROM users` avg | ~14 ms (no mmap), ~7 ms (mmap) | ~1 ms (`EXPLAIN ANALYZE`) | 68–92 ms |
| Bottleneck | File I/O / `read()` syscalls | Client connection overhead | Unix/TCP socket + auth + result serialization |
| Architecture | In-process, no server | Client-server, persistent daemon | |

For identical data, PostgreSQL's internal execution engine is faster (sub-millisecond), but the client-server architecture adds overhead. SQLite is faster end-to-end for embedded/single-process use cases.

### mmap Impact

| Aspect | SQLite3 | PostgreSQL |
|--------|---------|------------|
| mmap mechanism | `PRAGMA mmap_size=N` (bytes) | No direct equivalent; relies on OS page cache via `effective_cache_size` hint |
| Impact observed | ~50% reduction in query time when enabled | N/A — buffer pool (`shared_buffers`) serves a similar purpose |
| How it works | Maps file into virtual address space; avoids `read()` syscalls; OS manages physical pages | Reads blocks into shared buffer pool in SRAM; uses OS cache for data not in `shared_buffers` |
| Recommendation | Enable for read-heavy workloads; set ≥ database file size for full benefit | Tune `shared_buffers` (typically 25% of RAM) and let the OS page cache handle the rest |

### Architecture Summary

| Dimension | SQLite3 | PostgreSQL |
|-----------|---------|------------|
| Architecture | Serverless, embedded library | Client-server daemon |
| Concurrency | Single writer, multiple readers | Full MVCC, many concurrent writers |
| Best fit | Mobile apps, local tools, testing | Web applications, multi-user services |
| Data storage | Single `.db` file | Directory of segment files per table/index |
| Processes visible | None (library in calling process) | `postgres` daemon + per-connection workers |

---

## Commands Reference (PowerShell)

### SQLite3

```powershell
# Setup and run
sqlite3 sample.db
Get-Item sample.db | Select-Object Name, Length
sqlite3 sample.db "PRAGMA page_size;"
sqlite3 sample.db "PRAGMA page_count;"
sqlite3 sample.db "PRAGMA mmap_size=0;"
sqlite3 sample.db "PRAGMA mmap_size=268435456;"
Get-Process sqlite* -ErrorAction SilentlyContinue
```

### PostgreSQL

```powershell
# Setup (if not using Windows service)
initdb.exe -D "C:\pgdata"
pg_ctl.exe start -D "C:\pgdata"

# Database Interaction
psql -U postgres -d labdb
# Inside psql:
# SHOW block_size;
# ANALYZE users;
# SELECT relpages FROM pg_class WHERE relname = 'users';
# SELECT pg_size_pretty(pg_total_relation_size('users'));
# EXPLAIN ANALYZE SELECT * FROM users;
# SHOW shared_buffers;
# SHOW effective_cache_size;
```

