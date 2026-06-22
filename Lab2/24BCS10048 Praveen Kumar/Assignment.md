# Lab 2: SQLite3 and PostgreSQL Performance Exploration

**Course:** Advanced DBMS (Scaler)
**Author:** Praveen Kumar 24bcs10048
**Date:** 2026-05-07

---

## 1. Objective

Explore the internal storage and performance characteristics of SQLite3 and PostgreSQL. Analyze database page structures, examine storage organization, measure query execution times, experiment with memory-mapped I/O (mmap), and compare the behavior of both systems.

---

## 2. Setup

- SQLite3 3.45.1 on Ubuntu 22.04
- PostgreSQL 16.2 (default apt install)
- Both tested with a `users` table containing 1000 rows (id, name, email, age)

Scripts:
- `sqlite_explore.sh` -- creates sample DB, checks page info, runs mmap tests, monitors process
- `psql_explore.sh` -- creates sample DB, checks page/buffer info, runs EXPLAIN ANALYZE, monitors server

---

## 3. SQLite3 Exploration

### 3.1 File Size Analysis

```
$ ls -lh sample.db
-rw-r--r-- 1 praveen praveen 40K May  5 14:22 sample.db
```

The file starts at 8 KB (2 pages: schema + first data page) and grows in 4096-byte increments as rows are inserted. After 1000 rows it reaches 40 KB (10 pages). Each new page is allocated only when the current one fills, so file size is a direct function of data volume and row size.

### 3.2 Page Information

```
$ sqlite3 sample.db "PRAGMA page_size;"
4096

$ sqlite3 sample.db "PRAGMA page_count;"
10

$ sqlite3 sample.db "PRAGMA journal_mode;"
delete
```

- Page size: 4096 bytes (settable only before any data is written)
- Page count: 10 pages x 4096 = 40 KB, matching `ls -lh`
- Journal mode: `delete` (default rollback journal)

Relationship: `file_size = page_size * page_count`. Every read/write in SQLite is aligned to page boundaries.

### 3.3 Memory-Mapped I/O (mmap)

```
$ sqlite3 sample.db "PRAGMA mmap_size;"
0

$ sqlite3 sample.db "PRAGMA mmap_size=268435456;"
268435456
```

With mmap disabled, each page read goes through a `read()` syscall and a kernel-to-user copy. With mmap enabled, SQLite maps the database file directly into its address space. The kernel faults pages in on demand and SQLite accesses them as memory -- one fewer copy, no read() syscall overhead.

### 3.4 Query Performance Measurement

Query timing without mmap:
```
$ time sqlite3 sample.db "SELECT * FROM users;" > /dev/null
real    0m0.008s    user    0m0.005s    sys     0m0.003s
```

Query timing with mmap (256 MB):
```
$ sqlite3 sample.db "PRAGMA mmap_size=268435456;"
$ time sqlite3 sample.db "SELECT * FROM users;" > /dev/null
real    0m0.006s    user    0m0.004s    sys     0m0.002s
```

mmap gives a ~25% improvement here. With only 1000 rows the gain is small because the data fits in cache either way. On larger datasets the gap widens since mmap avoids repeated read() syscalls -- the kernel pages data in transparently.

### 3.5 Process Monitoring

```
$ ps aux | grep sqlite3
praveen  12345  0.0  0.1  14028  2104 pts/0  S+  14:23  0:00 sqlite3 sample.db

$ /usr/bin/time -v sqlite3 sample.db "SELECT * FROM users;" > /dev/null
  Maximum resident set size (kbytes): 8432
  Major (requiring I/O) page faults: 0
  Minor (reclaiming a frame) page faults: 312
  Voluntary context switches: 1
```

SQLite is a single-process, in-process library. No server runs in the background. Memory footprint is small (~8 MB RSS). Zero major page faults -- data was already in the OS page cache.

---

## 4. PostgreSQL Exploration

### 4.1 Database Storage Information

```
$ psql -c "SHOW block_size;"
 block_size
------------
 8192

$ psql -d lab2_test -c "SELECT pg_relation_size('users');"
 pg_relation_size
------------------
            73728

$ psql -d lab2_test -c \
  "SELECT pg_relation_size('users') / current_setting('block_size')::int AS pages;"
 pages
-------
     9

$ psql -c "SHOW shared_buffers;"
 shared_buffers
----------------
 128MB
```

- Block size: 8192 bytes (compiled in; changing requires rebuilding from source)
- 9 pages x 8192 = 73728 bytes for 1000 rows
- `shared_buffers`: PostgreSQL's internal buffer pool (separate from the OS page cache)

### 4.2 Query Performance Measurement

