# SQLite3 vs PostgreSQL: pages, mmap, and query timings

**Name:** Parth Sankhla
**Role Number:** 24BCS10229

## Setup

SQLite 3.53.0, PostgreSQL 18.3.

Same table loaded into both engines so the numbers are comparable:

```sql
users(id PK, name, email, bio, created_at)
```

200 000 rows, plus an index on `email`. Seed scripts are in `seed.sql` (sqlite) and `seed.psql` (postgres).

## SQLite3

### File size

```
$ ls -lh sample.db
-rw-r--r-- 1 shanks shanks 39M  sample.db
```

### PRAGMAs

```
sqlite> PRAGMA page_size;     -> 4096
sqlite> PRAGMA page_count;    -> 9882
sqlite> PRAGMA cache_size;    -> -2000   (negative => KiB, so 2 MiB)
sqlite> PRAGMA mmap_size;     -> 0       (mmap disabled by default)
sqlite> PRAGMA journal_mode;  -> delete
sqlite> PRAGMA encoding;      -> UTF-8
```

`page_size * page_count = 4096 * 9882 = 40 476 672` which matches the file
size. SQLite keeps the whole DB (heap + indexes + freelist) in this one
file as fixed-size pages.

### Changing mmap_size

`PRAGMA mmap_size = N` tells SQLite to map up to N bytes of the file
into the process's address space instead of going through the heap pager.
The clearest effect is on the process map. Snapshot of `ps` while a
scan was running:

```
mmap = 0          shanks  PID 25592   VSZ 8924   RSS 6664
mmap = 256 MiB    shanks  PID 25595   VSZ 46472  RSS 23368
```

VSZ jumps because the whole DB is now mapped in. RSS grows as pages get
faulted in.

### `time SELECT ...`

I dropped the OS page cache between cold runs (`echo 3 > /proc/sys/vm/drop_caches`)
so the timings reflect actual I/O instead of whatever Linux already had
buffered. Three runs each:

| query | mmap | cold runs (s) | warm runs (s) |
|---|---|---|---|
| `SELECT COUNT(*), SUM(LENGTH(bio)) FROM users` | 0       | 0.0417  0.0417  0.0415 | 0.0359  0.0285  0.0294 |
| same                                            | 256 MiB | 0.0455  0.0393  0.0469 | 0.0270  0.0300  0.0252 |
| `WHERE email = 'user_157891@example.com'`       | 0       | 0.0090  0.0091  0.0084 | 0.0035  0.0030  0.0029 |
| same                                            | 256 MiB | 0.0099  0.0094  0.0111 | 0.0035  0.0031  0.0028 |

The mmap difference is inside the run-to-run noise. A 39 MB DB on an
NVMe SSD reads in ~40 ms either way. mmap is supposed to help when the
DB is big and you do lots of random access, neither of which is true
here. What did move the numbers is cold vs warm: the full scan goes
from ~42 ms cold to ~28 ms warm, indexed lookup from ~9 ms to ~3 ms.

### `ps aux | grep sqlite3`

```
shanks  25595  46472  23368  R   sqlite3 sample.db   # mmap=256MB
shanks  25592   8924   6664  R   sqlite3 sample.db   # mmap=0
```

Same DB, same query, ~5x the virtual size with mmap on.

## PostgreSQL

Postgres has no `PRAGMA`. The equivalents come from `SHOW <guc>` and the
system catalogs.

### Page size

```
labdb=> SHOW block_size;
 8192
```

8 KiB, fixed at compile time. Twice SQLite's default.

### Page count

```sql
SELECT relname,
       relpages,
       pg_relation_size(oid) / current_setting('block_size')::int AS pages_on_disk,
       pg_size_pretty(pg_relation_size(oid))                      AS size
  FROM pg_class
 WHERE relname IN ('users', 'users_pkey', 'idx_users_email');
```

```
 relname          | relpages | pages_on_disk | size
------------------+----------+---------------+--------
 users            |     3364 |          3364 | 26 MB
 users_pkey       |      551 |           551 | 4408 kB
 idx_users_email  |      995 |           995 | 7960 kB
```

```
labdb=> SELECT pg_size_pretty(pg_database_size('labdb'));
 46 MB
```

Heap on disk is `26 MB` (3364 * 8 KiB). Each relation gets its own file,
plus `_fsm` (free space map) and `_vm` (visibility map):

```
$ sudo ls -lh /var/lib/postgres/data/base/16385/16390*
-rw-------  27M  16390
-rw-------  24K  16390_fsm
-rw-------  8.0K 16390_vm
```

### "mmap" in postgres

