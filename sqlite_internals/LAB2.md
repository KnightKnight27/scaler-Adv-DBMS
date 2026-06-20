# Lab 2 - SQLite internals

## Goal

Use SQLite's own `PRAGMA` interface to inspect page layout, cache settings,
journal mode, and memory-mapped I/O. Then connect those observations to the
architectural difference between SQLite and PostgreSQL.

## Run

```bash
sqlite3 students.db ".read sqlite_internals/pragmas.sql"
```

Useful values for the included `students.db`:

| Item | Observation |
|------|-------------|
| page size | SQLite stores the database in fixed-size pages, commonly 4096 bytes |
| page count | total pages allocated in the single database file |
| file size | `page_size * page_count` should match the filesystem size |
| mmap size | `0` means normal read calls; a positive value permits memory mapping |
| journal mode | shows the rollback/WAL strategy used for atomic commits |
| integrity check | returns `ok` when all pages validate |

## mmap

With `PRAGMA mmap_size = 268435456`, SQLite may map database pages into the
process address space. Reads then behave like memory loads backed by the OS page
cache instead of repeated `read()` syscalls.

On Linux, compare syscall behavior with:

```bash
strace -e trace=mmap,openat,read sqlite3 students.db "SELECT count(*) FROM students;"
```

## Why SQLite is not a server

SQLite is a library linked into the application process. The application calls
functions such as `sqlite3_open` and `sqlite3_exec`; the library reads and writes
the `.db` file directly. There is no daemon, socket protocol, or database login.

PostgreSQL is the opposite shape: client processes connect to a long-running
server, and that server owns shared buffers, WAL, authentication, process
coordination, and MVCC.
