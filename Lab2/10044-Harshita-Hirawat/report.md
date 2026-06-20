# Database Internals Lab Report

### SQLite3 vs PostgreSQL: Storage, mmap, Processes, and Query Timing

Name: Harshita Hirawat  
Roll Number: 10044  
Sample database: Sakila

---

## 1. Environment Setup

| Component | Details |
|---|---|
| Source data | Sakila SQL files in `sakila-db/` |
| SQLite database created | `sakila_lab.db` |
| PostgreSQL database used | `postgres` |
| PostgreSQL schema created | `lab_sakila` |

The Sakila files provided were MySQL-style SQL files, so I imported the useful Sakila tables into a generated SQLite database and then loaded the same table data into PostgreSQL.

| Table | Rows Imported |
|---|---:|
| `actor` | 200 |
| `customer` | 599 |
| `film` | 1000 |
| `inventory` | 4581 |
| `payment` | 16044 |
| `rental` | 16044 |

### 1.1 SQLite Installation and Version

SQLite was verified with:

```bash
sqlite3 --version
```

Installed version:

```text
3.53.1 (64-bit)
```

On Ubuntu or Debian, SQLite and its development library can be installed with:

```bash
sudo apt install sqlite3 libsqlite3-dev
```

---

## 2. SQLite3 Exploration

### 2.1 File Size and Page Metadata

Commands used:

```bash
ls -lh sakila_lab.db
```

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
```

PowerShell equivalent used for file size on Windows:

```powershell
Get-ChildItem sakila_lab.db | Select-Object Name,Length
```

| Metric | Observed Value |
|---|---:|
| SQLite database file | `sakila_lab.db` |
| File size | 3,747,840 bytes |
| Page size | 4096 bytes |
| Page count | 915 pages |
| Default `mmap_size` | 0 |

Verification:

```text
915 pages * 4096 bytes = 3,747,840 bytes
```

The file size exactly matches `page_count * page_size`, so the SQLite database size is a clean multiple of 4 KB.

---

### 2.2 Query Timing Without mmap

The lab example mentioned `SELECT * FROM users;`. Sakila does not contain a `users` table, so I used two larger Sakila tables: `rental` and `payment`.

Command pattern:

```sql
PRAGMA mmap_size=0;
SELECT * FROM rental;
SELECT * FROM payment;
```

| Query | Rows | Run Timings in ms | Average | Minimum |
|---|---:|---|---:|---:|
| `SELECT * FROM rental;` | 16044 | 24.804, 22.000, 28.671, 31.615, 25.636, 25.591, 27.538 | 26.551 ms | 22.000 ms |
| `SELECT * FROM payment;` | 16044 | 22.014, 27.503, 26.174, 23.830, 21.260, 22.188, 25.813 | 24.112 ms | 21.260 ms |

Observation: With `mmap_size=0`, SQLite reads through its normal file I/O path. Both full-table reads completed in the 20-30 ms range on this machine.

---

### 2.3 Query Timing With mmap

Command pattern:

```sql
PRAGMA mmap_size=268435456;
SELECT * FROM rental;
SELECT * FROM payment;
```

| Query | Rows | Effective mmap Size | Run Timings in ms | Average | Minimum |
|---|---:|---:|---|---:|---:|
| `SELECT * FROM rental;` | 16044 | 268435456 | 22.163, 19.481, 25.889, 23.411, 17.758, 17.846, 19.924 | 20.925 ms | 17.758 ms |
| `SELECT * FROM payment;` | 16044 | 268435456 | 25.339, 19.471, 17.384, 17.681, 20.715, 19.489, 20.783 | 20.123 ms | 17.384 ms |

Observation: Enabling mmap improved the average timing for both selected tables in this run. The improvement was visible, but it should still be treated as machine-dependent because OS cache state and background load can affect small timing experiments.

---

### 2.4 mmap Impact Summary

| Query | Without mmap Avg | With mmap Avg | Difference |
|---|---:|---:|---:|
| `SELECT * FROM rental;` | 26.551 ms | 20.925 ms | 5.626 ms faster |
| `SELECT * FROM payment;` | 24.112 ms | 20.123 ms | 3.989 ms faster |

Key point: SQLite exposes mmap directly through `PRAGMA mmap_size`. Setting it to `268435456` allowed SQLite to use memory-mapped I/O for reads up to 256 MB.

---

### 2.5 SQLite Process Inspection

Linux command from the lab:

```bash
ps aux | grep sqlite
```

Windows command used:

```powershell
Get-Process | Where-Object { $_.ProcessName -like '*sqlite*' }
```

Observed result:

| Observation | Result |
|---|---|
| Long-running SQLite server process | Not found |
| Reason | SQLite is embedded and runs inside the calling application process |

SQLite does not need a separate server. In this experiment, the database operations were executed through the local SQLite engine and no independent SQLite daemon was running in the background.

### 2.6 Other Useful PRAGMA Commands

```sql
PRAGMA journal_mode;
PRAGMA cache_size;
PRAGMA integrity_check;
PRAGMA database_list;
```

| PRAGMA | Purpose |
|---|---|
| `journal_mode` | Shows whether SQLite uses DELETE, WAL, MEMORY, or another journal mode |
| `cache_size` | Shows the configured number of pages kept in SQLite's page cache |
| `integrity_check` | Checks the database structure; a healthy database returns `ok` |
| `database_list` | Lists the main database and any attached databases |

### 2.7 Verifying mmap with strace

On Linux, the system-call difference can be checked with:

```bash
strace -c -e trace=openat,read,mmap sqlite3 sakila_lab.db \
  "PRAGMA mmap_size=0; SELECT count(*) FROM rental;"

