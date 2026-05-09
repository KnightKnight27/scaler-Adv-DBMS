# Lab 2 - SQLite3 vs PostgreSQL Comparison

---
## SQLite3

Created a database `users.db` and made a table called `users` (columns - id, name, email, age, city). Inserted 10,000 rows into it.

### File size

```
$ ls -lh users.db -> 484kb

So with 10k rows the db file is about 484 KB.

### Page size and page count

sqlite> PRAGMA page_size;
4096

sqlite> PRAGMA page_count;
121


Page size is 4096 bytes (4 KB) and there are 121 pages. That gives us 121 * 4096 = ~484 KB which matches the file size.

### mmap experiment

By default mmap is off:

sqlite> PRAGMA mmap_size;
0

Ran `SELECT * FROM users` with timer on, first without mmap then with it:

**Without mmap:**

sqlite> PRAGMA mmap_size = 0;
sqlite> .timer on
sqlite> SELECT * FROM users;
Run Time: real 0.007  user 0.004449  sys 0.000524


**With mmap (set to 256 MB):**

sqlite> PRAGMA mmap_size = 268435456;
sqlite> SELECT * FROM users;
Run Time: real 0.006  user 0.004835  sys 0.000422


There's a small improvement with mmap on. The sys time went down because with mmap the OS maps the file into memory directly so sqlite doesn't need to do separate read() calls. The difference is small here since the db is only 484 KB and fits in cache anyway.


## PostgreSQL

Created a database `lab2_test` and made the same `users` table (columns - id, name, email, age, city). Inserted 10,000 rows into it.

### Page size and page count

lab2_test=# SHOW block_size;
8192

lab2_test=# ANALYZE users;
lab2_test=# SELECT relpages, reltuples FROM pg_class WHERE relname = 'users';
relpages -> 96
reltuples -> 10000

lab2_test=# SELECT pg_size_pretty(pg_total_relation_size('users')) AS total_size;
1040kb

Page size is 8192 bytes (8 KB) and there are 96 pages. Total relation size comes to 1040 KB which includes indexes and other overhead.

### Query timing

Ran `SELECT * FROM users` with timing on, multiple times:

lab2_test=# \timing on

lab2_test=# SELECT * FROM users;
Time: 7.673 ms

lab2_test=# SELECT * FROM users;
Time: 8.530 ms

lab2_test=# SELECT * FROM users;
Time: 4.309 ms

First couple of runs were around 7-8 ms. Third run dropped to ~4 ms since the data was already cached in shared_buffers by then.

---

## Comparison

### Page size

| | Page Size |
|--|-----------|
| SQLite3 | 4 KB |
| PostgreSQL | 8 KB |

Postgres pages are 2x bigger. Bigger pages mean fewer I/O ops for sequential scans but can waste space if rows are small.

### Page count

| | Pages | Total Size |
|--|-------|------------|
| SQLite3 | 121 | 484 KB |
| PostgreSQL | 96 | 1040 KB |

Postgres has fewer pages since each one is bigger, but the total size is more than double. The extra space comes from MVCC headers on every row (~23 bytes each), indexes, and other internal metadata. SQLite packs data much tighter.

### Query performance

| | Time |
|--|------|
| SQLite3 (mmap off) | 7 ms |
| SQLite3 (mmap on) | 6 ms |
| PostgreSQL (first run) | 7.6 ms |
| PostgreSQL (cached) | 4.3 ms |


### mmap impact

With mmap on in SQLite, real time dropped from 7ms to 6ms and sys time went down by about 19%. The idea is that instead of doing read() system calls, the OS maps the whole file into memory and sqlite just reads it like normal memory. For our small 484 KB database the difference is minor, but it would matter more for larger databases.

Postgres doesn't use mmap at all. It has its own buffer pool (shared_buffers, defaults to 128 MB) and manages caching internally.

