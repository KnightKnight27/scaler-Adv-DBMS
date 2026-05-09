# SQLite3 vs PostgreSQL

Storage, page layout, memory behavior, and warm-cache query timings for the same `users` dataset on Windows.

**Author:** Tejas Varshney  
**Date:** 2026-05-07  
**Environment:** Windows 10 (build 26220), PowerShell 5.1  
**Versions:** SQLite 3.44.2 · PostgreSQL 16.13

> This README is a polished lab report. The SQL scripts, database files, and raw command output are in the `lab/` folder.

## Overview

The goal of this experiment is to compare SQLite and PostgreSQL using the same table shape, the same row count, and the same kinds of queries. The comparison focuses on three questions:

1. How much disk space does each engine use for the same data?
2. How do page size and memory-mapping affect SQLite?
3. Why does PostgreSQL often win on scan-heavy workloads?

## Dataset and Schema

The benchmark uses one table, `users`, with 200,000 rows and 6 columns: `id`, `name`, `email`, `age`, `city`, and `bio`. The `bio` column is a fixed ~140-byte string to make I/O and scan costs visible. Secondary indexes exist on `city` and `age` in both engines.

```sql
CREATE TABLE users (
    id    INTEGER PRIMARY KEY,
    name  TEXT NOT NULL,
    email TEXT NOT NULL,
    age   INTEGER,
    city  TEXT,
    bio   TEXT
);

CREATE INDEX idx_users_city ON users(city);
CREATE INDEX idx_users_age  ON users(age);
```

## Setup Notes

SQLite and PostgreSQL were installed and verified locally.

```powershell
PS> sqlite3 -version
PS> & "C:\Program Files\PostgreSQL\16\bin\psql.exe" --version
PS> Get-Service postgresql-x64-16
```

## Findings at a Glance

| Topic | SQLite | PostgreSQL |
| --- | --- | --- |
| Default page size | 4 KB | 8 KB |
| Table storage | Single `.db` file | Separate relation files under `base/<oid>/` |
| Memory model | Per-connection pager cache, optional `mmap` | Shared buffer pool + backend processes |
| Full-scan behavior | Single-threaded per connection | Parallel sequential scan |
| Index probe behavior | Fast, near 1 ms | Fast, near 1 ms |

## SQLite Results

### Disk footprint

PowerShell does not provide `ls -lh`, so file sizes were checked with `Get-ChildItem`.

| File | Bytes | Size |
| --- | ---: | ---: |
| `sample.db` | 50,700,288 | 48.35 MB |
| `sample_8k.db` | 49,463,296 | 47.17 MB |
| `sample_16k.db` | 49,201,152 | 46.92 MB |

The 8 KB and 16 KB variants were created from the same source data to compare how page size affects layout.

### Pager and PRAGMA values

```powershell
PS lab> sqlite3 sample.db "PRAGMA page_size; PRAGMA page_count; PRAGMA freelist_count;
                           PRAGMA cache_size; PRAGMA mmap_size; PRAGMA journal_mode;
                           PRAGMA synchronous; PRAGMA encoding;"
```

| Setting | Value | Meaning |
| --- | ---: | --- |
| `page_size` | 4096 | 4 KB pages |
| `page_count` | 12378 | Matches the file size on disk |
| `freelist_count` | 0 | No free pages |
| `cache_size` | -2000 | 2 MB cache |
| `mmap_size` | 0 | Memory mapping is off by default |
| `journal_mode` | delete | Rollback journal |
| `synchronous` | 2 | FULL |
| `encoding` | UTF-8 | Database encoding |

SQLite ships with `mmap_size = 0`, so pages are read through buffered `read()` calls unless memory mapping is enabled manually.

### Page-size comparison

| Page size | Page count | File size | Bytes / row |
| --- | ---: | ---: | ---: |
| 4 KB | 12,378 | 48.35 MB | 253 |
| 8 KB | 6,038 | 47.17 MB | 247 |
| 16 KB | 3,003 | 46.92 MB | 246 |

Larger pages reduce per-page overhead and slightly improve B-tree fan-out, but the savings are modest because the row payload is dominated by the `bio` column.

### mmap experiment

```sql
.timer ON
PRAGMA mmap_size = 0;            -- or 268435456 for 256 MB
SELECT SUM(length(bio)) FROM users;
SELECT SUM(length(bio)) FROM users;
SELECT SUM(length(bio)) FROM users;
SELECT city, COUNT(*), AVG(age) FROM users GROUP BY city ORDER BY city;
SELECT COUNT(*) FROM users WHERE age = 25;
```

