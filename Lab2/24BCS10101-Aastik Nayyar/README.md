# SQLite3 vs PostgreSQL: Storage and Memory Management

## Table of Contents
- [1. SQLite3 Experiment](#1-sqlite3-experiment)
- [2. PostgreSQL Experiment](#2-postgresql-experiment)
- [3. Observations](#3-observations)
- [4. Comparison Table](#4-comparison-table)
- [5. Analysis of mmap and Caching](#5-analysis-of-mmap-and-caching)
- [6. Process Architecture](#6-process-architecture)
- [7. Performance Analysis](#7-performance-analysis)

---

## 1. SQLite3 Experiment

### 1.1 Environment

```bash
sqlite3 --version
```

```
3.51.0 2025-11-04 19:38:17 fb2c931ae597f8d00a37574ff67aeed3eced4e5547f9120744ae4bfa8e74527b (64-bit)
```

### 1.2 Database and Table Setup

A new database file was created using the SQLite shell:

```bash
sqlite3 advDbLab.db
```

The following table was defined to store user records:

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

A small set of rows was manually inserted to seed the table:

```sql
INSERT INTO users (name, age) VALUES
('Alice', 21),
('Bob', 22),
('Charlie', 23),
('David', 24),
('Eve', 25);
```

### 1.3 Large Dataset Insertion

A recursive CTE was used to insert 100,000 rows in a single operation:

```sql
WITH RECURSIVE cnt(x) AS (
    SELECT 1
    UNION ALL
    SELECT x+1 FROM cnt LIMIT 100000
)
INSERT INTO users(name, age)
SELECT 'User' || x, 20 + (x % 10)
FROM cnt;
```

This approach avoids repeated round-trips to the database by batching all inserts into one transaction, which SQLite processes efficiently.

### 1.4 Database File Size

```bash
ls -lh advDbLab.db
```

```
-rw-r--r-- 1 aastik aastik 2.0M May 9 16:09 advDbLab.db
```

SQLite stores everything — records, indexes, and metadata — within a single file on disk. No separate log files or auxiliary files are generated.

### 1.5 Page Size

```bash
sqlite3 advDbLab.db "PRAGMA page_size;"
```

```
4096
```

SQLite organizes its database file into fixed-size pages. The default page size is 4096 bytes, which matches the typical OS memory page size. This alignment lets the operating system read and cache SQLite pages with minimal overhead.

### 1.6 Page Count

```bash
sqlite3 advDbLab.db "PRAGMA page_count;"
```

```
0
```

The output of 0 resulted from a typo — the PRAGMA was executed against `adbDbLab.db` instead of `advDbLab.db`. Since SQLite silently creates a new empty database when a file does not exist, the page count returned was zero.

### 1.7 Memory-Mapped I/O in SQLite

SQLite supports optional memory-mapped I/O via the `mmap_size` PRAGMA, but it is disabled by default:

```bash
sqlite3 advDbLab.db "PRAGMA mmap_size;"
```

```
0
```

To enable it for the current session:

```bash
sqlite3 advDbLab.db "PRAGMA mmap_size=268435456;"
```

```
268435456
```

This sets a 256 MB mmap window. However, opening a fresh connection shows the setting has been reset:

```bash
sqlite3 advDbLab.db "PRAGMA mmap_size;"
```

```
0
```

> **Key observation:** `mmap_size` is a session-scoped setting — it applies only to the active connection and is not persisted to the database file. Every new connection starts with mmap disabled unless the application explicitly enables it. This is by design, as SQLite is built to function correctly in environments where mmap may be unreliable.

When mmap is enabled, SQLite maps the database file directly into the process's virtual address space. The OS then loads pages on demand through page faults rather than explicit `read()` calls. This eliminates the extra memory copy that a standard syscall would introduce, which can improve read throughput for large, read-heavy databases.

### 1.8 Process Architecture

```bash
ps aux | grep sqlite
```

Only a single short-lived process appeared — one that existed only for the duration of the shell session, with very low memory usage. It exited as soon as the connection was closed.

This reflects SQLite's embedded design. The SQLite library is linked directly into the calling application and runs within the same process. There is no server daemon, no socket, and no separate process to manage.

### 1.9 Query Timing

```bash
time sqlite3 advDbLab.db "SELECT * FROM users;"
```

```
real    0m0.820s
user    0m0.146s
sys     0m0.332s
```

The three time values represent:

| Field | Meaning |
|-------|---------|
| `real` | Total elapsed wall-clock time |
| `user` | CPU time spent in application-level code |
| `sys` | CPU time spent on kernel operations such as file I/O and memory management |

The full table scan completed in under a second with no server, no buffer pool, and no background processes — illustrating that SQLite's lightweight architecture is well-suited for low-concurrency local workloads.

---

## 2. PostgreSQL Experiment

### 2.1 Starting the Service

PostgreSQL is started as a background service managed by the system's init daemon:

```bash
sudo systemctl start postgresql
```

Unlike SQLite, a running server must be available before any client can connect.

### 2.2 Connecting and Setting Up

```bash
sudo -u postgres psql
```

```sql
CREATE DATABASE oslab_pg;
\c oslab_pg
```

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

### 2.3 Large Dataset Insertion

PostgreSQL's built-in `generate_series` function was used for bulk insertion:

```sql
INSERT INTO users(name, age)
SELECT
    'User' || g,
    20 + (g % 10)
FROM generate_series(1, 100000) AS g;
```

### 2.4 Database Size

```sql
SELECT pg_size_pretty(pg_database_size('oslab_pg'));
```

```
15 MB
```

PostgreSQL consumed 15 MB to store the same 100,000 rows that SQLite stored in 2 MB. The larger footprint is expected: PostgreSQL attaches per-row metadata for MVCC support, maintains a SERIAL sequence, and stores WAL segments separately — all of which contribute to the additional storage.

### 2.5 Block Size

```sql
SHOW block_size;
```

```
8192
```

PostgreSQL's default block size is 8192 bytes — twice that of SQLite. Larger blocks reduce the number of I/O operations needed when scanning large tables, since more rows fit into each read. The tradeoff is that queries targeting only a few rows may pull more data from disk than required.

### 2.6 Page Count Estimation

```sql
SELECT relname, relpages
FROM pg_class
WHERE relname = 'users';
```

```
users | 0
```

The `relpages` value showed 0 because PostgreSQL refreshes catalog statistics asynchronously. The autovacuum process had not yet updated this entry following the bulk insert. Running `ANALYZE users;` would force it to update.

### 2.7 Query Timing

```sql
\timing
```

Results for `SELECT COUNT(*) FROM users`:

```
Time: 10.798 ms
Time: 36.262 ms
Time: 8.974 ms
Time: 12.424 ms
```

Result for `SELECT * FROM users`:

```
Time: 54.898 ms
```

> **Key observation:** The variation in COUNT(*) times reflects caching behavior. The first execution needed to load data pages from disk into shared buffers. Subsequent runs found those pages already in cache and returned results more quickly. The slower outlier (36 ms) likely reflects brief background activity or partial eviction of shared buffers.

The full SELECT took longer because all 100,000 rows had to be transferred rather than just computing an aggregate.

### 2.8 Process Architecture

```bash
ps aux | grep postgres
```

PostgreSQL operates as a multi-process server. A master `postmaster` process listens for incoming connections and forks a dedicated backend process for each client. Several background workers run continuously to handle checkpointing, WAL writing, dirty page flushing, and autovacuuming.

```
postgres  386672 ... /usr/lib/postgresql/16/bin/postgres ...
postgres  386676 ... postgres: 16/main: checkpointer
postgres  386677 ... postgres: 16/main: background writer
postgres  386679 ... postgres: 16/main: walwriter
postgres  386680 ... postgres: 16/main: autovacuum launcher
postgres  386681 ... postgres: 16/main: logical replication launcher
postgres  389273 ... postgres: 16/main: postgres oslab_pg [local] idle
```

---

## 3. Observations

### 3.1 SQLite

- All data resides in a single `.db` file, making backup and portability straightforward.
- The 4096-byte page size aligns with OS memory pages, enabling clean and efficient caching.
- Memory-mapped I/O is available but must be enabled per session — it is not a persistent configuration.
- SQLite runs entirely within the calling application's process; no server needs to be started.
- Memory consumption stays minimal because there is no buffer pool, no background writers, and no WAL writer running continuously.

### 3.2 PostgreSQL

- PostgreSQL uses 8192-byte blocks, well-suited for workloads involving large sequential scans.
- Disk usage is significantly higher (15 MB vs 2 MB) due to MVCC metadata, tuple headers, and WAL.
- Repeated queries grew faster as data pages were loaded into shared buffers and reused across connections.
- Multiple background processes run at all times, each responsible for a distinct task such as WAL writing, page flushing, or vacuuming.
- PostgreSQL manages its own buffer pool in shared memory rather than depending on mmap.

---

## 4. Comparison Table

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | Embedded library, no server | Client-server, separate server process |
| Storage Model | Single `.db` file | Directory of files per database cluster |
| Page Size | 4096 bytes | 8192 bytes |
| File Size (100k rows) | 2.0 MB | 15 MB |
| mmap Support | Yes, session-specific and opt-in | Not used; uses shared buffer pool |
| Process Model | Single process (embedded in app) | Multiple background + per-client processes |
| Shared Memory | Not used | Shared buffers across all backends |
| Concurrency | File-level locking, limited | MVCC with row-level locking |
| WAL / Crash Recovery | Available but optional | Always enabled by default |
| Performance | Fast for local, low-concurrency workloads | Scales to high concurrency and large data |
| Suitable Use Cases | Mobile apps, embedded tools, local utilities | Web backends, enterprise systems, multi-user apps |

---

## 5. Analysis of mmap and Caching

### Normal File I/O

When a database reads a page using a standard `read()` syscall, two data copies take place:

1. The disk controller transfers data from storage into the OS page cache via DMA.
2. The kernel copies that page from the page cache into the application's user-space buffer.

The second copy consumes CPU time and memory bandwidth — a cost that compounds for large databases with frequent page reads.

### How mmap Reduces Overhead

When SQLite activates mmap, it maps the database file directly into the process's virtual address space. Accessing a mapped address triggers a page fault if the page isn't loaded yet; the OS then brings it into the page cache and makes it directly accessible through the mapped pointer. No second copy is needed.

This removes one layer of memory copying for every read. For read-intensive workloads on large files, this can reduce CPU usage and improve overall throughput. The limitation is that each connection must opt in independently — there is no shared mmap pool across connections in SQLite.

### How PostgreSQL Manages Caching

Rather than using mmap, PostgreSQL allocates a large block of shared memory at startup — called shared buffers — and manages it internally. A buffer manager handles which pages remain in cache and which get evicted, using a clock-sweep algorithm similar to OS page replacement.

All backend processes share this pool. Once one client loads a data page, it stays available in shared buffers for all subsequent clients. This is why repeated `COUNT(*)` queries ran faster over time — the relevant pages were already in memory after the first execution.

An additional advantage is that PostgreSQL can apply query-level knowledge to caching decisions. It can, for instance, prevent large sequential scan data from displacing frequently-accessed index pages — something the OS page cache cannot do on its own.

---

## 6. Process Architecture

### SQLite — Embedded, Single Process

SQLite is a C library that gets linked into the application. No daemon, socket, or server process is involved. All database operations run in the same thread and process as the calling code.

```
Application Process
    |
    +-- SQLite Library (linked in)
    |       |
    |       +-- reads/writes advDbLab.db directly
    |
    +-- Application code
```

The tradeoff is that only one writer can hold the file lock at a time, making SQLite unsuitable for applications requiring many concurrent writes.

### PostgreSQL — Client-Server, Multi-Process

PostgreSQL uses a forking model. The `postmaster` process listens for connections and spawns a new backend process for each client. All backends share a common block of shared memory, coordinated by the buffer manager and lock manager.

```
postmaster (main process)
    |
    +-- shared memory (shared buffers, lock tables, etc.)
    |
    +-- backend (client 1)
    +-- backend (client 2)
    +-- checkpointer
    +-- background writer
    +-- walwriter
    +-- autovacuum launcher
```

This architecture supports true concurrent read/write access. MVCC ensures readers and writers do not block each other by retaining multiple versions of modified rows.

The cost is a higher baseline resource footprint: even idle, PostgreSQL holds shared memory and runs several background workers.

---

## 7. Performance Analysis

### File Size Difference

The same 100,000-row dataset occupied 2.0 MB in SQLite and 15 MB in PostgreSQL. Contributing factors include:

- PostgreSQL uses 8192-byte pages versus SQLite's 4096-byte pages.
- Each PostgreSQL row carries MVCC metadata and a tuple header for concurrency support.
- WAL segments, system catalog entries, and the SERIAL sequence add further overhead.
- SQLite's simpler file layout has inherently lower storage overhead.

For constrained environments such as mobile apps or embedded systems, SQLite's compact storage is a meaningful advantage.

### Query Timing Summary

| Database | Query | Time |
|---|---|---|
| SQLite3 | `SELECT * FROM users;` | real: 0.820s |
| PostgreSQL | `SELECT COUNT(*) FROM users;` | 10–36 ms |
| PostgreSQL | `SELECT * FROM users;` | 54 ms |

### Performance Characteristics

| Aspect | SQLite | PostgreSQL |
|---|---|---|
| Query overhead | Minimal — no network or IPC | Higher — client-server communication |
| Caching | Relies on OS page cache | Manages its own shared buffer pool |
| Concurrency overhead | None (single writer) | Coordination across multiple processes |
| Best workload | Lightweight, single-user, local access | Concurrent, multi-user, high-throughput |

SQLite incurs less architectural overhead because all operations happen inside one process with no IPC, no socket, and no shared memory coordination. PostgreSQL adds overhead from its server processes and concurrency mechanisms, but this is the necessary cost of supporting many simultaneous users reliably.

### When Each System Is More Appropriate

**SQLite works better when:**
- Only one writer is active at a time
- The dataset fits mostly within the OS page cache
- Minimal per-query latency is needed with no network round-trip
- No server setup is feasible or desired

**PostgreSQL works better when:**
- Many clients read and write at the same time
- The dataset is large enough to require a managed buffer pool
- Strong crash recovery guarantees are necessary
- Complex query planning, joins, and high-throughput access are required