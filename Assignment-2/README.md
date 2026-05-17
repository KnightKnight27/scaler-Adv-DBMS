# Lab 2 - SQLite3 vs PostgreSQL Comparison

**Tanishq | 24BCS10303**

---

## Setup

- SQLite3 3.45.1 on Ubuntu 22.04
- PostgreSQL 16.2 installed via apt
- Both tested with a `users` table, 1000 rows (id, name, email, age)

Scripts used:
- `sqlite_explore.sh` — sets up sample DB, checks page info, runs mmap tests
- `psql_explore.sh` — sets up sample DB, checks block/buffer info, runs EXPLAIN ANALYZE

---

## Observations

### SQLite3

```
$ ls -lh lab2.db
-rw-r--r-- 1 tanishq tanishq 40K May 9 11:04 lab2.db

$ sqlite3 lab2.db "PRAGMA page_size;"
4096

$ sqlite3 lab2.db "PRAGMA page_count;"
10

$ sqlite3 lab2.db "PRAGMA journal_mode;"
delete
```

Query time without mmap:
```
real    0m0.009s
user    0m0.005s
sys     0m0.003s
```

Query time with mmap (256MB):
```
real    0m0.006s
user    0m0.004s
sys     0m0.002s
```

The difference with mmap is small at 1000 rows since the whole file fits in cache anyway. mmap tells SQLite to map the database file directly into the process address space so it avoids an extra copy between the kernel buffer and user space. With bigger datasets this starts to matter more.

`ps aux | grep sqlite` during a query showed the sqlite3 process holding the file open. No server process — it's all in the same process as the client.

### PostgreSQL

```
block_size = 8192

 relation_bytes | page_count
----------------+------------
          73728 |          9

shared_buffers = 128MB
```

EXPLAIN ANALYZE:
```
Seq Scan on users  (cost=0.00..17.00 rows=1000 width=30)
                   (actual time=0.011..0.095 rows=1000 loops=1)
Planning Time: 0.042 ms
Execution Time: 0.128 ms
```

---

## Comparison

| Metric | SQLite3 | PostgreSQL |
|---|---|---|
| Page size | 4096 bytes | 8192 bytes |
| Pages used (1000 rows) | 10 | 9 |
| Disk usage | ~40 KB (single file) | ~72 KB (server-managed) |
| Query time (SELECT * 1000 rows) | ~9 ms | ~0.13 ms |
| mmap support | Yes, via PRAGMA mmap_size | No (uses shared_buffers) |
| Architecture | Embedded, no server | Client-server |
| Concurrency | File-level locking (WAL improves this) | MVCC, row-level locking |

---

## Notes

**Page size**: PostgreSQL uses 8 KB pages by default, baked in at compile time. SQLite defaults to 4 KB but you can change it per database using `PRAGMA page_size` before creating any tables. Larger pages mean fewer I/O operations for sequential reads but can waste space when rows are small.

**Query timing caveat**: The SQLite timing includes process startup overhead because `time` wraps the entire `sqlite3` invocation. PostgreSQL's EXPLAIN ANALYZE measures only the query inside the server process. So the numbers aren't a direct apples-to-apples comparison — PostgreSQL looks faster partly because startup cost isn't counted.

**mmap in SQLite**: `PRAGMA mmap_size=268435456` maps 256MB of the file into the process's virtual address space. Instead of going through `read()` syscalls that copy data into a user-space buffer, the process accesses pages directly from the kernel's page cache. One less copy. The gain here was about 3ms, which is small for 1000 rows but would scale with dataset size.

**PostgreSQL and shared_buffers**: PostgreSQL doesn't use mmap for data files. It manages its own buffer pool in shared memory (`shared_buffers`, default 128MB). All backends share this pool. It's a different tradeoff — more predictable memory usage and the server controls eviction, but it means one extra copy compared to mmap.

**Concurrency**: Not benchmarked here but it's the biggest real-world difference. SQLite locks the whole database file on writes, which limits concurrent write throughput. PostgreSQL uses MVCC so readers don't block writers and vice versa.

---

## Commands Reference

```bash
# SQLite3
sqlite3 lab2.db "PRAGMA page_size;"
sqlite3 lab2.db "PRAGMA page_count;"
sqlite3 lab2.db "PRAGMA journal_mode;"
sqlite3 lab2.db "PRAGMA mmap_size=0;"
sqlite3 lab2.db "PRAGMA mmap_size=268435456;"
time sqlite3 lab2.db "SELECT * FROM users;" > /dev/null
ps aux | grep sqlite

# PostgreSQL
psql -U postgres -c "SHOW block_size;"
psql -U postgres -c "SHOW shared_buffers;"
psql -U postgres -d lab2_test -c "SELECT pg_relation_size('users');"
psql -U postgres -d lab2_test -c "SELECT pg_relation_size('users') / current_setting('block_size')::int AS pages;"
psql -U postgres -d lab2_test -c "EXPLAIN ANALYZE SELECT * FROM users;"
```
