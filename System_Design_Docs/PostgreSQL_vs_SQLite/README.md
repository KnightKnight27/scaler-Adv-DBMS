# PostgreSQL vs SQLite Architecture Comparison

## 1. Problem Background

PostgreSQL and SQLite are both relational database systems, but they were designed for very different operating environments.

PostgreSQL exists to run as a shared database service for many concurrent clients. It is a client-server DBMS: applications connect to a long-running server process, and that server owns shared memory, background processes, transaction coordination, logging, recovery, and access to database files. This design is useful for large applications where many users need isolation, durability, permissions, backup, replication, and predictable behavior under concurrent load.

SQLite solves a different problem. It is an embedded database library linked directly into the application. There is no separate database server process. The database is normally a single cross-platform file, and the application reads and writes it through SQLite's library code. This makes SQLite attractive for mobile apps, desktop apps, local caches, edge devices, tests, and small tools where zero administration and simple deployment matter more than high write concurrency.

The main architectural difference is therefore not SQL syntax. It is ownership of coordination. PostgreSQL centralizes coordination inside a database server. SQLite pushes coordination into a compact library and the operating system's file-locking behavior.

## 2. Architecture Overview

```text
PostgreSQL

Client apps
    |
    v
Postmaster / listener
    |
    +--> backend process per connection
    |        |
    |        +--> parser, planner, executor
    |        +--> lock manager and MVCC visibility checks
    |
    +--> shared memory
    |        +--> shared buffers
    |        +--> WAL buffers
    |        +--> lock tables
    |
    +--> background workers
             +--> checkpointer
             +--> background writer
             +--> WAL writer
             +--> autovacuum

Disk:
    data files, index files, pg_wal, catalog files
```

```text
SQLite

Application process
    |
    v
SQLite library
    |
    +--> SQL parser and virtual machine
    +--> B-tree layer
    +--> pager
    +--> journal or WAL manager
    +--> OS / VFS abstraction

Disk:
    database file
    rollback journal or -wal / -shm files when needed
```

### High-Level Data Flow

In PostgreSQL, a query travels from the client connection into a backend process. The backend parses and plans the query, checks locks and tuple visibility, reads table or index pages through the shared buffer manager, and writes changes through WAL before data pages are later flushed. Since all clients pass through the server, PostgreSQL can coordinate global state such as locks, snapshots, cache replacement, checkpoints, and recovery.

In SQLite, the application calls SQLite directly. The SQL engine compiles statements into bytecode for the SQLite virtual machine. The B-tree layer finds table or index pages, and the pager loads pages from the database file. Transactions are protected using rollback journals or WAL files plus file locks. There is no separate shared buffer pool across application processes.

## 3. Internal Design

### Overall Architecture

PostgreSQL uses a multi-process server architecture. Each client connection is handled by a backend process, while shared memory holds common state such as buffers and lock tables. Background processes handle maintenance tasks like checkpointing, writing dirty buffers, WAL flushing, and autovacuum. The cost is more operational complexity and server overhead; the benefit is strong coordination for many clients.

SQLite uses an in-process architecture. The database engine runs in the same process as the application. This removes network and server scheduling overhead. It also means that cross-process coordination must be handled using database files, journals, WAL files, shared-memory helper files, and OS locks instead of a central database server.

### Process Model

PostgreSQL has a postmaster process that accepts connections and starts backend processes. This model isolates client sessions and allows the database to enforce transaction isolation, memory ownership, crash recovery, and background maintenance centrally.

SQLite has no database daemon. If five applications open the same SQLite file, there are five independent processes running SQLite library code. The database file is the shared object, and file locking becomes the coordination mechanism.

### Storage Engine And File Organization

PostgreSQL stores relations as files inside a database cluster directory. Tables and indexes are stored as fixed-size pages, commonly 8 KB. Heap tables are not clustered by primary key by default. A row lives in a heap page, and indexes point to heap tuple locations. This separation keeps table storage general-purpose and lets PostgreSQL support many index types through an access method interface.

SQLite normally stores the entire database in one main database file. The file is divided into pages. Tables and indexes are stored as B-tree structures inside that file. A rowid table is organized around an integer rowid B-tree, while indexes are separate B-trees. The single-file design is one of SQLite's biggest practical advantages because backup, copy, and deployment can be as simple as moving one file.

### Page Layout

PostgreSQL heap pages contain a page header, line pointers, tuple data, and free space. Line pointers allow tuples to move within the page while external references can still use a stable item identifier. Tuple headers store MVCC metadata such as transaction IDs used to determine visibility.

SQLite database pages contain B-tree page headers, cell pointer arrays, cell content areas, and free space. Since tables and indexes are B-trees, page structure is directly tied to ordered key lookup and range traversal. Overflow pages are used when payloads do not fit cleanly inside a B-tree page.

### Index Implementation

PostgreSQL's default index type is B-tree, but the index is separate from the heap table. A primary-key index maps key values to tuple identifiers. Under MVCC, multiple versions of a logical row may exist, and indexes may contain entries for versions that later become dead. This is one reason VACUUM matters.

SQLite uses B-trees as a central storage abstraction. Table B-trees store table records, and index B-trees store index keys. Because the database is embedded and file-oriented, its B-tree and pager layers are tightly connected.

### Transaction Management

PostgreSQL implements transactions using MVCC, WAL, locks, and snapshots. A transaction sees rows according to its snapshot. Writers create new tuple versions instead of overwriting visible rows in place. WAL records are flushed before changed data pages are considered durable. This supports high read concurrency because readers generally do not block writers and writers generally do not block readers.

SQLite supports ACID transactions using a rollback journal or WAL mode. In rollback-journal mode, original page content is preserved so a failed transaction can be undone. In WAL mode, changes are appended to a WAL file and later checkpointed back into the main database. WAL mode improves read/write overlap because readers can continue using a stable snapshot while the writer appends newer changes.

