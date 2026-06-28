# Lab Session 2: SQLite3 Internals — mmap, Page Size, PRAGMA & Library Architecture

## Objective
Install SQLite3, inspect its storage internals via PRAGMA commands, understand why SQLite is an in-process library (not a server), and document findings as part of System Design Assignment 1 (PostgreSQL vs SQLite3).

---

## Part 1: Installation & Verification

On Linux/Ubuntu/Debian, install via:
```bash
# Ubuntu/Debian
sudo apt install sqlite3 libsqlite3-dev
```

On this system, verification shows:
```bash
sqlite3 --version
# 3.45.3 2024-04-15 13:34:05 8653b758870e6ef0c98d46b3ace27849054af85da891eb121e9aaa537f1e8355 (64-bit)
```

---

## Part 2: Storage Internals via PRAGMA

We inspected the local database `students.db` located in the workspace root. Below are the actual values queried:

### Page Size
```sql
PRAGMA page_size;
-- Result: 4096 (bytes)
```
SQLite stores the entire database as a single file divided into fixed-size pages. The page size is set at database creation and matches the standard OS page size (4096 bytes).

### Page Count
```sql
PRAGMA page_count;
-- Result: 4
```
Total file size = `page_size * page_count` = `4096 * 4` = `16384` bytes, which matches the actual size of `students.db` on disk (16KB).

### mmap Size
```sql
PRAGMA mmap_size;
-- Result: 0 (default, mmap disabled)
```

We can enable memory-mapped I/O to map pages directly into the process address space, bypassing `read()` syscalls for sequential reads:
```sql
PRAGMA mmap_size = 268435456;  -- Enable with 256 MB limit
PRAGMA mmap_size;              -- Confirm change
```

With mmap enabled, SQLite calls `mmap()` on the database file. The OS maps file pages directly into the process address space — reads become memory accesses instead of `read()` syscalls.

Verify via strace:
```bash
strace -e trace=mmap,openat,read sqlite3 students.db "SELECT count(*) FROM students;"
# With mmap_size=0:  many read() calls
# With mmap_size>0:  mmap() call, then direct memory access — fewer/no read() calls
```

### Other PRAGMAs Observed on `students.db`
- **journal_mode**: `delete` (default rollback journal deleted after commit)
- **cache_size**: `-2000` (defaults to 2000 pages or 2000 KiB depending on sign)
- **integrity_check**: `ok` (verifies tree structure and database consistency)
- **Schema of `students` Table**:
  ```sql
  CREATE TABLE students (
      student_id SERIAL PRIMARY KEY,
      first_name VARCHAR(100) NOT NULL,
      last_name VARCHAR(100) NOT NULL,
      age INT,
      email VARCHAR(255) UNIQUE,
      course VARCHAR(100),
      created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
  );
  ```

---

## Part 3: SQLite3 is a Library, Not a Process

This is the most architecturally significant difference from PostgreSQL.

### How SQLite works
```
Your application binary
  └── links libsqlite3.so  (or statically embeds it)
        └── reads/writes the .db file directly via OS syscalls
```

- No separate server process. No TCP socket. No authentication handshake.
- The library runs **in the same process and address space** as your application.
- Concurrency is handled by file-level locks (WAL mode improves this significantly).

Verify there is no sqlite server process:
```bash
ps aux | grep sqlite
# Nothing — only your own process appears
```

Check that your program is dynamically linked to the library (on Linux):
```bash
ldd $(which sqlite3)
# shows: libsqlite3.so.0 => /lib/x86_64-linux-gnu/libsqlite3.so.0
```

From C++, SQLite is called directly:
```cpp
#include <sqlite3.h>
// sqlite3_open(), sqlite3_exec(), sqlite3_close() — all in-process function calls
```

---

## System Design Assignment 1: PostgreSQL vs SQLite3

### Architecture

| Dimension            | SQLite3                                      | PostgreSQL                                      |
|----------------------|----------------------------------------------|-------------------------------------------------|
| Process model        | Library — runs inside your process           | Client-server — separate `postgres` daemon      |
| Communication        | Direct function calls / file I/O             | TCP socket (default port 5432) or Unix socket   |
| Concurrency          | File locks; one writer at a time (WAL helps) | MVCC — many readers + writers simultaneously    |
| Authentication       | None (filesystem permissions only)           | Full user/role/password/SSL system              |
| Storage              | Single `.db` file                            | Data directory with many files + WAL            |
| Transactions         | ACID (serialized writes)                     | Full ACID with MVCC isolation levels            |

### When to use SQLite3
- Embedded applications (mobile apps, desktop apps, CLI tools)
- Test databases / local dev environments
- Single-user or low-concurrency workloads
- When you want zero infrastructure (no server to manage)
- Read-heavy workloads with occasional writes

### When to use PostgreSQL
- Multi-user applications with concurrent writes
- Web backends, APIs serving many simultaneous clients
- Need for row-level locking, advanced isolation (REPEATABLE READ, SERIALIZABLE)
- Complex queries, full-text search, JSON operators, extensions (PostGIS, etc.)
- Production systems requiring authentication, roles, SSL, and auditing

### How mmap fits in
- SQLite can use `mmap()` to map the .db file into the process address space — faster sequential reads since the OS page cache and the process share the same physical memory pages.
- PostgreSQL has its own shared buffer pool (`shared_buffers`) managed by the server process — it does not rely on mmap for its primary I/O path (though some WAL reads use it).

### Key insight
SQLite's single-file, in-process design makes it nearly unbeatable for portability and simplicity. PostgreSQL's client-server, MVCC design makes it unbeatable for concurrent multi-user workloads. The right choice depends entirely on who is writing to the database and how many of them are doing so at the same time.
