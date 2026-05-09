# SQLite3 vs PostgreSQL — Storage and Query Performance Lab

**Name:** Krritin Keshan
**College ID:** 24bcs10122
**Date:** May 9, 2026
**Machine:** MacBook (macOS Darwin 25.3.0), zsh shell
**Versions used:** SQLite 3.43.2 | PostgreSQL 16.4

---

## What this lab is about

The idea was to actually open up two databases (SQLite and Postgres), poke around their internals, and see how they behave when you push some real data through them. Things like — how big is one page on disk, how many pages does my table use, what changes when I turn on memory mapping, and is Postgres really faster than SQLite on the same query?

I made one table called `book_catalog` in both engines and loaded it with **150,000 rows**. The schema is the same on both sides so the comparison stays fair.

```sql
CREATE TABLE book_catalog (
    book_id        INTEGER PRIMARY KEY,
    title          TEXT NOT NULL,
    author         TEXT NOT NULL,
    isbn           TEXT,
    pages          INTEGER,
    genre          TEXT,
    description    TEXT  -- padded to ~200 bytes per row to give the disk something real to do
);

CREATE INDEX idx_books_genre  ON book_catalog(genre);
CREATE INDEX idx_books_author ON book_catalog(author);
```

---

## Part 1 — Exploring SQLite3

### 1.1 Installing and checking the version

SQLite already ships with macOS, but I wanted a newer one so I installed it via Homebrew:

```bash
brew install sqlite
sqlite3 --version
# 3.43.2 2023-10-10 13:08:14
```

### 1.2 Looking at the file size

After loading 150k rows I checked the file size on disk:

```bash
ls -lh library.db
# -rw-r--r--  1 krritinkeshan  staff   36M May  9 21:14 library.db
```

So one file = the whole database. That's the SQLite way. Easy to copy around, easy to break too if you're not careful.

### 1.3 PRAGMA — getting under the hood

PRAGMA is basically how you ask SQLite about itself. I ran a few:

```sql
sqlite> PRAGMA page_size;
4096
sqlite> PRAGMA page_count;
9216
sqlite> PRAGMA cache_size;
-2000
sqlite> PRAGMA mmap_size;
0
sqlite> PRAGMA journal_mode;
delete
```

Quick math check: `4096 bytes × 9216 pages ≈ 37.7 MB` which lines up with the file size from `ls`. So SQLite literally is just a stack of 4 KB pages on disk.

The interesting one for me was **`mmap_size = 0`**. That means by default SQLite is reading every page through normal `read()` system calls. Memory mapping is off out of the box.

### 1.4 Playing with page size

I rebuilt the database with different page sizes (you have to set the PRAGMA *before* creating tables, and use `VACUUM` to actually rewrite the file):

```sql
PRAGMA page_size = 8192;
VACUUM;
```

| Page size | Page count | File size on disk |
|-----------|-----------|-------------------|
| 4 KB (default) | 9,216 | 36 MB |
| 8 KB           | 4,520 | 35.3 MB |
| 16 KB          | 2,245 | 35.1 MB |

The savings are tiny — bigger pages have a bit less header overhead per page, but nothing dramatic. The row data itself dominates.

### 1.5 mmap_size — does it actually help?

This is the part I was most curious about. I ran three queries with mmap off, then with mmap set to 256 MB.

```sql
-- Q1: full scan with string work
SELECT SUM(LENGTH(description)) FROM book_catalog;

-- Q2: aggregation
SELECT genre, COUNT(*), AVG(pages) FROM book_catalog GROUP BY genre;

-- Q3: indexed lookup
SELECT COUNT(*) FROM book_catalog WHERE genre = 'Fantasy';
```

I used SQLite's built-in `.timer ON` to measure. Numbers below are averages of 5 warm runs.

| Query | mmap = 0 | mmap = 256 MB | Speedup |
|-------|----------|---------------|---------|
| Q1 — Full scan | 142 ms | 96 ms | ~1.5× |
| Q2 — GROUP BY  | 610 ms | 215 ms | ~2.8× |
| Q3 — Index hit |  0.9 ms | 0.9 ms | basically same |

**What I learned:** mmap helps a lot when you're scanning the whole table because the OS page cache gets read directly through pointers instead of being copied into SQLite's buffer. For a small index lookup it doesn't matter — you're touching maybe 3 pages, the syscall overhead is nothing.

### 1.6 Watching the process

While running queries I checked the process using:

```bash
ps aux | grep sqlite
# krritinkeshan  62311   0.4  0.1  sqlite3 library.db
```

Single process, single thread. SQLite is embedded — there's no server, no background workers. Whatever you ask it does on the spot.

---

## Part 2 — PostgreSQL

### 2.1 Installing

```bash
brew install postgresql@16
brew services start postgresql@16
psql --version
# psql (PostgreSQL) 16.4
```

Then created the database and loaded the same 150k rows.

```bash
createdb library
psql library < schema.sql
```

### 2.2 Page size and counts in Postgres

Postgres handles this differently — page size is **fixed at 8 KB at compile time**, you can't change it from a PRAGMA-style command without rebuilding from source.

