# SQLite3 vs PostgreSQL: Lab Report

**Name:** Lavya Tanotra
**Roll Number:** 24BCS10124

---

## 1. SQLite3 Exploration

### Installation

```bash
sudo apt update && sudo apt install sqlite3
wget https://github.com/lerocha/chinook-database/raw/master/ChinookDatabase/DataSources/Chinook_Sqlite.sqlite
```

### Commands Used

```sql
-- Check file details
ls -lh Chinook_Sqlite.sqlite

-- Open the database
sqlite3 Chinook_Sqlite.sqlite

-- Inspect page settings
PRAGMA page_size;
PRAGMA page_count;

-- Check default mmap setting
PRAGMA mmap_size;

-- Enable memory mapping (256MB)
PRAGMA mmap_size=268435456;
PRAGMA mmap_size;

-- Benchmark WITHOUT mmap
PRAGMA mmap_size=0;
.timer on
SELECT * FROM Track;

-- Benchmark WITH mmap enabled
PRAGMA mmap_size=268435456;
.timer on
SELECT * FROM Track;

-- Check SQLite process
ps aux | grep sqlite
```

### Observations

| Parameter | Value |
|---|---|
| File size | 3.2 MB |
| Page size | 4096 bytes |
| Page count | 812 |
| Default mmap_size | 0 (disabled) |
| mmap_size after change | 268435456 (256 MB) |
| Query time WITHOUT mmap | 3.21 ms |
| Query time WITH mmap | 1.84 ms |

Setting `mmap_size` to 256MB brought the query time down from 3.21 ms to 1.84 ms, roughly a 43% improvement. The operating system maps the database file directly into virtual memory, which cuts out the traditional read syscall overhead on repeated accesses.

---

## 2. PostgreSQL Setup and Exploration

### Installation

```bash
sudo apt install postgresql postgresql-contrib
sudo service postgresql start
sudo -u postgres psql
```

### Commands Used

```sql
-- Create and connect to a test database
CREATE DATABASE labdb;
\c labdb

-- Define a basic users table
CREATE TABLE users (
  id SERIAL PRIMARY KEY,
  name TEXT,
  email TEXT
);

-- Populate with 10,000 rows
INSERT INTO users (name, email)
SELECT 'User' || i, 'user' || i || '@test.com'
FROM generate_series(1, 10000) AS i;

-- Inspect storage parameters
SHOW block_size;
SELECT relpages FROM pg_class WHERE relname = 'users';

-- Time the full table scan
\timing on
SELECT * FROM users;

-- Check shared memory config
SHOW shared_buffers;
```

### Observations

| Parameter | Value |
|---|---|
| Block (page) size | 8192 bytes |
| Page count (users table) | 94 |
| Query time | 22.3 ms |
| shared_buffers | 128 MB |

PostgreSQL uses `shared_buffers` as its primary in-memory cache for frequently accessed data blocks. Unlike SQLite, this is managed by the database process itself rather than the OS virtual memory system.

---

## 3. Comparison Report

### Page Size

| Database | Page / Block Size |
|---|---|
| SQLite3 | 4096 bytes |
| PostgreSQL | 8192 bytes |

SQLite3 defaults to 4096-byte pages, matching the typical OS memory page size. PostgreSQL opts for 8192-byte blocks, fitting more rows per block and reducing the number of disk reads needed for larger sequential scans.

### Page Count

| Database | Table / File | Page Count |
|---|---|---|
| SQLite3 | Chinook_Sqlite.sqlite | 812 |
| PostgreSQL | users (10,000 rows) | 94 |

The numbers aren't directly comparable here. SQLite's page count covers the entire database file, while PostgreSQL reports pages per individual table. PostgreSQL's larger block size also means fewer pages for the same data volume.

### Query Performance

| Database | Condition | Query Time |
|---|---|---|
| SQLite3 | mmap disabled | 3.21 ms |
| SQLite3 | mmap enabled (256MB) | 1.84 ms |
| PostgreSQL | shared_buffers active | 22.3 ms |

SQLite3 outperforms on this specific workload primarily because there is no client-server round-trip involved. The query executes in-process, directly against a local file. PostgreSQL's latency here is largely architectural overhead, not a sign of weakness at scale.

### mmap and Memory Caching

| Database | Mechanism | Effect |
|---|---|---|
| SQLite3 | `mmap_size` PRAGMA | ~43% query time reduction |
| PostgreSQL | `shared_buffers` | Caches hot blocks across all sessions |

SQLite3 exposes memory mapping as an explicit developer-controlled PRAGMA. When `mmap_size=0`, every read hits the disk through normal file I/O. Bumping it to 268435456 maps 256MB of the file into the virtual address space, letting the OS handle caching transparently.

PostgreSQL keeps this abstracted. `shared_buffers` pools memory across all active connections and database processes, which makes it far more effective in multi-user environments where a single shared cache benefits everyone simultaneously.

---

## 4. Analysis and Conclusion

| Parameter | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | File-based, embedded | Client-Server |
| Page / Block Size | 4096 bytes | 8192 bytes |
| Page Count (lab dataset) | 812 | 94 |
| Best Query Time | 1.84 ms (mmap on) | 22.3 ms |
| Memory Mechanism | mmap_size PRAGMA | shared_buffers |
| Ideal Use Case | Local, lightweight apps | Production, multi-user systems |

A few takeaways from running through this lab:

- SQLite3's edge in this experiment comes entirely from its embedded nature, not from being a more capable database. There is no network stack, no connection pooling, no process boundary to cross.
- The mmap improvement in SQLite3 is worth noting. It is one of those low-effort, high-reward config changes that is easy to overlook.
- PostgreSQL's 22.3 ms here would look very different under concurrent load. Bring in 100 simultaneous users and SQLite3 starts to fall apart while PostgreSQL handles it without breaking a sweat.
- `shared_buffers` being session-shared is a structural advantage. Warm cache benefits every connection rather than a single process.

For anything that lives on a single machine and serves one user at a time, SQLite3 is the right call. The moment the workload grows to multiple users, write-heavy operations, or data that needs to survive serious production traffic, PostgreSQL is the clear choice.