strace -c -e trace=openat,read,mmap sqlite3 sakila_lab.db \
  "PRAGMA mmap_size=268435456; SELECT count(*) FROM rental;"
```

With `mmap_size=0`, SQLite follows its normal file-read path. With mmap enabled,
the database file can be mapped into the process address space, so the trace
shows database-backed `mmap()` activity and fewer `read()` calls.

### 2.8 SQLite Is a Linked Library

SQLite runs inside the application instead of running as a separate server.
On Linux, its dynamic linkage can be checked with:

```bash
ldd $(which sqlite3) | grep sqlite
```

A dynamically linked installation shows `libsqlite3.so`. Applications call
functions such as `sqlite3_open()`, `sqlite3_exec()`, and `sqlite3_close()`
directly in their own process. SQLite therefore needs no TCP connection,
authentication handshake, or long-running database daemon.

---

## 3. PostgreSQL Exploration

### 3.1 Installation and Server Check

Commands used:

```powershell
& 'C:\Program Files\PostgreSQL\17\bin\psql.exe' --version
& 'C:\Program Files\PostgreSQL\17\bin\pg_isready.exe' -h localhost -p 5432
```

| Check | Observed Output |
|---|---|
| `psql --version` | `psql (PostgreSQL) 17.9` |
| `pg_isready` | `localhost:5432 - accepting connections` |

PostgreSQL was running locally and accepting connections on port `5432`.

---

### 3.2 PostgreSQL Storage Metadata

Commands used:

```sql
SHOW block_size;
SHOW data_directory;
```

| Metric | Observed Value |
|---|---|
| Block size | 8192 bytes |
| Data directory | `C:/Program Files/PostgreSQL/17/data` |

PostgreSQL uses an 8 KB block size in this installation. Unlike SQLite, it does not store the whole database as one visible `.db` file. Data is managed inside the PostgreSQL data directory using internal relation files.

---

### 3.3 PostgreSQL Table Size and Page Estimate

The same six Sakila tables were loaded into PostgreSQL under the schema `lab_sakila`.

Query used:

```sql
SELECT
  relname,
  reltuples::bigint AS estimated_rows,
  pg_relation_size(c.oid) AS bytes,
  ceil(pg_relation_size(c.oid)::numeric / current_setting('block_size')::numeric)::int AS estimated_pages
FROM pg_class c
JOIN pg_namespace n ON n.oid = c.relnamespace
WHERE n.nspname = 'lab_sakila'
  AND relkind = 'r'
