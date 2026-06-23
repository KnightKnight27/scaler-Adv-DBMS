# Lab Session 2: Exploring SQLite3 Internals and Architecture

## Objective
The main goal of this lab is to install SQLite3 and take a deep dive into its storage internals using PRAGMA commands. We'll also examine why SQLite functions as an in-process library rather than a traditional server, and compile our findings for the first System Design assignment comparing PostgreSQL and SQLite3.

---

## Part 1: Installation and Setup

First, let's get SQLite3 installed.

```bash
# For Ubuntu or Debian-based systems
sudo apt install sqlite3 libsqlite3-dev

# Quick check to ensure it was installed correctly
sqlite3 --version
# You should see something like: 3.45.1 2024-01-30 ...
```

---

## Part 2: Inspecting Storage Internals with PRAGMA

Let's open up a database (or create a new one) and run some PRAGMA introspection commands to see what's happening under the hood.

```bash
sqlite3 students.db
```

### Checking Page Size
```sql
PRAGMA page_size;
-- Usually defaults to 4096 bytes to match the OS page size
```

SQLite stores its entire database as a single file divided into fixed-size pages. It's worth noting that the page size is determined when the database is created and can't be easily changed later unless you use a VACUUM INTO command.

### Getting the Page Count
```sql
PRAGMA page_count;
-- This shows the total number of pages currently allocated in the file
```

You can calculate the total file size simply by multiplying `page_size` by `page_count`.

### Memory Mapping (mmap) Size
```sql
PRAGMA mmap_size;
-- The default is usually 0. We can increase this to enable memory-mapped I/O.
```

Let's enable memory mapping. This speeds up read operations by bypassing traditional `read()` system calls for sequential access.
```sql
PRAGMA mmap_size = 268435456;  -- Sets it to 256 MB
PRAGMA mmap_size;              -- Just to confirm it changed
```

When mmap is enabled, SQLite uses the `mmap()` function on the database file. This allows the OS to map file pages directly into the process's address space. As a result, reading data becomes a direct memory access instead of requiring a `read()` system call.

We can actually see this in action using strace:
```bash
strace -e trace=mmap,open,read sqlite3 students.db "SELECT count(*) FROM students;"
# When mmap_size is 0: you'll see a lot of read() calls.
# When mmap_size > 0: you'll see an mmap() call followed by fewer or no read() calls.
```

### A Few Other Handy PRAGMAs
```sql
PRAGMA journal_mode;       -- Shows the current mode like WAL, DELETE, or MEMORY.
PRAGMA cache_size;         -- The number of pages kept in memory cache.
PRAGMA integrity_check;    -- Validates all pages to ensure database health.
PRAGMA database_list;      -- Lists all attached databases.
```

---

## Part 3: Why SQLite3 is a Library and Not a Process

This is probably the biggest architectural difference between SQLite and PostgreSQL.

### How SQLite Integrates
```
Your application binary
  └── links to libsqlite3.so (or embeds it statically)
        └── reads from and writes to the .db file directly using OS syscalls
```

Unlike PostgreSQL, there is no separate server process running in the background. There are no TCP sockets to connect to and no authentication handshakes. The SQLite library runs entirely within the same process and address space as your own application. It manages concurrency using simple file-level locks (though using WAL mode does improve concurrent access quite a bit).

We can verify that there's no background server running:
```bash
ps aux | grep sqlite
# You won't find a standalone server, just your own process using it.
```

And we can verify our program links to the dynamic library:
```bash
ldd $(which sqlite3)
# It should list something like: libsqlite3.so.0 => /lib/x86_64-linux-gnu/libsqlite3.so.0
```

When writing C++ code, we just call SQLite functions directly in our process:
```cpp
#include <sqlite3.h>
// Functions like sqlite3_open(), sqlite3_exec(), and sqlite3_close() are all in-process calls.
```

---

## System Design Assignment 1: PostgreSQL versus SQLite3

### Architecture Comparison

| Feature              | SQLite3                                      | PostgreSQL                                      |
|----------------------|----------------------------------------------|-------------------------------------------------|
| Process Model        | In-process library                           | Client-server model with a dedicated daemon     |
| Communication        | Direct function calls and file I/O           | TCP socket (usually port 5432) or Unix socket   |
| Concurrency Handling | File locks (WAL mode improves this)          | MVCC, supporting many concurrent read/writes    |
| Authentication       | None (relies on filesystem permissions)      | Comprehensive user, role, and SSL management    |
| Storage Approach     | A single `.db` file                          | A structured data directory with WAL files      |
| Transactions         | ACID compliant (writes are serialized)       | Fully ACID compliant with MVCC isolation levels |

### When SQLite3 Makes Sense
- For embedded applications like mobile, desktop, or CLI tools.
- When you need a quick local testing or development environment.
- In single-user scenarios or applications with very low concurrency.
- When you prefer a zero-configuration setup with no server to maintain.
- For workloads that are very read-heavy with only occasional writes.

### When PostgreSQL is the Better Choice
- Multi-user environments expecting significant concurrent write operations.
- Web backends and APIs dealing with many simultaneous client requests.
- When you need advanced database features like row-level locking or strict isolation levels (e.g., REPEATABLE READ, SERIALIZABLE).
- If your application heavily relies on complex queries, full-text search, JSON operators, or extensions like PostGIS.
- In production systems where robust authentication, role-based access control, SSL, and auditing are mandatory.

### The Role of Memory Mapping (mmap)
- SQLite can leverage `mmap()` to map the database file into its own process address space. This leads to significantly faster sequential reads because the OS page cache and the application process share the exact same physical memory pages.
- PostgreSQL, on the other hand, manages its own internal shared buffer pool (`shared_buffers`). It generally avoids relying on mmap for its main I/O path, although it might use it for reading some WAL files.

### Final Thoughts
SQLite's approach of using a single file and running entirely in-process makes it exceptionally portable and simple to use. PostgreSQL’s client-server architecture and MVCC implementation make it a powerhouse for handling complex, concurrent multi-user environments. Deciding between the two really comes down to the application's concurrency needs and whether a separate database server is feasible or necessary.
