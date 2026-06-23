# PostgreSQL vs SQLite

## Student Details
- Name: Harshit Tiwari
- Roll Number: 24BCS10277

## High-Level Architecture

| Area | PostgreSQL | SQLite |
| --- | --- | --- |
| Deployment model | Client-server database system | Embedded database library |
| Process boundary | Separate server process handles sessions | Runs inside the application process |
| Storage | Multiple files per database cluster | One database file plus optional journal/WAL files |
| Concurrency | MVCC with many concurrent readers/writers | Many readers, usually one writer at a time |
| Administration | Needs server config, users, roles, backups | Lightweight setup, simple file copying for small apps |

## Storage Design
PostgreSQL stores relations in fixed-size pages, usually 8 KB. It also maintains catalog tables, visibility information, WAL files, and background processes. This layout is better for large multi-user systems because the engine controls memory, checkpoints, and recovery.

SQLite stores the whole database inside a single file organized into pages. B-trees are used for tables and indexes. The simplicity is useful for local applications, offline-first tools, mobile apps, and small embedded services.

## Concurrency
PostgreSQL uses MVCC so readers do not block writers in normal cases. Each transaction sees a consistent snapshot based on transaction IDs. Updates create new row versions, and vacuum later removes dead tuples.

SQLite supports concurrent readers but serializes writes. WAL mode improves reader/writer overlap because readers can continue using the main database file while writers append changes to the WAL.

## When I Would Choose Each
- Choose PostgreSQL for multi-user systems, analytics, high write concurrency, access control, replication, and production services.
- Choose SQLite for local storage, prototyping, test fixtures, mobile apps, command-line tools, and applications where an external database server is unnecessary.

## Key Takeaway
SQLite optimizes for simplicity and portability. PostgreSQL optimizes for scalable concurrency, operational control, and richer database features.
