# Database Internals Lab Report

**Name:** Yash Solanki\
**Role Number:** 24BCS10291

---

## 1. SQLite3 Exploration

### Commands Used

**1. Inspecting File Size:**

```bash
# Verify the size of the database file on disk
ls -lh sample.db

```

**2. Checking Page Size and Page Count:**

```sql
-- Access the SQLite database
sqlite3 sample.db

-- Check the default page size
PRAGMA page_size;

-- Check the total number of pages
PRAGMA page_count;

```

**3. Experimenting with `mmap_size`:**

```sql
-- Check current mmap size (default is usually 0)
PRAGMA mmap_size;

-- Set mmap size to 256 MB (268435456 bytes)
PRAGMA mmap_size = 268435456;

```

**4. Timing Queries:**

```bash
# Timing query without mmap
time sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM users;"

# Timing query with mmap enabled
time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;"

```

**5. Inspecting Processes:**

```bash
ps aux | grep sqlite

```

### Observations

* **File Size & Pages:** SQLite stores the entire database in a single `.db` file. The total file size exactly matches the formula: `page_size` × `page_count`. The default page size is **4096 bytes (4 KB)**, which aligns perfectly with standard Linux OS memory pages to optimize kernel loading.
* **Process Architecture:** The `ps aux` command reveals that SQLite runs as a single, lightweight process. There is no background server daemon; it operates entirely as an embedded library within the calling application.
* **Query Performance & mmap:** Setting `mmap_size` maps the database file directly into the process's virtual address space, bypassing the kernel buffer cache and reducing `read()` syscall overhead. During testing, enabling mmap resulted in slightly faster execution times, especially on repeated (warm) reads.

---

## 2. PostgreSQL Setup & Exploration

### Commands Used

**1. Inspecting Block Size:**

```bash
# Access PostgreSQL
sudo -u postgres psql -d labdb

```

```sql
-- Check default block size
SHOW block_size;

```

**2. Checking Page Count:**

```sql
-- PostgreSQL tracks pages per relation (table), not for the whole database
SELECT relname, relpages 
FROM pg_class 
WHERE relname = 'users';

```

**3. Timing Queries:**

```sql
-- Turn on internal timing
\timing on

-- Execute query
SELECT * FROM users;

```


**4. Inspecting Processes:**

```bash
ps aux | grep postgres

```

### Observations

* **Block Size:** PostgreSQL uses a default block size of **8192 bytes (8 KB)**. This larger size is optimized for server workloads, allowing the engine to fetch more data per I/O operation during sequential scans.
* **Page Count:** Unlike SQLite, PostgreSQL stores data across multiple internal files within a data directory. Page counts are tracked per table (`relpages`) rather than as a single global file metric.
* **Process Architecture:** The `ps aux` command shows a robust **multi-process architecture**. Even when idle, PostgreSQL runs several dedicated background daemons, including a `postmaster` (supervisor), `checkpointer`, `background writer`, `walwriter`, and `autovacuum launcher`.
* **Query Performance:** PostgreSQL manages its own memory via `shared_buffers` and utilizes an advanced cost-based query planner. While it carries a slight IPC (Inter-Process Communication) overhead compared to an embedded DB, it executes queries highly efficiently, especially for large datasets.

---

## 3. Comparison Report: SQLite3 vs PostgreSQL

### Page Size

* **SQLite3:** Uses **4 KB** (4096 bytes) pages by default. This is tailored for embedded, local environments as it matches standard OS memory pages, preventing memory waste.
* **PostgreSQL:** Uses **8 KB** (8192 bytes) blocks. This is fixed at compile-time and designed for high-throughput server workloads to minimize disk I/O operations when reading large volumes of data.

### Page Count & Storage

* **SQLite3:** Consolidates all data into a **single file**. The `page_count` reflects the entire database file (Data + Indexes + Metadata). Total file size = `page_size` × `page_count`.
* **PostgreSQL:** Distributes data across a **directory of files**. Page counts are calculated per-relation (table or index) using `pg_class.relpages`.

### Query Performance

* **SQLite3:** Offers blazing-fast performance for simple, local reads due to the lack of network or IPC overhead. However, it relies heavily on a simple rule-based planner and locks the entire database file during writes, bottlenecking concurrent operations.
* **PostgreSQL:** Features a highly sophisticated cost-based query planner. While a simple query might have a microsecond of IPC latency, Postgres vastly outperforms SQLite on complex joins, aggregations, and concurrent read/write workloads (thanks to MVCC - Multi-Version Concurrency Control).

### The Impact of `mmap`

* **SQLite3:** Exposes memory-mapped I/O directly to the user via `PRAGMA mmap_size`. When enabled, it maps the `.db` file into memory, reducing standard read/write system calls. This provides a measurable speedup for read-heavy operations, particularly when the database size fits well within system RAM.
* **PostgreSQL:** Does not expose a user-facing `mmap_size` setting. Instead, it relies on its internal `shared_buffers` cache alongside the operating system's page cache (`effective_cache_size`). Because Postgres operates continuously as a server, it proactively manages memory and background disk flushing, rendering manual mmap tuning unnecessary for the end user.