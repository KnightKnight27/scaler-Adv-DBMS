# SQLite3 vs PostgreSQL Storage and Memory Management Analysis

## 1. SQLite3 Experiment

### 1.1 Environment

```bash
sqlite3 --version
```

```
3.51.0 2025-11-04 19:38:17 fb2c931ae597f8d00a37574ff67aeed3eced4e5547f9120744ae4bfa8e74527b (64-bit)
```

### 1.2 Database and Table Setup

A new database file was created using the command-line shell:

```bash
sqlite3 advDbLab.db
```

A table was created to store user records:

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

A small set of rows was inserted manually:

```sql
INSERT INTO users (name, age) VALUES
('Alice', 21),
('Bob', 22),
('Charlie', 23),
('David', 24),
('Eve', 25);
```

### 1.3 Large Dataset Insertion

A recursive CTE was used to insert 100,000 rows efficiently in a single statement:

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

This approach avoids repeated round-trips to the database and lets SQLite batch the inserts
within a single transaction.

### 1.4 Database File Size

```bash
ls -lh advDbLab.db
```

```
-rw-r--r-- 1 kavya-dhyani kavya-dhyani 2.0M May 9 16:09 advDbLab.db
```

SQLite stores the entire database in a single file on disk. There are no separate files for
indexes, logs, or metadata. Everything lives in `advDbLab.db`.

### 1.5 Page Size

```bash
sqlite3 advDbLab.db "PRAGMA page_size;"
```

```
4096
```

SQLite organizes its database file into fixed-size pages. The default page size is 4096 bytes,
which matches the default page size used by most modern operating systems. This alignment means
SQLite pages map cleanly onto OS pages, which reduces overhead when the OS reads or caches the
file.

### 1.6 Page Count

```bash
sqlite3 advDbLab.db "PRAGMA page_count;"
```

```
0
```

The `page_count` returned 0 because the PRAGMA command was accidentally executed on a different
database file (`adbDbLab.db`) instead of the populated database (`advDbLab.db`). SQLite
automatically creates a new empty database file if the specified file does not already exist,
which resulted in an empty database with zero allocated pages.

### 1.7 Memory-Mapped I/O in SQLite

SQLite supports optional memory-mapped I/O through the `mmap_size` PRAGMA. By default, mmap
is disabled:

```bash
sqlite3 advDbLab.db "PRAGMA mmap_size;"
```

```
0
```

Enabling mmap for the current session:

```bash
sqlite3 advDbLab.db "PRAGMA mmap_size=268435456;"
```

```
268435456
```

This sets a 256 MB mmap window for the current connection. However, checking mmap in a new
connection shows it has reverted:

```bash
sqlite3 advDbLab.db "PRAGMA mmap_size;"
```

```
0
```

**Key observation:** `mmap_size` is a per-connection, per-session setting. It is not stored
permanently in the database file. Each new connection starts with mmap disabled unless the
application explicitly sets it. This is by design -- SQLite is intended to work correctly in
environments where mmap may not be available or reliable.

When mmap is enabled, SQLite maps the database file into the process's virtual address space.
The OS then uses demand paging to load file pages only when they are accessed. This removes
the second copy (from page cache to user buffer) that normal `read()` calls require. For
read-heavy workloads on large databases, this can improve throughput noticeably.

### 1.8 Process Architecture

```bash
ps aux | grep sqlite
```

Observation: SQLite showed a single, short-lived process that existed only while the shell
was open. The process had minimal memory usage and disappeared when the connection closed.

This reflects SQLite's embedded architecture. The library runs inside the calling application's
process. There is no separate server process. The application links against the SQLite library
and all database operations happen within the same process memory space.

### 1.9 Query Timing

```bash
time sqlite3 advDbLab.db "SELECT * FROM users;"
```

```
real    0m0.820s
user    0m0.146s
sys     0m0.332s
```

These three values measure different aspects of the time taken:

- `real` -- total elapsed time from start to finish, including any waiting
- `user` -- CPU time spent executing application-level code
- `sys` -- CPU time spent inside kernel calls such as file reads and memory operations

SQLite completed the full table scan in under one second despite having no server, no buffer
pool, and no background processes. This demonstrates that its lightweight embedded architecture
is competitive for single-user local workloads.

---

## 2. PostgreSQL Experiment

### 2.1 Starting the Service

PostgreSQL runs as a background service managed by the OS init system:

```bash
sudo systemctl start postgresql
```

Unlike SQLite, PostgreSQL must be running as a server before any client can connect to it.

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

PostgreSQL provides a built-in `generate_series` function that makes bulk inserts
straightforward:

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

PostgreSQL reports 15 MB for the same 100,000 rows that SQLite stored in 2 MB. The larger
size is expected because PostgreSQL stores additional metadata per row (called a tuple header),
maintains visibility information for MVCC (multi-version concurrency control), and stores
supporting structures like the SERIAL sequence and WAL (write-ahead log) segments separately.

### 2.5 Block Size

```sql
SHOW block_size;
```

```
8192
```

