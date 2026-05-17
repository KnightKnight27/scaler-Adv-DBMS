# Storage Engine Lab Report

**Name:** Vansh Dobhal
**Roll:** 24BCS10099
**Batch:** Lab 02
**Title:** Storage Engine Exploration — SQLite3 vs PostgreSQL

---

## SQLite3 Exploration

### Setup

Installed SQLite3, created a database file `sample.db`, and created a `users` table inside it.

```bash
sqlite3 sample.db
```

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT
);
```

### File Size Observation

Checked file size before and after inserting data using `ls -lh`.

Before inserting rows:

```
-rw-r--r-- 1 vansh root 8.0K May 9 sample.db
```

After inserting 10,000 rows:

```
-rw-r--r-- 1 vansh root 196K May 9 sample.db
```

The database exists as a single normal file on disk. As more data was added, the file size grew automatically. This shows that SQLite internally allocates more pages within the same file rather than creating new files.

### PRAGMA Commands

**Page Size:**

```sql
PRAGMA page_size;
```

Result: `4096`

SQLite divides its database into fixed-size pages of 4KB each. It operates on data page-by-page rather than byte-by-byte.

**Page Count:**

```sql
PRAGMA page_count;
```

Result: `49`

With a 196KB database and 4KB pages, the math checks out: `196 / 4 = 49`. The entire database file is internally organized into exactly these pages.

**mmap_size (default):**

```sql
PRAGMA mmap_size;
```

Result: `0` — mmap was disabled by default.

### Experimenting with mmap

`mmap` allows SQLite to map the database file directly into virtual memory, potentially avoiding redundant data copies between kernel and user space.

Set mmap to 30MB:

```sql
PRAGMA mmap_size = 31457280;
```

### Query Timing — With vs Without mmap

Reset mmap to 0, then timed a full table scan:

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

**Without mmap:**

```
real    0m0.070s
user    0m0.014s
sys     0m0.023s
```

**With mmap (30MB):**

```
real    0m0.091s
user    0m0.006s
sys     0m0.040s
```

mmap did not improve performance here. Since the dataset was small and Linux already caches file pages internally (page cache), the overhead of mmap setup outweighed any benefit. mmap is not always guaranteed to speed things up.

### Process Behavior

Checked the SQLite process while it was running:

```bash
ps aux | grep sqlite
```

```
vansh   47121  0.0  0.0  12056  5424 pts/0    S+   16:47   0:00 sqlite3 sample.db
vansh   47693  0.0  0.0   9156  2296 pts/2    S+   16:49   0:00 grep --color=auto sqlite
```

After exiting SQLite, the process was completely gone:

```
vansh   47907  0.0  0.0   9156  2300 pts/2    S+   16:49   0:00 grep --color=auto sqlite
```

SQLite is an embedded database. It does not run a persistent server process in the background. Once the program exits, no SQLite process remains.

### System Calls with strace

```bash
strace sqlite3 sample.db
```

Notable syscalls observed in the output:

- `openat()` — opens the database file
- `read()` — reads data from the file
- `mmap()` — maps memory regions
- `close()` — closes file descriptors

Even a simple database open triggers dozens of low-level OS operations internally.

### Inode and Multiple Tables

Checked the inode of `sample.db`:

```bash
ls -i sample.db
```

```
104371 sample.db
```

Created a second table:

```sql
CREATE TABLE products (
    id INTEGER PRIMARY KEY,
    price INTEGER
);
```

Inserted some rows. Checked again:

```
-rw-r--r-- 1 vansh root 200K May 9 sample.db
```

Inode remained `104371`. No new file was created — the size just grew by 4KB (one new page). SQLite stores all tables inside the same single database file.

---

## PostgreSQL Exploration

### Setup

Installed PostgreSQL, created a database `labdb`, and created a `users` table, then inserted 10,000 rows.

```bash
sudo -u postgres psql
```

```sql
CREATE DATABASE labdb;
\c labdb

CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    email TEXT
);
```

Enabled query timing:

```sql
\timing
```

### Query Execution Time

```sql
SELECT * FROM users;
```

```
Time: 4.193 ms
```

### Block Size

```sql
SHOW block_size;
```

Result: `8192`

PostgreSQL internally stores data in 8KB pages/blocks, unlike SQLite's 4KB pages.

### Page Count

Calculating the approximate number of pages occupied by the `users` table:

```sql
SELECT pg_relation_size('users') / 8192 AS approx_pages;
```

```
 approx_pages
--------------
           64
```

### Shared Buffer (Memory)

```sql
SHOW shared_buffers;
```

Result: `128MB`

PostgreSQL maintains a shared memory buffer to cache pages in RAM. This is a server-level buffer shared across all connections, unlike SQLite's simpler mmap approach.

### Storage: One File Per Table

Checked the file location of the `users` table:

```sql
SELECT pg_relation_filepath('users');
```

```
base/16388/16390
```

Created a `products` table and checked its location:

```sql
CREATE TABLE products (id SERIAL PRIMARY KEY, price INTEGER);
SELECT pg_relation_filepath('products');
```

```
base/16388/16399
```

Both tables are in the same directory (`base/16388/`) but stored in separate files. This is fundamentally different from SQLite, which stores all tables inside one file.

### Process Behavior

Checked running processes while connected to PostgreSQL:

```bash
ps aux | grep postgres
```

```
postgres    2175  0.0  0.1 225756 23812 ?  Ss  checkpointer
postgres    2176  0.0  0.0 225628  7808 ?  Ss  background writer
postgres    2182  0.0  0.0 225476 10348 ?  Ss  walwriter
postgres    2183  0.0  0.0 227080  8520 ?  Ss  autovacuum launcher
postgres    2184  0.0  0.0 227056  7976 ?  Ss  logical replication launcher
```

After exiting with `\q`, the PostgreSQL server processes continued running:

```bash
ps aux | grep postgres
```

The same background processes (checkpointer, walwriter, autovacuum, etc.) were still active.

PostgreSQL is a full client-server database. It runs as a persistent daemon with multiple background workers even when no client is connected.

---

## Comparison: SQLite3 vs PostgreSQL

| Parameter | SQLite3 | PostgreSQL |
|---|---|---|
| **Architecture** | Embedded, serverless | Client-server, persistent daemon |
| **Page/Block Size** | 4 KB (4096 bytes) | 8 KB (8192 bytes) |
| **Page Count (10k rows)** | 49 pages (~196KB) | ~64 pages |
| **Storage per Table** | All tables in one `.db` file | Separate file per table |
| **mmap Support** | Yes, via `PRAGMA mmap_size` | Managed internally, not directly exposed |
| **Query Time (10k rows)** | ~70ms (without mmap) | ~4ms |
| **Shared Buffer** | Not applicable | 128MB default |
| **Background Processes** | None — process exits cleanly | Multiple (checkpointer, walwriter, autovacuum, etc.) |
| **mmap Impact** | Minimal — no measurable gain on small datasets | Not directly controllable |
| **Use Case** | Lightweight, embedded, local apps | Multi-user, production, high-concurrency workloads |

---

## Key Observations

- Databases are ultimately files stored in the filesystem and managed page by page.
- SQLite uses 4KB pages; PostgreSQL uses 8KB blocks — both divide data into fixed-size units rather than handling raw bytes.
- SQLite is embedded and serverless. The process appears only when in use and disappears on exit.
- PostgreSQL is a full server that runs continuously in the background with dedicated background workers for checkpointing, WAL writing, and autovacuum.
- mmap in SQLite did not improve performance for small datasets because the Linux kernel page cache already handles file caching efficiently.
- SQLite stores every table in one database file. PostgreSQL stores each table in its own separate file under the data directory.
- Even simple database operations involve many low-level OS syscalls — `openat`, `read`, `mmap`, `close`, etc.
- Storage engines are tightly connected to OS-level memory management and file I/O.
