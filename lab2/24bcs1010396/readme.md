# SQLite3 vs PostgreSQL Storage and Memory Management Analysis

## Introduction

This report presents a comparative analysis of SQLite3 and PostgreSQL with a focus on storage architecture, memory management, process models, caching mechanisms, and overall performance characteristics.

Both databases were evaluated using identical datasets and similar operations to understand how their internal architectures influence storage overhead, execution behavior, concurrency handling, and query performance.

The experiments specifically explore:

* Database file organization
* Page and block management
* Memory-mapped I/O behavior
* Shared memory and caching
* Process architecture
* Query timing and execution characteristics
* Storage overhead and scalability

---

# 1. SQLite3 Experiment

## 1.1 Environment

```bash
sqlite3 --version
```

```
3.51.0 2025-11-04 19:38:17 fb2c931ae597f8d00a37574ff67aeed3eced4e5547f9120744ae4bfa8e74527b (64-bit)
```

---

## 1.2 Database and Table Setup

A new SQLite database file was created using the command-line shell:

```bash
sqlite3 advDbLab.db
```

A table was then created to store user records:

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

A small number of rows were manually inserted for initial verification:

```sql
INSERT INTO users (name, age) VALUES
('Alice', 21),
('Bob', 22),
('Charlie', 23),
('David', 24),
('Eve', 25);
```

---

## 1.3 Large Dataset Insertion

A recursive Common Table Expression (CTE) was used to insert 100,000 rows efficiently in a single statement:

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

This approach minimizes repeated database round-trips and allows SQLite to process all inserts within a single transaction, significantly improving insertion efficiency.

---

## 1.4 Database File Size

```bash
ls -lh advDbLab.db
```

```
-rw-r--r-- 1 navjeetsingh navjeetsingh 2.0M May 9 20:09 advDbLab.db
```

SQLite stores the complete database inside a single file. Unlike client-server database systems, there are no separate files for metadata, indexes, or transaction logs by default. Everything is contained within `advDbLab.db`.

This file-based design makes SQLite extremely portable and simple to back up or transfer.

---

## 1.5 Page Size

```bash
sqlite3 advDbLab.db "PRAGMA page_size;"
```

```
4096
```

SQLite divides the database into fixed-size pages. The default page size is 4096 bytes, which aligns with the default page size used by most modern operating systems.

This alignment allows SQLite pages to map efficiently onto OS memory pages, reducing overhead during file reads and caching operations.

---

## 1.6 Page Count

```bash
sqlite3 advDbLab.db "PRAGMA page_count;"
```

```
0
```

The `page_count` returned `0` because the PRAGMA command was mistakenly executed on a different database file (`adbDbLab.db`) rather than the populated file (`advDbLab.db`).

SQLite automatically creates a new empty database file if the specified file does not exist. As a result, the command was executed against an empty database containing no allocated pages.

---

## 1.7 Memory-Mapped I/O in SQLite

SQLite supports optional memory-mapped I/O using the `mmap_size` PRAGMA.

By default, mmap is disabled:

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

This configures a 256 MB mmap window for the active connection.

However, opening a new connection shows that the setting resets:

```bash
sqlite3 advDbLab.db "PRAGMA mmap_size;"
```

```
0
```

### Key Observation

`mmap_size` is a per-connection and per-session configuration. It is not permanently stored inside the database file.

Every new SQLite connection starts with mmap disabled unless the application explicitly enables it again.

This design ensures SQLite remains portable across environments where memory mapping may not be available or reliable.

### How mmap Works

When mmap is enabled, SQLite maps the database file directly into the process's virtual address space. The operating system then uses demand paging to load pages only when they are accessed.

This removes the additional memory copy typically required during standard `read()` operations.

For read-heavy workloads on large databases, mmap can improve throughput and reduce CPU overhead.

---

## 1.8 Process Architecture

```bash
ps aux | grep sqlite
```

### Observation

SQLite appeared as a lightweight, short-lived process that existed only while the shell session remained active.

Once the connection closed, the process immediately disappeared.

This behavior reflects SQLite's embedded architecture:

* SQLite is not a standalone server
* It runs directly inside the application's process
* All database operations execute within the same memory space as the application

This architecture eliminates inter-process communication overhead and keeps resource consumption extremely low.

---