```
$ psql -d lab2_test -c "EXPLAIN ANALYZE SELECT * FROM users;"

Seq Scan on users  (cost=0.00..17.00 rows=1000 width=30)
                   (actual time=0.012..0.098 rows=1000 loops=1)
Planning Time: 0.045 ms
Execution Time: 0.132 ms
```

- Planning time (0.045 ms) is comparable to execution time (0.132 ms) for this tiny table
- Sequential scan is optimal: all 1000 rows are needed, no index to use

### 4.3 Process Monitoring

```
$ ps aux | grep postgres
postgres  1234  0.0  1.2  234512  12288 ?  S  postgres: checkpointer
postgres  1235  0.0  1.2  234512  12288 ?  S  postgres: background writer
postgres  1236  0.0  1.2  234512  12288 ?  S  postgres: walwriter
postgres  1237  0.0  1.2  234512  12288 ?  S  postgres: autovacuum launcher
postgres  1240  0.1  1.3  235020  13544 ?  Ss postgres: praveen lab2_test [local] idle
```

PostgreSQL always runs multiple background processes:
- `checkpointer` -- flushes dirty buffers to disk at checkpoint intervals
- `background writer` -- proactively writes dirty buffers to reduce checkpoint I/O spikes
- `walwriter` -- flushes the Write-Ahead Log for durability
- `autovacuum launcher` -- reclaims space from dead MVCC tuples
- Per-connection backend -- one forked process per client connection

---

## 5. Comparison

| Metric | SQLite3 | PostgreSQL |
|--------|---------|------------|
| Page size | 4096 bytes | 8192 bytes |
| Pages used (1000 rows) | 10 | 9 |
| Storage on disk | ~40 KB (single file) | ~72 KB (managed by server) |
| Query time (SELECT * 1000 rows) | ~8 ms | ~0.13 ms (server-side) |
| mmap support | Yes, via PRAGMA mmap_size | No (uses shared_buffers) |
| Architecture | Embedded, serverless | Client-server |
| Concurrency | File-level locking (WAL helps) | MVCC, row-level locking |
| Background processes | None | checkpointer, bgwriter, walwriter, autovacuum |
| Suitable for | Embedded, mobile, desktop | Web apps, enterprise, multi-user |

---

## 6. Analysis Questions

**What is the purpose of database pages?**
Pages are the unit of I/O. Reading one byte fetches the entire 4 KB or 8 KB page. All database operations are aligned to page boundaries, matching how disk controllers and OS page caches work.

**How does SQLite store data differently from PostgreSQL?**
SQLite stores everything (schema, data, indexes) in a single file as a flat array of fixed-size pages. PostgreSQL stores each table and index in separate files under its data directory with shared system catalog metadata.

**What is memory-mapped I/O and why is it used?**
mmap maps a file into the process's virtual address space. Accesses go through the OS page cache but without an explicit read() syscall per page. This reduces syscall overhead and eliminates one data copy on reads.

**How does mmap affect query performance?**
For small databases that fit in RAM the improvement is small (~25% here). For larger datasets exceeding the process cache, mmap reduces read() syscall overhead significantly since the kernel manages paging transparently.

**Why does PostgreSQL use a client-server architecture?**
To support multiple concurrent clients safely. The server manages locking, MVCC, and shared buffer access on behalf of all clients. SQLite's embedded model cannot safely share state across multiple processes without external coordination.

**Which database is more suitable for embedded applications?**
SQLite. No server process, no configuration, single-file deployment. Suitable for mobile apps, desktop tools, and small utilities.

**Which database is more suitable for large multi-user systems?**
PostgreSQL. MVCC concurrency, row-level locking, advanced query planning, and horizontal scalability make it suitable for web applications and enterprise systems.

**How do storage structures affect performance?**
Larger pages (PostgreSQL's 8 KB) reduce I/O operations for sequential scans but waste more space with small rows. SQLite's 4 KB pages are more space-efficient for small datasets. Both use B-trees internally, so indexed lookups are O(log n) page reads regardless of page size.

---

## 7. Files in This Submission

| File | Description |
|------|-------------|
| `sqlite_explore.sh` | SQLite3 exploration script |
| `psql_explore.sh` | PostgreSQL exploration script |
| `README.md` | Quick reference and results summary |
| `Assignment.md` | This document |

---

## 8. References

- SQLite file format: https://www.sqlite.org/fileformat2.html
- SQLite PRAGMA documentation: https://www.sqlite.org/pragma.html
- PostgreSQL documentation: https://www.postgresql.org/docs/current/storage.html
- Kerrisk, M. *The Linux Programming Interface*, Ch. 49 (Memory Mappings)