PostgreSQL uses a default block (page) size of 8192 bytes, which is twice the size of
SQLite's default page. Larger pages reduce the number of I/O operations needed to read large
tables because more rows fit in a single page fetch. However, if most queries only need a few
rows, larger pages can also mean more data is read from disk than is strictly necessary.

### 2.6 Page Count Estimation

```sql
SELECT relname, relpages
FROM pg_class
WHERE relname = 'users';
```

```
users | 0
```

The `relpages` value was 0 because PostgreSQL updates statistics asynchronously. The catalog
entry had not yet been updated by autovacuum after the bulk insert. Running `ANALYZE users;`
would refresh this value.

### 2.7 Query Timing

Timing was enabled to measure query execution time:

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

**Key observation:** The variation in `COUNT(*)` times reflects the effect of caching. The
first run had to read data pages from disk into shared buffers. Subsequent runs found the
pages already in shared buffers and returned results faster. The slower run (36 ms) likely
occurred when the system was under background load or when shared buffers were partially
evicted.

The `SELECT *` query took longer because it had to transfer all 100,000 rows of data across
the result set, not just count them.

### 2.8 Process Architecture

```bash
ps aux | grep postgres
```

Unlike SQLite, PostgreSQL runs as a server with multiple background processes responsible for
memory management, disk writes, logging, and concurrency control. This architecture allows
PostgreSQL to support multiple concurrent users efficiently.

```
postgres  386672  0.0  0.1 225464 31212 ?        Ss   16:18   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
postgres  386676  0.0  0.0 225596 12216 ?        Ss   16:18   0:00 postgres: 16/main: checkpointer 
postgres  386677  0.0  0.0 225620  7916 ?        Ss   16:18   0:00 postgres: 16/main: background writer 
postgres  386679  0.0  0.0 225464 10396 ?        Ss   16:18   0:00 postgres: 16/main: walwriter 
postgres  386680  0.0  0.0 227072  8860 ?        Ss   16:18   0:00 postgres: 16/main: autovacuum launcher 
postgres  386681  0.0  0.0 227048  8140 ?        Ss   16:18   0:00 postgres: 16/main: logical replication launcher 
root      387573  0.1  0.0  19820  7632 pts/0    S+   16:18   0:00 sudo -u postgres psql
root      387574  0.0  0.0  19820  2644 pts/2    Ss   16:18   0:00 sudo -u postgres psql
postgres  387575  0.0  0.0  28684 12004 pts/2    S+   16:18   0:00 /usr/lib/postgresql/16/bin/psql
postgres  389273  0.2  0.2 228352 34104 ?        Ss   16:19   0:00 postgres: 16/main: postgres oslab_pg [local] idle
kavya-d+  405463  0.0  0.0   9148  2272 pts/1    S+   16:23   0:00 grep --color=auto postgres
```

---

## 3. Observations

### 3.1 SQLite Observations

- The entire database is stored in a single file (`advDbLab.db`), making it trivial to
  copy, move, or back up.
- The default page size of 4096 bytes matches the OS page size, allowing clean alignment
  between SQLite pages and OS memory pages.
- mmap support exists but is session-specific. It must be enabled explicitly per connection
  and is not persisted.
- SQLite runs as a single process with no background workers. There is no server to start
  or stop.
- Memory usage is minimal because there is no buffer pool, no background writers, and no
  WAL writer running at all times.

### 3.2 PostgreSQL Observations

- PostgreSQL uses 8192-byte blocks, double the size of SQLite pages, which suits workloads
  involving large sequential scans.
- Database size on disk is larger (15 MB vs 2 MB for the same data) due to MVCC metadata,
  tuple headers, and WAL overhead.
- Query times for repeated queries decreased due to shared buffer caching. Once data pages
  are loaded into shared buffers, subsequent queries do not need to go to disk.
- Multiple background processes run continuously, each handling a specific OS-level task
  such as WAL writing, dirty page flushing, and dead tuple removal.
- PostgreSQL does not rely on memory-mapped I/O the same way SQLite does. It manages its
  own buffer pool in shared memory and coordinates access across processes through its
  buffer manager.

---

## 4. Comparison Table

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | Embedded library, no server | Client-server, server runs separately |
| Storage Model | Single `.db` file on disk | Directory of files per database cluster |
| Page Size | 4096 bytes (default) | 8192 bytes (default) |
| File Size (100k rows) | 2.0 MB | 15 MB |
| mmap Support | Yes, session-specific, opt-in | Not used directly; uses shared buffers |
| Process Model | Single process (embedded in app) | Multiple background + per-client processes |
| Shared Memory | Not used | Shared buffers shared across all backends |
| Concurrency | File-level locking, limited | MVCC, row-level locking, high concurrency |
| WAL / Crash Recovery | Available but optional | Always enabled by default |
| Performance | Fast for local, low-concurrency workloads | Scales to high concurrency and large data |
| Suitable Use Cases | Mobile apps, local tools, embedded systems | Web backends, enterprise, multi-user systems |

---

## 5. Analysis of mmap and Caching

### How Normal File I/O Works

When a database reads a page from disk using a regular `read()` system call, two copies occur:

1. The disk controller uses DMA to transfer the data from storage into a page in the OS page
   cache (kernel RAM).