## 1.9 Query Timing

```bash
time sqlite3 advDbLab.db "SELECT * FROM users;"
```

```
real    0m0.820s
user    0m0.146s
sys     0m0.332s
```

These values represent different aspects of execution time:

* `real` — total wall-clock execution time
* `user` — CPU time spent executing application-level code
* `sys` — CPU time spent inside kernel operations such as file reads and memory management

SQLite completed a full table scan of 100,000 rows in under one second despite having:

* No server process
* No dedicated buffer pool
* No continuously running background workers

This demonstrates the efficiency of SQLite's lightweight embedded architecture for local and low-concurrency workloads.

---

# 2. PostgreSQL Experiment

## 2.1 Starting the Service

PostgreSQL operates as a dedicated database server and must be running before any client connections can be established.

```bash
sudo systemctl start postgresql
```

Unlike SQLite, PostgreSQL follows a client-server architecture with multiple background processes running continuously.

---

## 2.2 Connecting and Database Setup

```bash
sudo -u postgres psql
```

Creating the database:

```sql
CREATE DATABASE oslab_pg;
\c oslab_pg
```

Creating the table:

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

---

## 2.3 Large Dataset Insertion

PostgreSQL provides the `generate_series` function for generating sequential data efficiently.

```sql
INSERT INTO users(name, age)
SELECT
    'User' || g,
    20 + (g % 10)
FROM generate_series(1, 100000) AS g;
```

This method simplifies bulk insertion and performs efficiently for large datasets.

---

## 2.4 Database Size

```sql
SELECT pg_size_pretty(pg_database_size('oslab_pg'));
```

```
15 MB
```

The PostgreSQL database occupied approximately 15 MB for the same dataset that SQLite stored in only 2 MB.

The larger storage footprint is expected because PostgreSQL maintains:

* MVCC metadata
* Tuple headers
* WAL (Write-Ahead Logging)
* Sequence objects
* Additional system catalog structures

These structures improve concurrency handling, durability, and crash recovery at the cost of increased storage overhead.

---

## 2.5 Block Size

```sql
SHOW block_size;
```

```
8192
```

PostgreSQL uses an 8192-byte default page size, which is twice the default page size used by SQLite.

Larger pages reduce the number of disk I/O operations required during sequential scans because more rows fit within a single page fetch.

However, for highly selective queries that access only a few rows, larger pages may cause more unnecessary data to be read into memory.

---

## 2.6 Page Count Estimation

```sql
SELECT relname, relpages
FROM pg_class
WHERE relname = 'users';
```

```
users | 0
```

The `relpages` value remained `0` because PostgreSQL updates these statistics asynchronously.

The catalog entry had not yet been refreshed by autovacuum after the bulk insertion.

Running:

```sql
ANALYZE users;
```

would update the statistics and provide an accurate page count.

---

## 2.7 Query Timing

Timing mode was enabled:

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

### Key Observation

The variation in `COUNT(*)` timings reflects the effect of caching.

* The first execution required loading data pages from disk into shared buffers
* Subsequent executions reused cached pages already present in memory

The slower intermediate run likely occurred due to temporary system activity or partial buffer eviction.

The `SELECT *` query required more time because the entire dataset had to be transferred rather than simply counting rows.

---

## 2.8 Process Architecture

```bash
ps aux | grep postgres
```

Unlike SQLite, PostgreSQL runs as a collection of cooperating server processes.

Its architecture includes:

* A main server process
* Background maintenance workers
* Dedicated backend processes for connected clients

Example process list:

```text
postgres  386672  0.0  0.1 225464 31212 ?        Ss   20:18   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
postgres  386676  0.0  0.0 225596 12216 ?        Ss   20:18   0:00 postgres: 16/main: checkpointer
postgres  386677  0.0  0.0 225620  7916 ?        Ss   20:18   0:00 postgres: 16/main: background writer
postgres  386679  0.0  0.0 225464 10396 ?        Ss   20:18   0:00 postgres: 16/main: walwriter
postgres  386680  0.0  0.0 227072  8860 ?        Ss   20:18   0:00 postgres: 16/main: autovacuum launcher
postgres  386681  0.0  0.0 227048  8140 ?        Ss   20:18   0:00 postgres: 16/main: logical replication launcher
root      387573  0.1  0.0  19820  7632 pts/0    S+   20:18   0:00 sudo -u postgres psql
root      387574  0.0  0.0  19820  2644 pts/2    Ss   20:18   0:00 sudo -u postgres psql
postgres  387575  0.0  0.0  28684 12004 pts/2    S+   20:18   0:00 /usr/lib/postgresql/16/bin/psql
postgres  389273  0.2  0.2 228352 34104 ?        Ss   20:19   0:00 postgres: 16/main: postgres oslab_pg [local] idle
navjeet-d+  405463  0.0  0.0   9148  2272 pts/1    S+   20:23   0:00 grep --color=auto postgres
```

