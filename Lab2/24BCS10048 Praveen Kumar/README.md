# Lab 2 - SQLite3 vs PostgreSQL Comparison

**Praveen Kumar | 24BCS10048**

---

## Setup

- SQLite3 3.45.1 on Ubuntu 22.04
- PostgreSQL 16.2 (default apt install)
- Both tested with a `users` table containing 1000 rows (id, name, email, age)

Scripts used:
- `sqlite_explore.sh` — creates sample DB, checks page info, runs mmap tests
- `psql_explore.sh` — creates sample DB, checks page/buffer info, runs EXPLAIN ANALYZE

---

## Observations

### SQLite3

```
$ ls -lh sample.db
-rw-r--r-- 1 praveen praveen 40K May  5 14:22 sample.db

$ sqlite3 sample.db "PRAGMA page_size;"
4096

$ sqlite3 sample.db "PRAGMA page_count;"
10

$ sqlite3 sample.db "PRAGMA journal_mode;"
delete
```

Query timing without mmap:
```
real    0m0.008s
user    0m0.005s
sys     0m0.003s
```

Query timing with mmap (256MB):
```
real    0m0.006s
user    0m0.004s
sys     0m0.002s
```

mmap gave a small improvement here, but with only 1000 rows the difference is negligible. The data fits in cache either way. On larger datasets the gap widens because mmap avoids extra read() syscalls — the kernel maps pages directly into the process address space.

### PostgreSQL

```
block_size = 8192

relation_bytes | page_count
          73728 |          9

shared_buffers = 128MB
```

EXPLAIN ANALYZE output:
```
Seq Scan on users  (cost=0.00..17.00 rows=1000 width=30)
                   (actual time=0.012..0.098 rows=1000 loops=1)
Planning Time: 0.045 ms
Execution Time: 0.132 ms
```

---

## Comparison

| Metric | SQLite3 | PostgreSQL |
|--------|---------|------------|
| Page size | 4096 bytes | 8192 bytes |
| Pages used (1000 rows) | 10 | 9 |
| Storage on disk | ~40 KB (single file) | ~72 KB (managed by server) |
| Query time (SELECT * 1000 rows) | ~8 ms | ~0.13 ms (server-side) |
| mmap support | Yes, via PRAGMA | N/A (uses shared_buffers) |
| Architecture | Embedded, serverless | Client-server |
| Concurrency | File-level locking (WAL helps) | MVCC, row-level locking |

### Notes

- **Page size difference**: PostgreSQL uses 8 KB pages by default (compiled in, rarely changed). SQLite defaults to 4 KB but can be configured per-database. Larger pages mean fewer I/O operations for sequential scans but can waste space with small rows.

- **Query timing caveat**: The SQLite number includes process startup time (`time` wraps the whole sqlite3 invocation). PostgreSQL's EXPLAIN ANALYZE measures only the query execution inside the server, which is why it looks faster. Apples-to-apples comparison would need both measured the same way.

- **mmap in SQLite**: Setting `PRAGMA mmap_size` tells SQLite to memory-map the database file instead of using read()/write() syscalls. This skips one copy (kernel buffer → user buffer) since the mapped pages are accessed directly. PostgreSQL doesn't use mmap for data files — it manages its own shared buffer pool in shared memory.

- **Concurrency**: This wasn't part of the benchmark, but it's the biggest practical difference. SQLite locks the entire database file for writes. PostgreSQL handles concurrent reads and writes via MVCC without blocking.

---

## Commands Reference

```bash
# SQLite
sqlite3 sample.db "PRAGMA page_size;"
sqlite3 sample.db "PRAGMA page_count;"
sqlite3 sample.db "PRAGMA mmap_size=268435456;"
sqlite3 sample.db "PRAGMA journal_mode;"
time sqlite3 sample.db "SELECT * FROM users;" > /dev/null
ps aux | grep sqlite

# PostgreSQL
psql -c "SHOW block_size;"
psql -d lab2_test -c "SELECT pg_relation_size('users');"
psql -d lab2_test -c "SELECT pg_relation_size('users') / current_setting('block_size')::int AS pages;"
psql -c "SHOW shared_buffers;"
psql -d lab2_test -c "EXPLAIN ANALYZE SELECT * FROM users;"
```
