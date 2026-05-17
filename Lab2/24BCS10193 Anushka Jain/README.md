# Lab 2 — SQLite3 vs PostgreSQL: Storage Internals Deep Dive

**Anushka Jain | 24BCS10193**  
Advanced DBMS — Scaler

---

## The Problem We Were Trying to Solve

When you run `SELECT * FROM users` in SQLite versus PostgreSQL, the query looks identical — but under the hood, everything is different. Page sizes differ, memory management strategies differ, the entire architecture differs. This lab forced us to stop treating databases as black boxes and actually look at *how* they store data on disk, *how* they cache it in memory, and *why* those design choices impact query speed.

The assignment: install both databases, run the same workload (1000-row table), and compare them on page size, page count, mmap behavior, and query timing. No screenshots — only a markdown report. That constraint matters because it means you actually had to run the commands and read the numbers yourself.

---

## Setup

- Ubuntu 22.04 (WSL2 / native Linux)
- SQLite3 3.45.1
- PostgreSQL 16.x (default apt install)
- Test table: `students` — 1000 rows with `id, name, email, score, dept`
- Scripts: `sqlite_explore.sh` and `psql_explore.sh` (both in this folder — just run them)

```bash
chmod +x sqlite_explore.sh psql_explore.sh
./sqlite_explore.sh
./psql_explore.sh
```

---

## What We Actually Observed

### SQLite3

```
$ ls -lh anushka_lab2.db
-rw-r--r-- 1 anushka anushka 52K May 8 22:10 anushka_lab2.db

$ sqlite3 anushka_lab2.db "PRAGMA page_size;"
4096

$ sqlite3 anushka_lab2.db "PRAGMA page_count;"
13

$ sqlite3 anushka_lab2.db "PRAGMA journal_mode;"
delete

$ sqlite3 anushka_lab2.db "PRAGMA freelist_count;"
0
```

**Query timing — without mmap:**
```
real    0m0.009s
user    0m0.006s
sys     0m0.003s
```

**Query timing — with mmap (64 MB):**
```
real    0m0.007s
user    0m0.004s
sys     0m0.002s
```

**Query timing — with mmap (256 MB):**
```
real    0m0.006s
user    0m0.004s
sys     0m0.001s
```

**Aggregate query (GROUP BY dept, AVG score) — no mmap:**
```
real    0m0.010s
user    0m0.007s
sys     0m0.002s
```

---

### PostgreSQL

```
block_size = 8192

 relation_bytes | relation_pretty | page_count
----------------+-----------------+------------
          81920 | 80 kB           |         10

shared_buffers = 128MB
work_mem = 4MB
```

**EXPLAIN ANALYZE — full table scan:**
```
Seq Scan on students  (cost=0.00..20.00 rows=1000 width=38)
                      (actual time=0.015..0.112 rows=1000 loops=1)
Planning Time: 0.058 ms
Execution Time: 0.148 ms
```

**EXPLAIN ANALYZE — aggregate query:**
```
HashAggregate  (cost=22.50..24.50 rows=4 width=44)
               (actual time=0.201..0.205 rows=4 loops=1)
  Group Key: dept
  ->  Seq Scan on students  (cost=0.00..20.00 rows=1000 width=16)
                            (actual time=0.013..0.089 rows=1000 loops=1)
Planning Time: 0.072 ms
Execution Time: 0.234 ms
```

**Buffer hit ratio (after first run — warm cache):**
```
 disk_reads | cache_hits | hit_ratio_pct
------------+------------+---------------
          0 |         10 |        100.00
```

---

## Comparison Table

| Metric | SQLite3 | PostgreSQL |
|--------|---------|------------|
| Page size | 4096 bytes (4 KB) | 8192 bytes (8 KB) |
| Pages used (1000 rows) | 13 | 10 |
| Database file size | ~52 KB (single file) | ~80 KB (cluster managed by server) |
| Query time — `SELECT *` 1000 rows | ~9 ms (includes process startup) | ~0.15 ms (server-side only) |
| mmap support | Yes — `PRAGMA mmap_size` | No — uses `shared_buffers` pool |
| Buffer/cache mechanism | OS page cache + optional mmap | Self-managed shared memory |
| Architecture | Embedded, serverless, single file | Client-server, persistent daemon |
| Concurrency model | File-level locking (WAL mode helps) | MVCC — row-level, non-blocking reads |
| Journal / WAL | `delete` mode by default (WAL optional) | WAL always on |

---

## The Interesting Bits — What the Numbers Actually Mean

### Page Size: Why Does It Matter?

SQLite uses 4 KB pages by default; PostgreSQL uses 8 KB, baked in at compile time. This is not a random choice. A larger page means:
- Fewer I/O operations for sequential scans (fewer trips to disk per MB of data)
- More wasted space if your rows are tiny (you read 8 KB to get 3 rows)
- Higher memory pressure per cached page

For a 1000-row student table, SQLite needed 13 pages while PostgreSQL needed 10 — PostgreSQL's bigger pages fit more rows per page, so the total count is lower even though individual pages are twice as large.

### mmap: What It Actually Does

By default, SQLite uses `read()` syscalls: kernel copies data from disk into a kernel page frame, then copies it again into SQLite's user-space buffer. Two copies total.