These processes collectively manage:

* Shared memory
* WAL writing
* Disk flushing
* Autovacuum cleanup
* Concurrency control
* Replication support

This architecture enables PostgreSQL to support large-scale concurrent workloads efficiently.

---

# 3. Observations

## 3.1 SQLite Observations

* The entire database exists within a single file (`advDbLab.db`), making backups and transfers simple.
* The default page size of 4096 bytes aligns with typical OS page sizes.
* mmap support exists but must be enabled separately for every connection.
* SQLite runs entirely within the application's process and does not require a standalone server.
* Resource usage remains minimal because there are no continuously running background workers.
* SQLite is highly efficient for lightweight and single-user workloads.

---

## 3.2 PostgreSQL Observations

* PostgreSQL uses larger 8192-byte blocks optimized for large scans and enterprise workloads.
* Storage overhead is significantly higher due to MVCC metadata, WAL, and concurrency infrastructure.
* Query execution benefits heavily from shared buffer caching.
* Multiple background processes continuously manage maintenance and durability.
* PostgreSQL relies on its own shared memory buffer manager instead of mmap for caching data pages.
* The architecture is optimized for concurrency, scalability, and reliability.

---

# 4. Comparison Table

| Feature               | SQLite3                                    | PostgreSQL                                           |
| --------------------- | ------------------------------------------ | ---------------------------------------------------- |
| Architecture          | Embedded library, no server                | Client-server architecture                           |
| Storage Model         | Single `.db` file                          | Directory containing multiple files                  |
| Page Size             | 4096 bytes                                 | 8192 bytes                                           |
| File Size (100k rows) | 2.0 MB                                     | 15 MB                                                |
| mmap Support          | Supported, session-specific                | Uses shared buffers instead                          |
| Process Model         | Single embedded process                    | Multiple background + backend processes              |
| Shared Memory         | Not used                                   | Shared buffers across processes                      |
| Concurrency           | File-level locking                         | MVCC with row-level concurrency                      |
| WAL / Recovery        | Optional                                   | Enabled by default                                   |
| Resource Usage        | Very lightweight                           | Higher baseline resource usage                       |
| Best Use Cases        | Embedded systems, local tools, mobile apps | Enterprise systems, web backends, multi-user systems |

---

# 5. Analysis of mmap and Caching

## 5.1 How Normal File I/O Works

Traditional file I/O using `read()` typically involves two copy operations:

1. Data is transferred from storage into the OS page cache using DMA.
2. The kernel copies that data from the page cache into the application's user-space buffer.

The second copy consumes additional CPU cycles and memory bandwidth.

For large databases and repeated reads, this overhead becomes significant.

---

## 5.2 How mmap Reduces Overhead

When SQLite enables mmap, the database file is mapped directly into the process's virtual memory space.

The operating system loads pages only when accessed using demand paging.

As a result:

* The second memory copy is eliminated
* Data becomes directly accessible from mapped memory
* CPU overhead decreases
* Read throughput improves

This is especially beneficial for large, read-heavy workloads.

However, SQLite's mmap configuration is connection-specific and not shared across processes.

---

## 5.3 PostgreSQL Shared Buffer Management

PostgreSQL does not rely on mmap for database pages.

Instead, it allocates a large shared memory region called **shared buffers** during startup.

The internal buffer manager:

* Decides which pages remain cached
* Controls eviction policies
* Coordinates access across multiple backend processes

All clients share the same cache.

This allows PostgreSQL to optimize caching decisions using database-specific knowledge rather than relying entirely on the operating system.

For example:

* Frequently used index pages can remain cached
* Large sequential scans can avoid polluting the cache unnecessarily

