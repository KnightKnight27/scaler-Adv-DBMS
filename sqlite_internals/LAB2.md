# Lab 2 — SQLite3 internals: mmap, page size, PRAGMA & library architecture

**Goal:** inspect SQLite's storage internals via PRAGMA, understand why SQLite is
an in-process **library** (not a server), and set up the PostgreSQL-vs-SQLite
comparison (see [`PostgreSQL_vs_SQLite.md`](PostgreSQL_vs_SQLite.md)).

## Version

```
$ sqlite3 --version
3.51.0 2025-06-12 ...
```

## Storage internals — real PRAGMA output

Run against the repo's `students.db`:

```bash
sqlite3 ../students.db ".read pragmas.sql"
```

| PRAGMA            | Value on `students.db` | Meaning |
|-------------------|------------------------|---------|
| `page_size`       | `4096`                 | bytes per page; fixed at creation (matches the OS page size) |
| `page_count`      | `4`                    | pages allocated in the file |
| `mmap_size`       | `0`                    | memory-mapped I/O is **off** by default |
| `journal_mode`    | `delete`               | rollback journal (the classic default; not WAL) |
| `cache_size`      | `2000`                 | pages held in the in-memory page cache |
| `integrity_check` | `ok`                   | every page validated |

**File size check:** `page_size × page_count = 4096 × 4 = 16384 bytes`, which is
exactly what the filesystem reports for `students.db` (16384 B). The whole
database is one file divided into fixed-size pages.

## A telling detail in the schema

```sql
CREATE TABLE students (
    student_id SERIAL PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    ...
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

This DDL is **PostgreSQL-flavored** (`SERIAL`, `VARCHAR(n)`, `TIMESTAMP`). SQLite
accepts it because of its dynamic, type-affinity system — `SERIAL` is parsed as a
column-type token (it is *not* an auto-increment sequence as it would be in
PostgreSQL), and `VARCHAR(100)` carries TEXT affinity with the length ignored.
The same script means different things to the two engines — exactly the kind of
portability gap the comparison doc explores.

## mmap: turning `read()` into memory access

By default (`mmap_size = 0`) SQLite reads pages with `read()` syscalls. Enabling
mmap maps the database file into the process address space, so reads become plain
memory loads served from the OS page cache:

```sql
PRAGMA mmap_size = 268435456;  -- 256 MB
PRAGMA mmap_size;              -- confirm
```

Verify the effect with tracing:

```bash
# Linux
strace -e trace=mmap,openat,read sqlite3 students.db "SELECT count(*) FROM students;"
#   mmap_size=0  -> many read() calls
#   mmap_size>0  -> one mmap(), then direct memory access (fewer/no read())
```

## SQLite is a library, not a process

```
your application binary
  └── links libsqlite3  (or statically embeds it)
        └── reads/writes the .db file directly via OS syscalls
```

- No server process, no TCP socket, no auth handshake — `ps aux | grep sqlite`
  shows only your own process.
- The engine runs **in your process's address space**: `sqlite3_open()`,
  `sqlite3_exec()`, `sqlite3_close()` are ordinary in-process function calls.
- Concurrency is handled with file locks (WAL mode relaxes this to one writer +
  many readers).

## Takeaways

- A SQLite database is a single file of fixed-size pages; `page_size × page_count` = file size.
- PRAGMA is the introspection surface for page size, journal mode, cache size, and mmap.
- `mmap_size > 0` swaps `read()` syscalls for direct memory access via the page cache.
- Being an in-process library (vs PostgreSQL's client-server daemon) is SQLite's defining architectural choice — detailed in [`PostgreSQL_vs_SQLite.md`](PostgreSQL_vs_SQLite.md).
