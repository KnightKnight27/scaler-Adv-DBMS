# Lab 2: SQLite3 Internals and SQLite3 vs PostgreSQL

**Name:** Aditya Bhaskara  **Roll No:** 24BCS10058  **Lab Session:** 2

## Objective

Inspect how SQLite3 lays out a database on disk, drive that introspection through
PRAGMA commands, understand why SQLite runs as an in-process library rather than a
server, and use those findings to compare SQLite3 against PostgreSQL.

All of the output below was captured on macOS (Apple clang, SQLite 3.51.0). Where
the behaviour differs from a typical Linux box the difference is called out.

---

## Part 1: Setup

On Linux the install is `sudo apt install sqlite3 libsqlite3-dev`. macOS ships
SQLite already, so the only step was to confirm the version:

```
$ sqlite3 --version
3.51.0 2025-06-12 13:14:41 ...
```

A 1000 row table was created to give the file something to store:

```sql
CREATE TABLE students (
    id         INTEGER PRIMARY KEY,
    name       TEXT,
    age        INTEGER,
    department TEXT
);

WITH RECURSIVE seq(n) AS (
    SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < 1000
)
INSERT INTO students(id, name, age, department)
SELECT n, 'student_' || n, 18 + (n % 10),
       CASE n % 4 WHEN 0 THEN 'CS' WHEN 1 THEN 'Math'
                  WHEN 2 THEN 'Physics' ELSE 'Bio' END
FROM seq;
```

---

## Part 2: Storage internals via PRAGMA

### The whole database is one file of fixed-size pages

```
page_size        = 4096
page_count       = 9
freelist_count   = 0
cache_size       = 2000
mmap_size        = 0
journal_mode     = delete
```

`page_size` is the unit SQLite reads and writes, and it matches the 4096 byte OS
page. The most useful check is that the file is exactly the pages it claims to
hold, nothing more:

```
page_size * page_count = 4096 * 9 = 36864 bytes
actual file size                  = 36864 bytes
```

So a SQLite database really is just `page_count` pages of `page_size` bytes laid
end to end in a single file. The first page also holds the 100 byte file header
and the schema.

### File size grows one page at a time, not one row at a time

Building the same table at different row counts shows the file growing in page
sized steps rather than smoothly:

| Rows | page_count | File size |
|------|------------|-----------|
| 0    | 2          | 8192 bytes  |
| 10   | 2          | 8192 bytes  |
| 100  | 2          | 8192 bytes  |
| 1000 | 8          | 32768 bytes |

Up to 100 rows everything fits in the two pages SQLite already allocated (one for
the schema, one for the leaf), so the file does not grow at all. Past that it
adds whole pages at a time. SQLite never grows the file by a few bytes; it grows
it by a page.

(The 1000 row `students.db` committed here reports 9 pages rather than 8 because
its rows carry a longer `department` string than the size-test table, which is a
nice reminder that row width drives page count.)

### Page size is fixed at creation

`PRAGMA page_size` can only be changed before any table exists, or later through
`VACUUM INTO`. Once data is written the page size is baked into the file header.

### mmap turns reads into memory accesses

`mmap_size` is 0 by default, so SQLite reads pages with ordinary `read()` calls.
Setting it asks SQLite to memory map the database file instead:

```
$ sqlite3 students.db "PRAGMA mmap_size=268435456; PRAGMA mmap_size;"
268435456
268435456
```

With mmap on, SQLite calls `mmap()` once and the OS maps file pages straight into
the process address space. Reading a page becomes a memory access against the
shared page cache rather than a `read()` syscall, which removes the user/kernel
copy on the read path. On Linux you can watch this with
`strace -e trace=mmap,read sqlite3 students.db "SELECT count(*) FROM students;"`:
mmap_size=0 produces many `read()` calls, mmap_size>0 produces a single `mmap()`
and then far fewer reads.

### Journal mode

Default journal mode here is `delete` (a rollback journal written beside the db
and removed on commit). Switching to write ahead logging is a one liner:

```
$ sqlite3 students.db "PRAGMA journal_mode=WAL;"
wal
```

