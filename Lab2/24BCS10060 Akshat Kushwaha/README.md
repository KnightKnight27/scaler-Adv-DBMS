# Lab 2 — SQLite vs PostgreSQL (Storage Internals)

**Name:** Akshat Kushwaha
**Roll No:** 24BCS10060

## What this lab is about

Both SQLite and PostgreSQL are SQL databases, but they are built very
differently. In this lab I poked at SQLite's internals using `PRAGMA` commands,
checked that SQLite really is just a library (no server process), and then
compared it with PostgreSQL's client-server design. This write-up is also the
first System Design assignment (PostgreSQL vs SQLite).

---

## Part 1 — Looking inside SQLite with PRAGMA

I made a small database and asked SQLite about its own storage.

```bash
sqlite3 lab2.db
```

```sql
-- make a table and add some rows
CREATE TABLE products (
    id    INTEGER PRIMARY KEY,
    name  TEXT,
    price INTEGER
);
-- SQLite has no generate_series, so a recursive CTE makes the rows
WITH RECURSIVE seq(n) AS (
    SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < 5000
)
INSERT INTO products(name, price)
SELECT 'item_' || n, n * 10 FROM seq;   -- 5000 rows
```

Then the introspection commands:

```sql
PRAGMA page_size;     -- 4096   (bytes per page)
PRAGMA page_count;    -- 27     (pages currently used for 5000 rows)
PRAGMA mmap_size;     -- 0      (memory-mapped I/O off by default)
PRAGMA journal_mode;  -- delete (or wal if turned on)
PRAGMA cache_size;    -- 2000   (pages SQLite keeps cached)
```

What I learned from these:

| PRAGMA | Value | Meaning |
|---|---|---|
| `page_size` | 4096 | SQLite stores the whole DB as fixed 4 KB pages. This is set at creation and matches the usual OS page size. |
| `page_count` | 27 | How many pages are in use. `file size = page_size × page_count`. |
| `mmap_size` | 0 | Memory-mapped reads are off by default; turning it on can speed up reads. |
| `journal_mode` | delete | How SQLite keeps transactions safe (rollback journal, or WAL). |

**mmap experiment.** With `PRAGMA mmap_size = 268435456;` (256 MB) SQLite maps
the database file straight into the process's memory, so reading a page becomes a
normal memory access instead of a `read()` system call + copy. For a full scan of
my small table the difference was tiny, but the idea is that mmap removes the
extra kernel→user copy on the read path.

---

## Part 2 — SQLite is a library, not a server

This is the biggest architectural difference. SQLite does **not** run as a
separate program. It is a library that gets linked *into my own program*, and it
reads and writes the `.db` file directly.

I checked there is no server process:

```bash
ps aux | grep sqlite     # nothing running in the background
```

Compare that with PostgreSQL, where I'd see several `postgres` processes always
running even when nobody is querying.

```
SQLite:                          PostgreSQL:
+---------------------+          client ---\
|  my application     |          client ----> [ postgres server processes ]
|   + sqlite library  |          client ---/        |
|        |            |                             v
|        v            |                       data files on disk
|   lab2.db file      |
+---------------------+
```

So with SQLite the "database" is just code inside my app plus one file. With
PostgreSQL the database is a set of long-running server processes that clients
connect to over a socket.

---

## Part 3 — Comparison (System Design Assignment 1)

| Aspect | SQLite | PostgreSQL |
|---|---|---|
| Process model | library inside your program | separate client-server daemon |
| How you talk to it | direct function calls / file I/O | TCP or Unix socket (port 5432) |
| Page size | 4096 bytes | 8192 bytes |
| Storage | one `.db` file | a data directory with many files + WAL |
| Concurrency | one writer at a time (WAL helps readers) | MVCC — many readers and writers at once |
| Users / auth | none (just file permissions) | full users, roles, passwords, SSL |
| Setup | nothing to install/run | install + run + manage a server |

### When SQLite is the right choice
- Mobile and desktop apps (it's embedded, one file, low memory).
- Local/dev databases and small tools.
- Single-user or low-write workloads.
- When you want zero infrastructure.

### When PostgreSQL is the right choice
- Web backends and APIs with many users at the same time.
- Workloads with lots of concurrent writes.
- When you need advanced SQL, real isolation levels, extensions, auth.

### The key insight
The choice basically comes down to **who writes to the database and how many of
them at once**. One local user → SQLite's simplicity wins. Many concurrent users
→ PostgreSQL's client-server + MVCC design wins. Neither is "better"; they were
built for different situations.

---

## Key takeaways

- SQLite stores everything in fixed-size pages (4 KB) inside a single file, and
  `PRAGMA` lets you inspect that directly.
- `mmap` can speed up reads by mapping the file into memory and skipping a copy.
- SQLite runs *inside* your process (a library); PostgreSQL runs as *separate
  server processes* you connect to.
- PostgreSQL's MVCC is what lets many users read and write at the same time,
  which is the main reason it scales to multi-user systems where SQLite doesn't.