There isn't one in the SQLite sense. Postgres uses `shared_buffers`
(in-process buffer pool) and leans on the OS page cache for the rest.
Defaults from this cluster:

```
labdb=> SHOW shared_buffers;        -- 128MB
labdb=> SHOW effective_cache_size;  -- 4GB    (planner hint about OS cache)
labdb=> SHOW work_mem;              -- 4MB
```

So the closest comparable test is cold (after `systemctl restart postgresql`
plus drop_caches) vs warm.

### Timings

Three runs each, same two queries.

| query | cold runs (s) | warm runs (s) |
|---|---|---|
| `SELECT COUNT(*), SUM(LENGTH(bio)) FROM users` | 0.0964  0.1033  0.1048 | 0.0748  0.0699  0.0722 |
| `WHERE email = 'user_157891@example.com'`       | 0.0622  0.0701  0.0675 | 0.0418  0.0404  0.0406 |

`EXPLAIN (ANALYZE, BUFFERS)` for the full scan:

```
Finalize Aggregate  (actual time=37.877..41.058 rows=1)
  Buffers: shared hit=3364
  ->  Gather  Workers Planned: 2  Workers Launched: 2
        ->  Partial Aggregate
              ->  Parallel Seq Scan on users (rows=66666 loops=3)
Planning Time: 0.734 ms
Execution Time: 41.211 ms
```

For the indexed lookup:

```
Index Scan using idx_users_email on users
  (actual time=0.023..0.024 rows=1)
  Buffers: shared hit=4
Execution Time: 0.105 ms
```

The full scan reads exactly `relpages = 3364` blocks (`shared hit=3364`).
The index lookup hits 4 buffers: 3 levels of B-tree plus 1 heap page.

Worth noting: `Execution Time: 0.105 ms` for the index lookup, but the
end-to-end time from `psql` was ~40 ms warm. The other 99% is psql startup,
libpq round-trip, parse, plan, MVCC visibility, wire-format. That gap
shows up in every comparison below.

### `ps aux | grep postgres`

```
postgres  /usr/bin/postgres -D /var/lib/postgres/data       VSZ 227M  RSS 41M
postgres  postgres: io worker 0                             VSZ 227M  RSS 50M
postgres  postgres: io worker 1                             VSZ 227M  RSS 48M
postgres  postgres: checkpointer
postgres  postgres: background writer
postgres  postgres: walwriter
postgres  postgres: autovacuum launcher
postgres  postgres: logical replication launcher
```

Every backend has VSZ ~227 MB because each one attaches the 128 MB
`shared_buffers` shared-memory segment. SQLite's process was 9-46 MB
total. Different model entirely.

## Comparison

### Page size and count

|  | SQLite | PostgreSQL |
|---|---|---|
| default page / block size | 4096 B | 8192 B |
| pages used by data        | 9882 (whole DB in one file) | 3364 heap + 551 PK + 995 email idx |
| files on disk             | 1 | 1 per relation, plus `_fsm` and `_vm` siblings |
| configurable              | per-DB at creation | compile-time only |

Both engines expose `bytes_on_disk = page_size * page_count` as the
sanity check.

### Query timings (median of 3)

| query | sqlite cold | sqlite warm | postgres cold | postgres warm |
|---|---:|---:|---:|---:|
| FULL SCAN  | 41.7 ms | 29.4 ms | 103.3 ms | 72.2 ms |
| INDEX      |  9.0 ms |  3.0 ms |  67.5 ms | 40.6 ms |

Sqlite is faster on this workload, but the gap is mostly fixed cost,
not query work. Postgres' own `EXPLAIN ANALYZE` reports 0.1 ms for the
indexed lookup. The remaining ~40 ms is psql + IPC + protocol. Sqlite
is in-process, so there is no protocol overhead to pay.

Also, postgres parallelised the full scan with 2 workers without being
asked. On a much bigger table or a bigger machine this flips.

### mmap

| engine | mmap mechanism | observed effect |
|---|---|---|
| sqlite   | `PRAGMA mmap_size=N`, default 0 in CLI | timing change inside noise (~5%); VSZ goes from ~9 MB to ~46 MB, RSS roughly triples |
| postgres | no mmap; `shared_buffers` + OS page cache play the same role | cold->warm shrinks full scan by ~30%, indexed lookup by ~40% |

The lesson on this hardware is that mmap only matters when the DB is
much bigger than the buffer pool but the working set isn't, and you're
doing random access. With a 39 MB DB on NVMe none of that holds, so
mmap mainly changes how the process's memory looks rather than how
fast the query runs.