ORDER BY relname;
```

| Table | Estimated Rows | Relation Bytes | Estimated Pages |
|---|---:|---:|---:|
| `actor` | 200 | 16,384 | 2 |
| `customer` | 599 | 73,728 | 9 |
| `film` | 1000 | 327,680 | 40 |
| `inventory` | 4581 | 245,760 | 30 |
| `payment` | 16044 | 1,114,112 | 136 |
| `rental` | 16044 | 1,228,800 | 150 |

Total size for the six-table PostgreSQL lab schema, including indexes:

```text
4080 kB
```

Observation: PostgreSQL page counts were checked per table relation. This is different from SQLite, where `PRAGMA page_count` reports the page count of the whole database file.

---

### 3.4 PostgreSQL Query Timing

Commands used:

```sql
\timing on
SET search_path TO lab_sakila;
SELECT * FROM rental;
SELECT * FROM payment;
```

| Query | Rows | Run Timings in ms | Average | Minimum |
|---|---:|---|---:|---:|
| `SELECT * FROM rental;` | 16044 | 10.637, 9.742, 7.349, 10.852, 8.193, 8.971, 7.801 | 9.078 ms | 7.349 ms |
| `SELECT * FROM payment;` | 16044 | 9.310, 8.945, 7.323, 7.040, 8.833, 8.051, 7.001 | 8.072 ms | 7.001 ms |

Observation: In this run, PostgreSQL returned the same large Sakila tables faster than SQLite. The result is not only about the database engine; client output behavior, caching, and process state can also affect timing.

---

### 3.5 PostgreSQL Process Inspection

Linux command from the lab:

```bash
ps aux | grep postgres
```

Windows command used:

```powershell
Get-Process | Where-Object { $_.ProcessName -like '*postgres*' }
```

Observed PostgreSQL process IDs:

| Process Name | PID |
|---|---:|
| `postgres` | 9572 |
| `postgres` | 9700 |
| `postgres` | 18896 |
| `postgres` | 28016 |
| `postgres` | 30220 |
| `postgres` | 31556 |
| `postgres` | 32936 |
| `postgres` | 33220 |

Observation: PostgreSQL was visible as multiple running processes. This matches its server-based design, where the server and background workers stay alive independently of the application using the database.

---

## 4. TCP Loopback Research

TCP loopback is used when a program connects back to the same machine using addresses like:

```text
localhost
127.0.0.1
```

In this lab, PostgreSQL was checked at:

```text
localhost:5432
```

When a Java app or any other client connects to `localhost:5432`, the traffic is handled inside the operating system through the loopback interface. It does not go out through the physical network card. The request still uses the network stack, but it stays inside the same machine.

---

## 5. SQLite3 vs PostgreSQL Comparison

### 5.1 Page and Storage Comparison

| Metric | SQLite3 | PostgreSQL |
|---|---:|---:|
| Storage style | Single database file | Data directory with relation files |
| Observed storage size | 3,747,840 bytes | 4080 kB for lab schema including indexes |
| Page/block size | 4096 bytes | 8192 bytes |
| Page count inspected | 915 pages for whole DB | Per-table pages, e.g. `rental` 150 and `payment` 136 |
| Direct command | `PRAGMA page_count;` | `pg_relation_size`, `pg_class`, block size calculation |

SQLite gave a direct whole-file page count. PostgreSQL required relation-level inspection because its storage is spread across internal table and index files.

---

### 5.2 Query Performance Comparison

| Query | SQLite Without mmap | SQLite With mmap | PostgreSQL |
|---|---:|---:|---:|
| `SELECT * FROM rental;` | 26.551 ms avg | 20.925 ms avg | 9.078 ms avg |
| `SELECT * FROM payment;` | 24.112 ms avg | 20.123 ms avg | 8.072 ms avg |

In this experiment, PostgreSQL was fastest for both full-table reads. SQLite improved after mmap was enabled, but PostgreSQL still had the lower average timing for these two queries.

---

### 5.3 mmap and Memory Behavior

| Area | SQLite3 | PostgreSQL |
|---|---|---|
| mmap control | User can set `PRAGMA mmap_size` | No equivalent per-query SQL PRAGMA |
| mmap value tested | `268435456` bytes | Not applicable |
| Default observed | `0` | Not applicable |
| Impact observed | Faster average SELECT timings | Uses its own server memory and OS caching model |

SQLite exposes mmap as a simple database setting. PostgreSQL does not use the same user-facing mmap flag; it is a server process with its own memory and caching behavior.

---

### 5.4 Architecture Summary

| Aspect | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | Embedded database library | Client-server DBMS |
| Process model | Runs inside application process | Multiple `postgres` processes observed |
| Communication | Direct function calls and local file I/O | TCP or Unix-domain socket |
| Concurrency | File locking; one writer at a time, improved by WAL | MVCC supports many concurrent readers and writers |
| Authentication | Filesystem permissions; no database server login | Users, roles, passwords, and SSL support |
| Transactions | ACID transactions with serialized writes | ACID transactions with multiple isolation levels |
| Deployment | Simple local file | Requires running server |
| Storage inspection | Easy through file size and PRAGMA commands | Uses catalog queries and relation sizes |
| Best suited for | Local storage, simple apps, embedded use | Multi-user systems and server applications |

### 5.5 When to Use Each Database

Use SQLite for embedded applications, desktop or mobile software, tests, local
tools, and low-concurrency workloads where simple deployment is important.

Use PostgreSQL for web backends, multi-user applications, concurrent writes,
advanced queries, role-based security, auditing, and production systems that
need a dedicated database server.

---

## 6. Commands Reference

### SQLite3

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
PRAGMA journal_mode;
PRAGMA cache_size;
PRAGMA integrity_check;
PRAGMA database_list;
PRAGMA mmap_size=0;
SELECT * FROM rental;
SELECT * FROM payment;
PRAGMA mmap_size=268435456;
SELECT * FROM rental;
SELECT * FROM payment;
```

