# Lab 2 — SQLite3 Internals: mmap, Page Size, PRAGMA & Library Architecture

**Rohan Ranjan — 24BCS10428**

## Objective
Install SQLite3, inspect its storage internals via `PRAGMA` commands, understand why
SQLite is an in-process library (not a server), and document System Design Assignment 1
(PostgreSQL vs SQLite3).

## Files
| File              | Purpose                                                                 |
|-------------------|-------------------------------------------------------------------------|
| `pragmas.sql`     | PRAGMA introspection script (`sqlite3 students.db < pragmas.sql`)        |
| `sqlite_demo.cpp` | C++ program linking `libsqlite3` — proves SQLite is in-process          |
| `README.md`       | Findings + System Design Assignment 1 (PostgreSQL vs SQLite3)           |

## Part 1 — Installation & verification
```bash
sudo apt install sqlite3 libsqlite3-dev   # Ubuntu/Debian
sqlite3 --version                          # e.g. 3.45.1 2024-01-30 ...
```

## Part 2 — Storage internals via PRAGMA
```bash
sqlite3 students.db < pragmas.sql
```
- **`PRAGMA page_size;`** — default `4096` bytes (matches the OS page size). Fixed at
  creation; only changeable via `VACUUM INTO`. SQLite stores the whole DB as one file
  divided into fixed-size pages.
- **`PRAGMA page_count;`** — pages allocated. `file size = page_size * page_count`.
- **`PRAGMA mmap_size;`** — `0` by default. Set it to enable memory-mapped I/O:
  ```sql
  PRAGMA mmap_size = 268435456;  -- 256 MB
  ```
  With mmap on, SQLite calls `mmap()` on the DB file; the OS maps file pages directly
  into the process address space, so reads become memory accesses rather than `read()`
  syscalls.

Confirm the syscall difference with strace:
```bash
strace -e trace=mmap,open,read sqlite3 students.db "SELECT count(*) FROM students;"
# mmap_size=0:  many read() calls
# mmap_size>0:  one mmap() call, then direct memory access (fewer/no read())
```

Other useful PRAGMAs: `journal_mode`, `cache_size`, `integrity_check`, `database_list`.

## Part 3 — SQLite3 is a library, not a process
```
Your application binary
  └── links libsqlite3.so  (or statically embeds it)
        └── reads/writes the .db file directly via OS syscalls
```
- No separate server process, no TCP socket, no auth handshake.
- The library runs **in the same process and address space** as your application.
- Concurrency is handled by file-level locks (WAL mode helps significantly).

```bash
ps aux | grep sqlite        # nothing — only your own process
ldd ./sqlite_demo           # libsqlite3.so.0 => /lib/.../libsqlite3.so.0
```
`sqlite_demo.cpp` shows this directly: `sqlite3_open()`, `sqlite3_exec()`,
`sqlite3_close()` are all ordinary in-process function calls.
```bash
g++ -std=c++17 sqlite_demo.cpp -lsqlite3 -o sqlite_demo && ./sqlite_demo
```

---

## System Design Assignment 1 — PostgreSQL vs SQLite3

### Architecture
| Dimension       | SQLite3                                      | PostgreSQL                                    |
|-----------------|----------------------------------------------|-----------------------------------------------|
| Process model   | Library — runs inside your process           | Client-server — separate `postgres` daemon    |
| Communication   | Direct function calls / file I/O             | TCP socket (port 5432) or Unix socket         |
| Concurrency     | File locks; one writer at a time (WAL helps) | MVCC — many readers + writers simultaneously  |
| Authentication  | None (filesystem permissions only)           | Full user/role/password/SSL system            |
| Storage         | Single `.db` file                            | Data directory with many files + WAL          |
| Transactions    | ACID (serialized writes)                     | Full ACID with MVCC isolation levels          |

### When to use SQLite3
- Embedded apps (mobile, desktop, CLI tools); test / local-dev databases.
- Single-user or low-concurrency workloads; read-heavy with occasional writes.
- When you want zero infrastructure (no server to manage).

### When to use PostgreSQL
- Multi-user applications with concurrent writes; web backends / APIs.
- Row-level locking, advanced isolation (REPEATABLE READ, SERIALIZABLE).
- Complex queries, full-text search, JSON operators, extensions (PostGIS, …).
- Production systems needing authentication, roles, SSL, and auditing.

### How mmap fits in
- SQLite can `mmap()` the `.db` file into the process address space — fast sequential
  reads because the OS page cache and the process share the same physical pages.
- PostgreSQL has its own shared buffer pool (`shared_buffers`) managed by the server; it
  does not rely on mmap for its primary I/O path (some WAL reads aside).

### Key insight
SQLite's single-file, in-process design is nearly unbeatable for portability and
simplicity. PostgreSQL's client-server, MVCC design is unbeatable for concurrent
multi-user workloads. The right choice depends entirely on **who** writes to the database
and **how many** of them do so at the same time.
