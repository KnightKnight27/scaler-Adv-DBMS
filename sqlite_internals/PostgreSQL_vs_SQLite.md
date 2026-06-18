# System Design Assignment 1 — PostgreSQL vs SQLite3

A comparison grounded in Lab 2's observations of `students.db` (a single
16 KB file, 4 pages of 4096 B, `mmap_size=0`, `journal_mode=delete`).

## Architecture

| Dimension       | SQLite3                                      | PostgreSQL                                      |
|-----------------|----------------------------------------------|-------------------------------------------------|
| Process model   | Library — runs **inside** your process       | Client-server — separate `postgres` daemon      |
| Communication   | Direct function calls / file I/O             | TCP socket (default :5432) or Unix socket       |
| Concurrency     | File locks; one writer at a time (WAL helps) | MVCC — many concurrent readers **and** writers  |
| Authentication  | None (filesystem permissions only)           | Roles, passwords, SSL, `pg_hba.conf`            |
| Storage         | One `.db` file of fixed-size pages           | A data directory of many files + WAL segments   |
| Buffer cache    | Page cache + optional `mmap` of the file     | Server-managed `shared_buffers` pool            |
| Transactions    | ACID, writes serialized                      | Full ACID with selectable MVCC isolation levels |
| Type system     | Dynamic type affinity (`SERIAL` ≈ TEXT/INT)  | Strict, rich types; `SERIAL` is a real sequence |

## When to use SQLite3

- Embedded apps — mobile, desktop, CLI tools, browsers.
- Local dev / test databases.
- Single-user or low-concurrency, read-heavy workloads.
- Zero infrastructure: no server to install, configure, or operate.

## When to use PostgreSQL

- Multi-user systems with **concurrent writers**.
- Web/API backends serving many simultaneous clients.
- Need for row-level locking and stronger isolation (REPEATABLE READ, SERIALIZABLE).
- Complex queries, full-text search, JSON operators, extensions (PostGIS, …).
- Authentication, roles, SSL, and auditing in production.

## How mmap fits in

- SQLite can `mmap()` the database file so the process and the OS page cache
  share the same physical pages — fast sequential reads, no `read()` syscalls
  (Lab 2 shows it **off** by default, `mmap_size=0`).
- PostgreSQL manages its own `shared_buffers` pool inside the server process and
  does not rely on mmap for its primary I/O path. (Its buffer eviction is the
  ClockSweep algorithm built in Lab 3.)

## The schema portability gap

`students.db` was created from PostgreSQL-style DDL (`SERIAL PRIMARY KEY`,
`VARCHAR(100)`, `TIMESTAMP DEFAULT CURRENT_TIMESTAMP`). It loads in SQLite, but:

- `SERIAL` is **not** an auto-increment sequence in SQLite — it is just a type
  token; you would use `INTEGER PRIMARY KEY AUTOINCREMENT` instead.
- `VARCHAR(100)` length is advisory only (TEXT affinity, no enforcement).
- `UNIQUE`/`NOT NULL`/`PRIMARY KEY` *are* enforced in both.

So the same script is portable in syntax but not in semantics.

## Key insight

SQLite's single-file, in-process design is nearly unbeatable for portability and
simplicity; PostgreSQL's client-server, MVCC design is unbeatable for concurrent
multi-user workloads. **The deciding question is: who writes to the database, and
how many of them at once?**