WAL lets readers keep reading the existing pages while a writer appends changes to
a separate `-wal` file, which is the main lever SQLite has for read/write
concurrency.

### Integrity check

```
$ sqlite3 students.db "PRAGMA integrity_check;"
ok
```

This walks every page and confirms the b-tree structure is consistent.

---

## Part 3: SQLite is a library, not a server

This is the architectural difference that matters most.

### There is no database process

```
$ pgrep -l sqlited
$              # prints nothing: there is no such process
```

`pgrep` finds no match because SQLite has no server process. Nothing listens on a
socket and no daemon runs; the engine executes inside whatever process opened the
database.

### How the engine is linked

On this machine the `sqlite3` command links only general purpose libraries, with
no separate SQLite library in the list:

```
$ otool -L /usr/bin/sqlite3
/usr/bin/sqlite3:
        /usr/lib/libz.1.dylib
        /usr/lib/libncurses.5.4.dylib
        /usr/lib/libedit.3.dylib
        /usr/lib/libSystem.B.dylib
```

Since there is no `libsqlite3` entry, this build has the engine compiled directly
into the binary. Other builds link it dynamically instead: on a typical Linux box
`ldd $(which sqlite3)` shows `libsqlite3.so.0`, the same engine loaded as a shared
library at startup. The linking choice varies by build, but the architecture does
not. Either way the engine runs inside your process and talks to the `.db` file
directly through OS syscalls, with no separate server and no network hop.

From C++ this is just function calls into the linked library:

```cpp
#include <sqlite3.h>
// sqlite3_open(), sqlite3_exec(), sqlite3_close() are ordinary in-process calls
```

---

## Part 4: SQLite3 vs PostgreSQL

### Architecture

| Dimension       | SQLite3                                    | PostgreSQL                                      |
|-----------------|--------------------------------------------|-------------------------------------------------|
| Process model   | Library running inside your process        | Client/server, a separate `postgres` daemon     |
| Communication   | Direct function calls and file I/O         | TCP socket (port 5432) or a Unix socket          |
| Concurrency     | File locks, one writer at a time, WAL helps| MVCC, many readers and writers at once           |
| Authentication  | None, filesystem permissions only          | Users, roles, passwords, SSL                     |
| Storage         | A single `.db` file                        | A data directory of many files plus WAL          |
| Buffer cache    | OS page cache, optionally via mmap         | Its own `shared_buffers` pool inside the daemon  |

### When SQLite3 is the right choice

- Embedded and on-device storage (mobile apps, desktop apps, browsers, CLI tools).
- Local development and test databases.
- Single writer or low concurrency workloads.
- Anywhere you want zero infrastructure and no server to operate.

### When PostgreSQL is the right choice

- Many clients writing at the same time (web backends, APIs).
- Workloads that need row level locking and stronger isolation levels.
- Rich features: extensions, full text search, JSON operators, replication.
- Production systems that need authentication, roles, SSL and auditing.

### Where mmap fits

SQLite can map the `.db` file into its address space so the process and the OS
page cache share the same physical pages, which speeds up the read path. The
PRAGMA experiment above shows exactly that switch being thrown. PostgreSQL takes
the opposite approach: the daemon manages its own `shared_buffers` pool and does
not rely on mmap for its main I/O path.

### Key insight

The single file, in-process design is what makes SQLite unbeatable for
portability and simplicity, and it is the same design that limits it to one
writer at a time. PostgreSQL pays for a server process and MVCC machinery, and in
return it serves many concurrent writers safely. The decision comes down to one
question: how many writers touch the database at the same time, and from how many
processes.

---

## Key takeaways

- A SQLite database is literally `page_count` pages of `page_size` bytes in one
  file; `page_size * page_count` equals the file size on disk.
- The file grows a page at a time, so small inserts often do not change its size.
- `mmap_size` flips the read path from `read()` syscalls to direct memory access
  against the shared page cache.
- SQLite has no server process; its engine is linked into the application, while
  PostgreSQL is a separate daemon you connect to over a socket.