### Concurrency Control

PostgreSQL is designed for many concurrent users. MVCC allows each transaction to read a consistent snapshot. Locks are still required for schema changes, conflicting row updates, and internal consistency, but ordinary reads do not need to block ordinary writes.

SQLite allows many readers, but write concurrency is intentionally limited. Even in WAL mode, there can be multiple readers but only one writer at a time. This is a good trade-off for embedded and local workloads because it keeps the engine small and reliable. It becomes a limitation for write-heavy multi-user server applications.

### Durability And Recovery

PostgreSQL uses write-ahead logging. WAL records describing changes are persisted before the corresponding data pages are written. After a crash, PostgreSQL replays WAL to bring data files back to a consistent state. Checkpoints limit the amount of WAL that must be replayed.

SQLite uses rollback journals or WAL files depending on journal mode. In rollback-journal mode, recovery rolls back incomplete changes. In WAL mode, recovery uses the WAL file and WAL-index state to determine what committed frames should be visible and checkpointed.

## 4. Design Trade-Offs

| Area | PostgreSQL | SQLite |
| --- | --- | --- |
| Deployment | Requires server setup and administration | Single library and database file |
| Architecture | Client-server | Embedded |
| Best workload | Multi-user, concurrent, large datasets | Local, mobile, desktop, embedded, small to medium data |
| Concurrency | High read/write concurrency through MVCC | Many readers, single writer |
| Storage | Relation files, heap pages, separate indexes | Single database file with B-tree pages |
| Durability | WAL, checkpoints, crash recovery | Rollback journal or WAL mode |
| Operations | Roles, permissions, backups, replication, monitoring | Minimal administration |
| Cost | More memory and process overhead | Very low startup and runtime overhead |

PostgreSQL chooses a heavier architecture because it optimizes for shared use. A server process can maintain shared buffers, coordinate locks, run background workers, and recover from crashes independently of any one client application. This makes PostgreSQL a better fit for production systems where many users write concurrently, data is centrally managed, and operational features matter.

SQLite chooses a compact embedded architecture because it optimizes for simplicity and locality. Removing the server removes installation complexity, network latency, and administrative overhead. The trade-off is that SQLite cannot coordinate high write concurrency in the same way a database server can.

## 5. Experiments / Observations

The local exploration in this repository compared a simple `users` table in SQLite and PostgreSQL.

### SQLite Observations

```sql
PRAGMA page_size;
-- 4096

PRAGMA page_count;
-- 47
```

Approximate database size:

```text
4096 bytes * 47 pages = 192512 bytes
```

The observed `SELECT * FROM users;` timing changed after enabling memory mapping:

| SQLite mode | Observed time |
| --- | ---: |
| Default mmap setting | about 209 ms |
| `PRAGMA mmap_size = 268435456` | about 53 ms |

This shows how reducing page read overhead and system calls can matter for local file-based reads. The improvement is not proof that SQLite is always faster; it shows that SQLite's performance is strongly affected by local file access, cache state, and pager configuration.

### PostgreSQL Observations

```sql
SHOW block_size;
-- 8192

SELECT relpages FROM pg_class WHERE relname = 'users';
-- 41 after ANALYZE
```

The observed `SELECT * FROM users;` timing improved on repeat execution:

| PostgreSQL query run | Observed time |
| --- | ---: |
| First run | about 16.131 ms |
| Second run | about 5.083 ms |

The second run was faster because PostgreSQL and the operating system can serve pages from cache. The observation also explains why `ANALYZE` matters: PostgreSQL's planner depends on collected statistics, and relation page estimates such as `relpages` may not reflect current table contents until statistics are refreshed.

### Interpretation

SQLite performed well as a local embedded database and benefited from mmap. PostgreSQL showed the benefit of shared buffers, OS caching, and planner statistics. The experiment reflects the architectural difference: SQLite is optimized for direct local file access, while PostgreSQL is optimized for managed access through a server that can coordinate cache, planning, concurrency, and durability.

## 6. Key Learnings

- PostgreSQL and SQLite are not competitors in only a "which is faster" sense; they optimize for different deployment models.
- PostgreSQL's client-server design makes sense when many clients need shared access, strong concurrency, observability, backup, and recovery.
- SQLite's embedded design makes sense when the database should be part of the application and easy to copy, ship, test, or run offline.
- PostgreSQL's MVCC improves concurrency but creates dead tuple cleanup work, which is why VACUUM is a core part of the design.
- SQLite's single-writer model is a deliberate simplification. It is a limitation for write-heavy server workloads but a strength for reliability and small footprint.
- Page size, cache state, journaling mode, and mmap settings have visible performance impact because both systems ultimately move fixed-size pages between disk and memory.
- Architecture determines behavior: PostgreSQL spends resources on coordination; SQLite avoids that cost by narrowing the coordination problem.

## References

- PostgreSQL Documentation: Database Page Layout - https://www.postgresql.org/docs/current/storage-page-layout.html
- PostgreSQL Documentation: Write-Ahead Logging - https://www.postgresql.org/docs/current/wal-intro.html
- PostgreSQL Documentation: Explicit Locking - https://www.postgresql.org/docs/current/explicit-locking.html
- PostgreSQL Documentation: B-Tree Indexes - https://www.postgresql.org/docs/current/btree.html
- SQLite Documentation: Database File Format - https://www.sqlite.org/fileformat.html
- SQLite Documentation: Write-Ahead Logging - https://www.sqlite.org/wal.html
- SQLite Documentation: WAL-Mode File Format - https://www.sqlite.org/walformat.html
- SQLite Documentation: OS Interface / VFS - https://www.sqlite.org/vfs.html