```bash
ls -lh sakila_lab.db
ps aux | grep sqlite
ldd $(which sqlite3) | grep sqlite
strace -c -e trace=openat,read,mmap sqlite3 sakila_lab.db "PRAGMA mmap_size=0; SELECT count(*) FROM rental;"
strace -c -e trace=openat,read,mmap sqlite3 sakila_lab.db "PRAGMA mmap_size=268435456; SELECT count(*) FROM rental;"
```

### PostgreSQL

```sql
SHOW block_size;
SHOW data_directory;
\timing on
SET search_path TO lab_sakila;
SELECT * FROM rental;
SELECT * FROM payment;
```

```sql
SELECT
  relname,
  reltuples::bigint AS estimated_rows,
  pg_relation_size(c.oid) AS bytes,
  ceil(pg_relation_size(c.oid)::numeric / current_setting('block_size')::numeric)::int AS estimated_pages
FROM pg_class c
JOIN pg_namespace n ON n.oid = c.relnamespace
WHERE n.nspname = 'lab_sakila'
  AND relkind = 'r'
ORDER BY relname;
```

```bash
ps aux | grep postgres
```

---

## 7. Conclusions

1. SQLite used a 4096-byte page size and the generated `sakila_lab.db` file contained 915 pages. The file size was 3,747,840 bytes, which exactly matched `915 * 4096`.

2. SQLite's default `mmap_size` was 0. After setting `PRAGMA mmap_size=268435456`, the average time for `SELECT * FROM rental;` improved from 26.551 ms to 20.925 ms, and `SELECT * FROM payment;` improved from 24.112 ms to 20.123 ms.

3. PostgreSQL used an 8192-byte block size and stored data inside `C:/Program Files/PostgreSQL/17/data`. Unlike SQLite, its storage was inspected per relation instead of as one database file.

4. For PostgreSQL, the largest imported Sakila tables were `rental` and `payment`. Their estimated relation pages were 150 and 136 pages respectively, and the total six-table lab schema size including indexes was 4080 kB.

5. PostgreSQL query timing was faster in this run: `rental` averaged 9.078 ms and `payment` averaged 8.072 ms. SQLite was simpler to inspect, while PostgreSQL showed the stronger server-style execution behavior.

6. SQLite did not show a separate long-running server process. PostgreSQL showed multiple `postgres` processes, which confirms the architectural difference between embedded SQLite and server-based PostgreSQL.

7. TCP loopback traffic to `localhost:5432` stays inside the operating system and does not touch the physical network card. This is why local PostgreSQL connections can happen without external network hardware.