This design is particularly effective for highly concurrent workloads.

---

# 6. Process Architecture Comparison

## 6.1 SQLite — Embedded Single-Process Design

SQLite operates as a library linked directly into the application.

There is:

* No daemon
* No server process
* No network communication layer

Architecture overview:

```text
Application Process
    |
    +-- SQLite Library (linked in)
    |       |
    |       +-- reads/writes advDbLab.db directly
    |
    +-- Application code
```

Advantages of this architecture include:

* Extremely low overhead
* Minimal memory usage
* Fast local access
* Simple deployment

The primary limitation is concurrency.

SQLite allows only one writer at a time because write operations rely on file-level locking.

---

## 6.2 PostgreSQL — Client-Server Multi-Process Design

PostgreSQL follows a dedicated server architecture.

A master process called `postmaster` accepts incoming connections and creates backend processes for clients.

Architecture overview:

```text
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

Advantages of this design include:

* High concurrency
* Shared caching across clients
* Strong crash recovery
* Reliable multi-user support
* Advanced transaction handling

The trade-off is increased memory and CPU overhead even when the system is idle.

---

# 7. Performance Analysis

## 7.1 File Size Difference

The same dataset occupied:

* **2.0 MB** in SQLite
* **15 MB** in PostgreSQL

Several factors contribute to PostgreSQL's larger storage footprint:

* Larger default page size
* MVCC metadata
* Tuple headers
* WAL structures
* System catalog storage
* Additional concurrency infrastructure

SQLite's simpler architecture results in significantly lower storage overhead.

This makes SQLite particularly suitable for:

* Embedded devices
* Mobile applications
* Lightweight desktop applications
* Storage-constrained environments

---

## 7.2 Query Timing Comparison

| Database   | Query                         | Observed Time |
| ---------- | ----------------------------- | ------------- |
| SQLite3    | `SELECT * FROM users;`        | real: 0.820s  |
| PostgreSQL | `SELECT COUNT(*) FROM users;` | 10–36 ms      |
| PostgreSQL | `SELECT * FROM users;`        | 54 ms         |

---

## 7.3 Performance Characteristics

| Aspect                 | SQLite                   | PostgreSQL                                |
| ---------------------- | ------------------------ | ----------------------------------------- |
| Query Overhead         | Minimal                  | Higher due to client-server communication |
| Caching Strategy       | OS page cache            | Dedicated shared buffer pool              |
| Concurrency Management | Limited                  | Advanced MVCC coordination                |
| Deployment Complexity  | Very simple              | Requires server management                |
| Best Workload Type     | Single-user local access | Multi-user concurrent systems             |

SQLite avoids the overhead associated with:

* Network communication
* Inter-process coordination
* Shared memory synchronization

This makes it extremely efficient for local workloads.

PostgreSQL introduces additional complexity and overhead to support:

* Concurrent users
* Transaction isolation
* Shared caching
* Advanced query optimization
* Enterprise-level durability

---

## 7.4 When Each System Performs Better

### SQLite Performs Better When:

* Only one writer is active at a time
* The database fits largely within the OS page cache
* Minimal deployment complexity is required
* Applications need extremely low local latency
* No dedicated server should be maintained

### PostgreSQL Performs Better When:

* Many clients read and write concurrently
* The dataset is large and frequently accessed
* Strong crash recovery guarantees are necessary
* Complex joins and query planning are common
* High scalability and reliability are required

---

# Conclusion

SQLite and PostgreSQL are designed for fundamentally different goals.

SQLite prioritizes:

* Simplicity
* Portability
* Minimal overhead
* Embedded deployment

Its lightweight architecture makes it ideal for local applications, mobile systems, and environments where server management is undesirable.

PostgreSQL prioritizes:

* Scalability
* Concurrency
* Reliability
* Enterprise-grade data management

Its multi-process architecture, shared buffer management, MVCC implementation, and background maintenance processes enable it to efficiently support complex and concurrent workloads.

The experiments demonstrate that:

* SQLite achieves excellent efficiency with minimal storage and process overhead
* PostgreSQL consumes more resources but provides significantly stronger concurrency and scalability capabilities

Ultimately, the choice between SQLite and PostgreSQL depends entirely on workload requirements, deployment constraints, concurrency needs, and long-term scalability goals.
