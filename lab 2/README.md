# Lab Session 2: SQLite3 Internals — mmap, Page Size, PRAGMA & Library Architecture

## Objective
Install SQLite3, inspect its storage internals via PRAGMA commands, understand why SQLite is an in-process library (not a server), and document findings as part of System Design Assignment 1 (PostgreSQL vs SQLite3).

---

## Part 1: Installation & Verification

SQLite3 is installed on the system and verified:
```bash
sqlite3 --version
# e.g.: 3.51.0 2025-06-12 ...
```

---

## Part 2: Storage Internals via PRAGMA

SQLite stores the entire database as a single file divided into fixed-size pages. The page size is set at database creation and cannot be changed afterwards (without `VACUUM INTO`).

- **Page Size**: (`PRAGMA page_size;`) unit of disk I/O, defaults to 4096 bytes (matches OS virtual memory page size).
- **Page Count**: (`PRAGMA page_count;`) total pages allocated. Total database size = `page_size * page_count`.
- **mmap Size**: (`PRAGMA mmap_size;`) size threshold for memory-mapped files. Tuning this bypasses traditional `read()` syscalls for direct memory access via OS-level virtual memory mapping.

---

## Part 3: SQLite3 is a Library, Not a Process

Unlike PostgreSQL (which operates as a client-server daemon over sockets), SQLite is an **in-process library** compiled and linked directly into the host application.

```
Your application binary
  └── links libsqlite3.dylib / .so (or statically embeds it)
        └── reads/writes the .db file directly via OS syscalls
```

- No separate background daemon is spawned.
- All database function calls are simple CPU functions within your program's address space.

---

## System Design Assignment 1: PostgreSQL vs SQLite3

| Dimension            | SQLite3                                      | PostgreSQL                                      |
|----------------------|----------------------------------------------|-------------------------------------------------|
| **Process model**    | Library — runs inside host application       | Client-server — separate `postgres` daemon      |
| **Communication**    | Direct function calls / local file I/O        | TCP socket (default 5432) or Unix domain socket |
| **Concurrency**      | File locks (one concurrent writer at a time)  | Multi-Version Concurrency Control (MVCC)        |
| **Authentication**   | None (relies on local OS file permissions)   | Fine-grained users, roles, SSL, passwords       |
| **Storage format**   | Single `.db` file                            | Data directory with many files + WAL segments   |
| **Transactions**     | ACID (serialized writes)                     | Full ACID with selectable isolation levels      |

---

## Code Structure

- [sqlite_internals.cpp](file:///Users/kushalsacharya/Desktop/scaler-Adv-DBMS/lab%202/sqlite_internals.cpp): Programmatically interacts with `students.db` utilizing standard SQLite C++ API, querying page size/counts, tuning `mmap_size`, inserting and selecting data.
- [Makefile](file:///Users/kushalsacharya/Desktop/scaler-Adv-DBMS/lab%202/Makefile): Defines compilation and clean directives.

---

## Build and Run Instructions

To compile the application:
```bash
make
```

To run the programmatic SQLite internals demonstration:
```bash
make run
```

To clean build artifacts and databases:
```bash
make clean
```