`PRAGMA mmap_size=268435456` tells SQLite to memory-map the database file instead. The kernel maps pages directly into SQLite's virtual address space. When SQLite accesses data, it's accessing kernel memory directly — one copy instead of two.

On our 1000-row dataset, the improvement is small (9 ms → 6 ms) because the whole database fits in cache either way. On a multi-GB database where data is spilling in and out of cache, mmap makes a much bigger difference because it eliminates the `copy_from_user` / `copy_to_user` round-trips that happen on every `read()`.

PostgreSQL doesn't use mmap for data files at all. It manages its own `shared_buffers` pool in shared memory — a deliberate design choice that gives it more control over what gets evicted and when. Database engines that self-manage their buffer pool can implement smarter replacement policies (like PostgreSQL's clock-sweep) instead of relying on the OS's LRU.

### Query Timing: Why PostgreSQL Looks "Faster"

The SQLite number (9 ms) includes:
- Forking/exec-ing the `sqlite3` process
- Parsing the command-line argument
- Opening the file
- Running the query
- Printing output (redirected to /dev/null)
- Process teardown

PostgreSQL's EXPLAIN ANALYZE number (0.148 ms) measures *only* the query inside the already-running server. The connection overhead, process startup, and network round-trip are not counted. A fair comparison would need both measured the same way — for this scale, SQLite is absolutely competitive.

### Concurrency: The Biggest Real-World Difference

We didn't benchmark this, but it's worth knowing: SQLite acquires an exclusive write lock on the entire database file for the duration of a write. One concurrent writer blocks all readers (unless WAL mode is enabled, which relaxes this to allow concurrent readers). PostgreSQL's MVCC lets reads and writes happen simultaneously without blocking each other — readers see a consistent snapshot, writers don't block them. For a web application with many concurrent users, this is the deciding factor between the two.

---

## Problems We Hit and How We Fixed Them

### 1. SQLite3 not installed
`command not found: sqlite3` on a fresh Ubuntu install.  
Fix: `sudo apt update && sudo apt install sqlite3`

### 2. PostgreSQL peer authentication failure
Running `psql -U postgres` from our normal user account threw `FATAL: Peer authentication failed`.  
The default PostgreSQL setup on Ubuntu uses peer auth — it checks that your Linux username matches the database username. We had two options:
- Switch to the postgres system user: `sudo -u postgres psql`
- Or edit `/etc/postgresql/*/main/pg_hba.conf`, change `peer` to `md5` for local connections, then `sudo systemctl restart postgresql`

We went with `sudo -u postgres` for the script since it's non-destructive.

### 3. `PRAGMA mmap_size` doesn't persist between `sqlite3` invocations
Setting `PRAGMA mmap_size` inside one `sqlite3` session doesn't carry over to the next time you open the file. Each invocation of the `sqlite3` CLI starts fresh. This means the `time sqlite3 ...` commands in the script are technically always testing with mmap off unless we set it in the same session as the query. We worked around this by running `PRAGMA mmap_size=...` and the query in the same heredoc/session.

### 4. `pg_statio_user_tables` showed zero hits on first run
The buffer hit ratio query returned `NULL` or zeros on the very first run because PostgreSQL hadn't tracked any I/O stats yet for the newly created table. Running the SELECT query first and *then* checking stats gave the real numbers.

### 5. Timing SQLite fairly
The `time` command wraps the entire `sqlite3` process invocation, not just the query. Early on our numbers looked dramatically slower than PostgreSQL. We clarified in the report that the comparison isn't apples-to-apples — PostgreSQL's EXPLAIN ANALYZE measures only internal server execution time, not the full round-trip.

---

## Commands Reference

```bash
# ---- SQLite3 ----------------------------------------------------------------
sqlite3 mydb.db "PRAGMA page_size;"
sqlite3 mydb.db "PRAGMA page_count;"
sqlite3 mydb.db "PRAGMA freelist_count;"
sqlite3 mydb.db "PRAGMA cache_size;"
sqlite3 mydb.db "PRAGMA journal_mode;"
sqlite3 mydb.db "PRAGMA mmap_size=268435456;"   # set mmap to 256 MB
sqlite3 mydb.db "PRAGMA mmap_size=0;"            # disable mmap

time sqlite3 mydb.db "SELECT * FROM students;" > /dev/null
ps aux | grep sqlite3

# ---- PostgreSQL -------------------------------------------------------------
psql -U postgres -c "SHOW block_size;"
psql -U postgres -d mydb -c "SELECT pg_relation_size('students');"
psql -U postgres -d mydb -c "
  SELECT pg_relation_size('students') / current_setting('block_size')::int
  AS page_count;
"
psql -U postgres -d mydb -c "SHOW shared_buffers;"
psql -U postgres -d mydb -c "EXPLAIN ANALYZE SELECT * FROM students;"
psql -U postgres -d mydb -c "
  SELECT heap_blks_read, heap_blks_hit
  FROM pg_statio_user_tables WHERE relname = 'students';
"
```

---

## Files in This Submission

| File | What It Does |
|------|-------------|
| `sqlite_explore.sh` | Creates a 1000-row SQLite DB, checks PRAGMA values, runs mmap timing tests |
| `psql_explore.sh` | Creates matching PostgreSQL DB, checks block size/page count, runs EXPLAIN ANALYZE |
| `README.md` | This report: observations, comparisons, problems faced, analysis |
| `Assignment.md` | Original assignment specification |
