# Advanced DBMS - Lab 1: SQLite3 vs PostgreSQL

**Name:** Sudharsan  
**Roll No:** 23bcs10077

---

## SQLite3

Installed via Homebrew (`brew install sqlite3`), used the [Chinook sample database](https://www.sqlitetutorial.net/sqlite-sample-database/) which has a bunch of tables like `customers`, `invoices`, `tracks`, etc.

### File size

```bash
$ ls -lh Chinook_Sqlite.sqlite
-rw-r--r--  1 user  staff  1.1M May 10 14:22 Chinook_Sqlite.sqlite
```

### Page size & count

```sql
PRAGMA page_size;
-- 4096

PRAGMA page_count;
-- 272
```

So total storage = 4096 * 272 = ~1.08 MB, which lines up with the file size from `ls`.

### mmap

By default mmap_size is 0 (disabled).

```sql
PRAGMA mmap_size;
-- 0

PRAGMA mmap_size = 268435456;  -- setting it to 256MB
-- 268435456
```

With mmap enabled, SQLite maps the database file directly into memory instead of going through read() syscalls. For a small db like Chinook, the difference is barely noticeable, but here's what I got:

```bash
# Without mmap
$ time sqlite3 Chinook_Sqlite.sqlite "SELECT * FROM tracks;"
real    0m0.018s

# With mmap enabled
$ time sqlite3 Chinook_Sqlite.sqlite "PRAGMA mmap_size=268435456; SELECT * FROM tracks;"
real    0m0.014s
```

Not a huge gap honestly, the db is tiny. Would matter more on larger files.

### Process info

```bash
$ ps aux | grep sqlite
user  12847  0.0  0.0  sqlite3 Chinook_Sqlite.sqlite
```

Nothing fancy, just one lightweight process. SQLite runs in-process so there's no background server or anything.

---

## PostgreSQL

Installed with `brew install postgresql@16` and started it with `brew services start postgresql@16`. Created a similar setup by importing the Chinook database into Postgres.

```bash
$ psql -d chinook
```

### Page size & count

Postgres doesn't have a simple PRAGMA like SQLite. You query it differently:

```sql
SHOW block_size;
-- 8192

SELECT pg_relation_size('tracks') / 8192 AS page_count;
-- 38
```

Default block size in Postgres is 8192 (8 KB), which is 2x SQLite's default 4096.

### Query timing

Postgres has `\timing` built in:

```sql
\timing on

SELECT * FROM tracks;
-- Time: 4.832 ms
```

Slower than SQLite for this kind of simple read on a small db. Makes sense since Postgres has the overhead of client-server communication, query planning, etc.

### mmap

Postgres doesn't expose mmap the same way SQLite does. It uses its own buffer pool (`shared_buffers`) to cache pages in memory. You can tune it in `postgresql.conf`:

```sql
SHOW shared_buffers;
-- 128MB
```

The OS may also use mmap under the hood (Postgres uses `mmap` for some operations internally), but it's not a user-facing toggle like in SQLite. So "mmap" as a concept applies differently here.

---

## Comparison

| | SQLite3 | PostgreSQL |
|---|---|---|
| **Page Size** | 4096 bytes (4 KB) | 8192 bytes (8 KB) |
| **Page Count** (tracks table context) | 272 (whole db) | 38 (just the tracks table) |
| **Simple SELECT time** | ~0.018s | ~4.8ms |
| **mmap** | User-configurable via PRAGMA, maps db file into memory directly | Not directly configurable; uses shared_buffers and OS page cache instead |
| **Architecture** | Embedded, serverless, single file | Client-server, multi-process |

### Takeaways

- SQLite is faster for simple reads on small databases because there's zero network/IPC overhead. It's literally just reading a file.
- PostgreSQL's page size being larger (8 KB vs 4 KB) means fewer I/O operations for sequential reads on bigger datasets, but for a toy database it doesn't matter.
- mmap in SQLite gave a marginal speedup. On a database that fits in the OS page cache anyway, both paths end up reading from RAM.
- Postgres is designed for concurrency, ACID with multiple writers, replication, etc. Comparing raw speed on a single-user read is not really a fair fight -- they're built for different things.