2. The kernel copies that page from the page cache into the application's own buffer in user
   memory.

This second copy costs CPU time and memory bandwidth, especially for large databases where many
pages are read frequently.

### How mmap Reduces This Cost

When SQLite enables mmap, it calls `mmap()` on the database file. The OS maps the file's pages
directly into the process's virtual address space. When the application accesses a mapped
address, the OS uses demand paging: if the page is not yet loaded, a page fault occurs and the
kernel brings the page into the page cache and maps it into the process's address space. After
this, the application reads the data without any additional copy.

The result is that mmap removes the second copy entirely. The data goes from disk to the page
cache (via DMA) and is immediately accessible to the application through the mapped address.
For read-heavy workloads, this can meaningfully reduce CPU usage and improve throughput.

The limitation is that mmap in SQLite is session-specific. There is no global pool shared
across connections. Each connection that wants mmap must enable it separately.

### How PostgreSQL Handles Caching

PostgreSQL does not use mmap for its data pages. Instead, it allocates a large block of shared
memory at startup, called shared buffers, and manages it internally. The buffer manager decides
which pages to keep in shared buffers and which to evict when space runs low, using a
clock-sweep algorithm similar to the OS page replacement policy.

All backend processes (one per client connection) share access to this pool. When one client
reads a data page, that page stays in shared buffers. If another client needs the same page
shortly after, it is already there. This is why repeated `SELECT COUNT(*) FROM users` queries
became faster over successive runs -- the pages were already loaded into shared buffers after
the first run.

The advantage of managing its own buffer pool is that PostgreSQL can apply database-specific
knowledge to caching decisions. For example, it knows which pages are part of a large
sequential scan (which should not pollute the cache) versus which pages are part of an index
that is accessed repeatedly (which should be kept in cache).

---

## 6. Process Architecture Comparison

### SQLite - Embedded, Single Process

SQLite is a C library that an application links against. There is no daemon, no network socket,
and no server process. When the application calls a SQLite function, the library code runs in
the same thread, in the same process, and has direct access to the process's memory.

```
Application Process
    |
    +-- SQLite Library (linked in)
    |       |
    |       +-- reads/writes advDbLab.db directly
    |
    +-- Application code
```

This design makes SQLite extremely lightweight. The tradeoff is that only one writer can access
the database at a time (file-level write lock), making it unsuitable for applications with many
concurrent writers.

### PostgreSQL - Client-Server, Multi-Process

PostgreSQL uses a forking model. A master process called `postmaster` listens for incoming
connections. When a client connects, `postmaster` forks a new backend process dedicated to that
client. All backend processes share a common block of shared memory (shared buffers) and
communicate through it under the coordination of the buffer manager and lock manager.

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

This architecture supports true concurrent read and write access from many clients at the same
time. MVCC ensures that readers do not block writers and writers do not block readers by keeping
multiple versions of rows in the data pages.

The cost is higher baseline resource usage: even with no clients connected, PostgreSQL runs
several background processes and holds a large shared memory allocation.

---

## 7. Performance Analysis

### File Size Difference

The same dataset (100,000 rows) consumed 2.0 MB in SQLite and 15 MB in PostgreSQL. This
difference comes from several factors:

- PostgreSQL uses 8192-byte pages, while SQLite uses 4096-byte pages.
- PostgreSQL stores additional MVCC metadata and tuple headers for concurrency control.
- PostgreSQL maintains WAL (Write-Ahead Logging), indexes, and system catalog information.
- SQLite uses a simpler file-based architecture with lower storage overhead.

For applications where disk space is limited, such as embedded systems or mobile devices,
SQLite's compact storage is a significant advantage.

### Query Timing Comparison

| Database | Query | Observed Time |
|---|---|---|
| SQLite3 | `SELECT * FROM users;` | real: 0.820s |
| PostgreSQL | `SELECT COUNT(*) FROM users;` | 10-36 ms |
| PostgreSQL | `SELECT * FROM users;` | 54 ms |


### Performance Comparison

| Aspect | SQLite | PostgreSQL |
|---|---|---|
| Query overhead | Minimal -- no network or IPC | Higher -- client-server communication |
| Caching | Relies on OS page cache | Manages its own shared buffer pool |
| Concurrency overhead | None (single writer) | Coordination across multiple processes |
| Best workload | Lightweight, single-user, local access | Concurrent, multi-user, high-throughput |

SQLite has lower architectural overhead because it runs entirely inside the application process.
There is no network round-trip, no inter-process communication, and no shared memory
coordination. PostgreSQL introduces additional overhead from its server processes and concurrency
management, but this is the cost of supporting multiple simultaneous users reliably. PostgreSQL
is optimized for scalability; SQLite is optimized for simplicity and local access.

### When Each System Performs Better

SQLite performs better when:

- Only one writer is active at a time
- The database is small enough to fit mostly in the OS page cache
- Latency of individual queries must be minimal with no network round-trip
- The application must run without any server setup

PostgreSQL performs better when:

- Many clients read and write concurrently
- The dataset is large and requires a managed buffer pool
- Crash recovery guarantees are mandatory
- Complex queries, joins, and query planning are frequent

---