```sql
library=# SHOW block_size;
 block_size
------------
 8192

library=# SELECT relname, relpages, reltuples
          FROM pg_class
          WHERE relname LIKE '%book%';

      relname        | relpages | reltuples
---------------------+----------+-----------
 book_catalog        |     4630 |    150000
 book_catalog_pkey   |      415 |    150000
 idx_books_genre     |      135 |    150000
 idx_books_author    |      485 |    150000
```

So the table itself is `4630 × 8 KB ≈ 36 MB`, very close to the SQLite heap. But once you add the indexes, primary key, visibility map, free space map etc, the total cluster size sits around **~44 MB**.

```sql
SELECT pg_size_pretty(pg_database_size('library'));
-- 44 MB
```

### 2.3 Memory settings

Postgres doesn't use mmap the same way SQLite does. It has its own shared cache called `shared_buffers`:

| Setting | Value | What it does |
|---------|-------|--------------|
| `shared_buffers` | 128 MB | Main page cache shared across connections |
| `work_mem` | 4 MB | Memory per sort/hash operation |
| `max_parallel_workers_per_gather` | 2 | Allows queries to use background workers |

### 2.4 Query timing

Same three queries, run with `\timing on` in psql:

| Query | Postgres time |
|-------|---------------|
| Q1 — Full scan | 78 ms |
| Q2 — GROUP BY  | 52 ms |
| Q3 — Index hit | 0.7 ms |

Then I ran `EXPLAIN (ANALYZE, BUFFERS)` to see why:

```
Gather  (cost=1000.00..5421.50 rows=150000 width=8)
  Workers Planned: 2
  Workers Launched: 2
  ->  Parallel Seq Scan on book_catalog
      Buffers: shared hit=4630
```

Two things stood out:
1. **Parallel workers** — Postgres split the scan across 3 processes (main + 2 workers). SQLite can't do this.
2. **shared hit=4630** — every page was already in `shared_buffers`, so no disk I/O at all.

### 2.5 Process check

```bash
ps aux | grep postgres
# postgres  postmaster
# postgres  background writer
# postgres  walwriter
# postgres  autovacuum launcher
# postgres  logical replication launcher
```

Big difference here — Postgres runs as a server with multiple background processes always alive. SQLite was just one thread that came and went with my query.

---

## Part 3 — Side by Side

### 3.1 The big comparison table

| Feature | SQLite3 | PostgreSQL |
|---------|---------|------------|
| **Architecture** | Embedded (just a file) | Client–server with daemon |
| **Page size (default)** | 4 KB (configurable) | 8 KB (fixed at compile time) |
| **Pages used (150k rows)** | 9,216 | 4,630 (heap only) |
| **Storage on disk** | ~36 MB total | ~44 MB total (heap + indexes + maps) |
| **Memory mapping** | Off by default, set via `PRAGMA mmap_size` | Not used; uses `shared_buffers` instead |
| **Concurrency** | One writer at a time | MVCC, many readers + writers |
| **Parallel queries** | No | Yes |
| **Full scan time** | 142 ms (96 ms with mmap) | 78 ms |
| **GROUP BY time** | 610 ms (215 ms with mmap) | 52 ms |
| **Index lookup** | 0.9 ms | 0.7 ms |

### 3.2 So which one is "better"?

Honestly neither — they're solving different problems.

- **SQLite wins** when you want zero setup, a single file you can ship with an app, and your workload is mostly one user at a time. Phones, browsers, small tools — that's its world.
- **Postgres wins** when you have lots of users hitting the database at once, when queries are heavy and could use parallelism, and when you care about strict transactions across many connections.

### 3.3 Things that surprised me

1. **mmap is a free 2–3× speedup in SQLite** for scan-heavy queries. It's basically one PRAGMA command and the database gets noticeably faster. Wasn't expecting the gap to be that wide.
2. **Postgres uses more disk** for the same row count because it stores extra bookkeeping (visibility map, FSM) to support MVCC. That overhead is the price of multi-user safety.
3. **Indexed lookups are equally fast** on both — once you're hitting an index, the engine differences pretty much disappear. The page cache and B-tree do all the work.
4. **Page size changes are tiny** in terms of file size for this dataset. The improvements only really show up when your data is way bigger than RAM and you start caring about how many rows fit per page.

---

## Commands I used (quick reference)

**SQLite:**
```bash
sqlite3 library.db
.timer ON
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
PRAGMA mmap_size = 268435456;  -- 256 MB
VACUUM;
.quit
```

**Shell:**
```bash
ls -lh library.db
ps aux | grep sqlite
ps aux | grep postgres
```

**PostgreSQL:**
```sql
\timing on
SHOW block_size;
SELECT relname, relpages, reltuples FROM pg_class WHERE relname LIKE '%book%';
SELECT pg_size_pretty(pg_database_size('library'));
EXPLAIN (ANALYZE, BUFFERS) SELECT genre, COUNT(*) FROM book_catalog GROUP BY genre;
```

---

## Closing thoughts

Doing this lab made the page/cache/mmap stuff click for me in a way reading the docs never did. Seeing a query drop from 610 ms to 215 ms because of one PRAGMA flag is the kind of thing you remember. And on the Postgres side, watching `EXPLAIN ANALYZE` literally show you the parallel workers it spun up was cool — you can actually see *why* it's faster, not just that it is.

If I had to summarise it in one line: **SQLite is a really clever file format with a query engine on top, Postgres is a really clever process with a file format underneath**, and the trade-offs flow naturally from that.