Warm-cache averages:

| Query | mmap = 0 | mmap = 256 MB | Speedup |
| --- | ---: | ---: | ---: |
| `SUM(length(bio))` | 148 ms | 107 ms | ~1.4× |
| `GROUP BY city, COUNT(*), AVG(age)` | 665 ms | 220 ms | ~3.0× |
| `WHERE age = 25` | 1 ms | 1 ms | ~1× |

The main takeaway is simple: mapping the file removes per-page copy overhead, which matters most on scan-heavy queries.

### Process model

```powershell
PS> Get-Process | Where-Object { $_.ProcessName -match 'sqlite' } |
    Select-Object Id, ProcessName,
        @{N='WS_MB';E={[math]::Round($_.WorkingSet/1MB,2)}},
        @{N='VM_MB';E={[math]::Round($_.VirtualMemorySize/1MB,2)}}, StartTime
```

SQLite runs inside a single process. There is no separate server and no background worker pool.

## PostgreSQL Results

### Relation sizes and file layout

```sql
SHOW block_size;

SELECT relname, relpages, reltuples::bigint AS rows,
       pg_size_pretty(pg_relation_size(oid)) AS size
FROM pg_class
WHERE relname IN ('users','idx_users_city','idx_users_age','users_pkey')
ORDER BY relname;
```

| Relation | Pages | Rows | Size |
| --- | ---: | ---: | ---: |
| `idx_users_age` | 176 | 200000 | 1408 kB |
| `idx_users_city` | 172 | 200000 | 1376 kB |
| `users` | 6241 | 200000 | 49 MB |
| `users_pkey` | 551 | 200000 | 4408 kB |

PostgreSQL uses 8 KB pages by default. Its storage is split across the heap, indexes, and auxiliary files such as the visibility map and free-space map.

### Memory settings

| Parameter | Setting | Role |
| --- | ---: | --- |
| `block_size` | 8192 | Page size on disk and in shared buffers |
| `shared_buffers` | 16384 | 128 MB shared page cache |
| `effective_cache_size` | 524288 | 4 GB optimizer hint |
| `work_mem` | 4096 | 4 MB per sort/hash operation |
| `wal_buffers` | 512 | 4 MB WAL ring buffer |
| `max_parallel_workers_per_gather` | 2 | Parallel query limit |

PostgreSQL does not expose a per-connection `mmap_size` equivalent. Its cache behavior is managed through the server and shared memory.

### Query timing

| Query | PostgreSQL average |
| --- | ---: |
| Full scan `SUM(length(bio))` | 73 ms |
| `GROUP BY city, COUNT(*), AVG(age)` | 51 ms |
| `WHERE age = 25` | 0.7 ms |

### Execution notes

`EXPLAIN (ANALYZE, BUFFERS)` shows why PostgreSQL is faster on the heavier queries:

| Query pattern | Plan feature | Meaning |
| --- | --- | --- |
| Full scan | Parallel Seq Scan | The table is scanned by the leader and workers |
| Group by | HashAggregate + Gather Merge | Aggregation is parallelized |
| Index probe | Index Only Scan | The visibility map avoids heap fetches |

### Process model

PostgreSQL is a multi-process server. Even when idle, it keeps the postmaster and background workers running, and query parallelism can temporarily add more worker processes.

## Comparison Summary

| Aspect | SQLite | PostgreSQL |
| --- | --- | --- |
| Storage unit | Single database file | Cluster directory with many relation files |
| Page size | 4 KB default, tunable per database | 8 KB fixed at build time |
| Total size for this dataset | 48.35 MB | 56 MB |
| Full scan | Slower without mmap | Faster with parallel scan |
| Group by | Much slower without mmap | Much faster with hash aggregate |
| Index probe | Very fast | Very fast |
| Memory model | Lightweight, in-process | Heavier, server-based |

## Conclusions

SQLite is compact and simple. Its performance improves noticeably when `mmap_size` is enabled, especially on scan-heavy workloads. PostgreSQL is heavier operationally, but its parallel execution, shared buffers, and executor strategies make it faster on the same data for analytical queries.

If you want to extend this report, the most useful next additions would be the raw `EXPLAIN` plans, the exact SQL used to generate the dataset, and screenshots or terminal captures from the benchmark runs.
