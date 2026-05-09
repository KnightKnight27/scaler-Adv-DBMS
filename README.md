# SQLite3 vs PostgreSQL: Storage and Memory Management

## 1. SQLite3 Experiment

### 1.1 Environment

```bash
sqlite3 --version
```
```
3.51.0 2025-11-04 19:38:17 fb2c931ae597f8d00a37574ff67aeed3eced4e5547f9120744ae4bfa8e74527b (64-bit)
```

### 1.2 Database and Table Setup

A new database file was initialized using the SQLite shell:

```bash
sqlite3 advDbLab.db
```

The following table was defined to hold user records:

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

A few rows were added manually to seed the table:

```sql
INSERT INTO users (name, age) VALUES
('Alice', 21),
('Bob', 22),
('Charlie', 23),
('David', 24),
('Eve', 25);
```

### 1.3 Large Dataset Insertion

A recursive CTE was used to populate the table with 100,000 rows in one operation:

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

This method avoids repeated database round-trips by batching all inserts into a single transaction, which SQLite handles efficiently.

### 1.4 Database File Size

```bash
ls -lh advDbLab.db
```
```
-rw-r--r-- 1 kavya-dhyani kavya-dhyani 2.0M May 9 16:09 advDbLab.db
```

SQLite consolidates all data — including indexes, metadata, and records — into one file on disk. No separate log or auxiliary files are created.

### 1.5 Page Size

```bash
sqlite3 advDbLab.db "PRAGMA page_size;"
```
```
4096
```

SQLite divides its database file into uniformly sized pages. The default is 4096 bytes, which aligns with the typical OS memory page size. This alignment allows the operating system to read and cache SQLite pages with minimal overhead.

### 1.6 Page Count

```bash
sqlite3 advDbLab.db "PRAGMA page_count;"
```
```
0
```

The result of 0 was due to a typo — the PRAGMA was run against a different file (`adbDbLab.db`) rather than the intended one (`advDbLab.db`). Since SQLite silently creates a new empty database when a filename doesn't exist, the result was zero allocated pages.

### 1.7 Memory-Mapped I/O in SQLite

SQLite offers optional memory-mapped I/O via the `mmap_size` PRAGMA. It is off by default:

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

This allocates a 256 MB mmap window. However, opening a new connection shows the setting has been reset:

```bash
sqlite3 advDbLab.db "PRAGMA mmap_size;"
```
```
0
```

> **Key observation:** `mmap_size` is a session-level setting — it applies only to the current connection and is not written to the database file. Every new connection defaults to mmap being off unless the application sets it explicitly. This is intentional, as SQLite is designed to operate correctly in environments where mmap may be unreliable.

When mmap is active, SQLite maps the database file directly into the process's virtual address space. The OS then loads pages on demand (via page faults) rather than through explicit read calls. This eliminates the extra memory copy that a standard `read()` syscall would require, which can improve read throughput for large, read-heavy databases.

### 1.8 Process Architecture

```bash
ps aux | grep sqlite
```

Only a single short-lived process appeared — one that existed only while the shell session was active, with very low memory usage. It terminated when the connection was closed.

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
- **real** — total wall-clock time elapsed
- **user** — CPU time spent in application-level code
- **sys** — CPU time spent on kernel operations like file I/O and memory management

The full table scan completed in under a second with no server, no buffer pool, and no background processes — demonstrating that SQLite's lightweight architecture is well-suited for low-concurrency local workloads.

---

## 2. PostgreSQL Experiment

### 2.1 Starting the Service

PostgreSQL is launched as a background service managed by the system's init daemon:

```bash
sudo systemctl start postgresql
```

Unlike SQLite, a running server is required before any client can connect.

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

PostgreSQL used 15 MB to store the same 100,000 rows that SQLite stored in 2 MB. The larger footprint is expected: PostgreSQL attaches per-row metadata for MVCC support, maintains a SERIAL sequence, and stores WAL segments separately — all of which contribute to additional storage consumption.

### 2.5 Block Size

```sql
SHOW block_size;
```
```
8192
```

PostgreSQL's default block size is 8192 bytes — double that of SQLite. Larger blocks reduce the number of I/O operations needed to scan big tables, since more rows fit in each fetch. The tradeoff is that queries targeting only a few rows may read more data from disk than necessary.

### 2.6 Page Count Estimation

```sql
SELECT relname, relpages
FROM pg_class
WHERE relname = 'users';
```
```
users | 0
```

The `relpages` field showed 0 because PostgreSQL updates catalog statistics asynchronously. The autovacuum process had not yet refreshed this entry after the bulk insert. Running `ANALYZE users;` would update it.

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

> **Key observation:** The variation in COUNT(*) times reflects caching behavior. The first execution had to load data pages from disk into shared buffers. Later runs found those pages already cached and returned results faster. The slower outlier (36 ms) likely reflects background load or partial eviction of shared buffers.

The full SELECT took longer because it had to transfer all 100,000 rows rather than just compute an aggregate.

### 2.8 Process Architecture

```bash
ps aux | grep postgres
```

PostgreSQL operates as a multi-process server. A master `postmaster` process accepts incoming connections and forks a dedicated backend process for each client. Several background workers run continuously to handle checkpointing, WAL writing, dirty page flushing, and autovacuuming.

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

- All data is stored in a single `.db` file, making backup and portability straightforward.
- The 4096-byte page size aligns with OS memory pages, enabling clean and efficient caching.
- Memory-mapped I/O is available but must be enabled per session — it is not a persistent setting.
- SQLite runs entirely within the calling application's process; there is no server to start.
- Memory consumption is minimal because there is no buffer pool, no background writers, and no WAL writer running continuously.

### 3.2 PostgreSQL

- PostgreSQL uses 8192-byte blocks, which suits workloads with large sequential scans.
- Disk usage is higher (15 MB vs 2 MB) due to MVCC metadata, tuple headers, and WAL.
- Repeated queries became faster as data pages were loaded into shared buffers and reused across connections.
- Multiple background processes run at all times, each responsible for a distinct task such as WAL writing, page flushing, or vacuuming.
- PostgreSQL manages its own buffer pool in shared memory rather than relying on mmap.

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

When a database reads a page using a standard `read()` syscall, two data copies occur:

1. The disk controller transfers data from storage into the OS page cache via DMA.
2. The kernel copies that page from the page cache into the application's user-space buffer.

The second copy consumes CPU time and memory bandwidth — a cost that compounds for large databases with frequent page reads.

### How mmap Reduces Overhead

When SQLite activates mmap, it maps the database file into the process's virtual address space. Accessing a mapped address triggers a page fault if the page isn't loaded yet; the OS then brings it into the page cache and makes it directly accessible through the mapped pointer. No second copy is needed.

This removes one layer of memory copying for every read. For read-intensive workloads on large files, this can reduce CPU usage and improve overall throughput. The limitation is that each connection must opt in independently — there is no shared mmap pool across connections in SQLite.

### How PostgreSQL Manages Caching

Rather than using mmap, PostgreSQL allocates a large block of shared memory at startup — called shared buffers — and manages it internally. A buffer manager handles which pages remain in cache and which get evicted, using a clock-sweep algorithm similar to OS page replacement.

All backend processes share this pool. Once one client loads a data page, it stays available in shared buffers for all subsequent clients. This is why repeated COUNT(*) queries ran faster over time — the relevant pages were already in memory after the first execution.

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
