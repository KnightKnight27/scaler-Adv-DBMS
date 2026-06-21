# PostgreSQL vs SQLite: Architecture Comparison

**Name:** Harshita Hirawat  
**Roll number:** 24BCS10044

## 1. Problem Background

PostgreSQL and SQLite both provide SQL and ACID transactions, but they solve
different deployment problems.

SQLite is meant to be part of an application. The application calls a library,
and that library reads or writes one database file. This is useful when the data
belongs to one device or one application and running a database server would be
more work than the problem requires.

PostgreSQL is meant to be shared. A separate server owns the database files and
many clients connect to it. That extra server boundary enables centralized
authentication, memory management, background maintenance, concurrent sessions,
and crash recovery. It costs more to operate, but it fits multi-user systems.

The important difference is therefore not “small database versus large
database.” It is **embedded ownership versus shared service ownership**.

## 2. Architecture Overview

```text
SQLite
Application process
    |
    +-- SQL parser / virtual machine / pager / B-tree
    |            (all inside libsqlite3)
    |
    +-- database.db + journal or WAL

PostgreSQL
Clients -- TCP/Unix socket --> PostgreSQL server
                                |
                                +-- backend processes
                                +-- shared buffer pool
                                +-- planner and executor
                                +-- background writer/checkpointer
                                |
                                +-- relation files + WAL + catalogs
```

SQLite is [serverless](https://www.sqlite.org/serverless.html): there is no
intermediate server process. PostgreSQL uses a
[client/server model](https://www.postgresql.org/docs/current/tutorial-arch.html),
so clients do not access relation files directly.

### Request flow

- **SQLite:** function call -> SQL bytecode -> pager -> OS file API.
- **PostgreSQL:** client message -> server backend -> planner/executor -> shared
  buffers -> storage manager.

The PostgreSQL path has communication overhead, but the server can coordinate
many users. SQLite avoids that overhead, but all participating processes must
coordinate through file locks.

## 3. Internal Design

| Area | SQLite | PostgreSQL |
|---|---|---|
| Process model | Library inside the application | Server with client backends and background processes |
| Main storage | One database file | Data directory containing relation forks, catalogs, WAL, and other files |
| Default page size observed | 4 KB | 8 KB |
| Cache | SQLite page cache plus OS cache | Shared buffers plus OS cache |
| Table organization | Table B-tree; rowid is normally the integer key | Heap table separate from indexes |
| Indexes | B-trees stored in the database file | Separate index relations; B-tree is the default index type |
| Concurrency | Many readers, normally one writer; WAL improves reader/writer overlap | MVCC allows many readers and writers; row/table locks handle conflicts |
| Durability | Rollback journal or WAL with `synchronous` policy | WAL, checkpoints, full-page images, and recovery replay |
| Administration | File permissions; no server roles | Roles, privileges, SSL, backup, monitoring, replication |

### Storage and page organization

SQLite divides the database file into fixed-size pages. Page 1 contains the
database header; tables and indexes are represented by B-tree pages. Its
[file-format documentation](https://www.sqlite.org/fileformat.html) explains
the page header, cell pointer array, cells, free blocks, and overflow pages.

PostgreSQL stores each table and index as a relation with one or more forks.
Inside a normal 8 KB page are a page header, line pointers, free space, tuples,
and sometimes index-specific special space. A line pointer stays stable even if
the tuple moves within the page. This indirection is useful to indexes and MVCC.
The exact layout is described in the PostgreSQL
[page-layout documentation](https://www.postgresql.org/docs/current/storage-page-layout.html).

### Transactions and concurrency

SQLite locking is database-file oriented. In rollback-journal mode a writer
eventually needs exclusive access. WAL mode lets readers continue while one
writer appends to the WAL, but it still does not turn SQLite into a many-writer
server. See SQLite's [locking](https://www.sqlite.org/lockingv3.html) and
[WAL](https://www.sqlite.org/wal.html) documentation.

PostgreSQL stores multiple tuple versions. A reader applies snapshot visibility
rules instead of waiting for every writer. Old versions later require VACUUM.
This spends storage and maintenance work to obtain much higher concurrency.

## 4. Design Trade-Offs

### Why SQLite works well on mobile and desktop devices

- Deployment is one library and one portable file.
- Calls do not cross a network boundary.
- The database follows the application's lifecycle.
- Device workloads usually have few simultaneous writers.
- Backup or transfer can be simple, but a live WAL-mode database should be
  copied through SQLite's backup API or after a safe checkpoint; copying only
  the main file while its WAL is active can omit committed changes.

The limitation is coordination. A local notes application rarely has dozens of
independent writers, so a single-writer design is a sensible simplification.

### Why PostgreSQL fits multi-user systems

- The server owns concurrency, authentication, memory, and recovery.
- Clients can be on different machines and written in different languages.
- MVCC reduces read/write blocking.
- Background processes perform checkpoints, WAL writing, vacuuming, and
  statistics collection.
- Replication and operational controls are centralized.

The cost is a service that must be configured, monitored, patched, and backed
up. For a single-user local file, that cost is often unnecessary.

### A practical choice

Choose SQLite when the application is the natural owner of the data. Choose
PostgreSQL when the data must be a shared, independently managed service. Moving
from SQLite to PostgreSQL is not simply “scaling the file”; it changes ownership,
communication, concurrency, and operations.

## 5. Experiments / Observations

### Environment and dataset

- SQLite 3.53.1, Windows x64
- Temporary database with 10,000 customers and 200,000 orders
- `page_size=4096`, `journal_mode=WAL`, `synchronous=FULL`
- Point query: `SELECT SUM(total) FROM orders WHERE customer_id=7777;`
- Five runs were executed inside one SQLite connection before and after adding
  `CREATE INDEX idx_orders_customer ON orders(customer_id)`.

### Results

| Observation | Without secondary index | With secondary index |
|---|---:|---:|
| Query plan | `SCAN orders` | `SEARCH orders USING INDEX idx_orders_customer` |
| Average query time | 8.205 ms | 0.052 ms |
| Database pages | 1,635 | 2,168 |

The plan and file-layout observations can be reproduced with SQLite's own
introspection commands:

```sql
EXPLAIN QUERY PLAN
SELECT SUM(total) FROM orders WHERE customer_id=7777;

PRAGMA page_size;
PRAGMA page_count;
PRAGMA journal_mode;
```

Running the plan command again after creating `idx_orders_customer` exposes the
change from a full table scan to an indexed search. `page_count * page_size`
provides an independent check against the main database-file size after the WAL
has been checkpointed.

The index added 533 pages, about 2.08 MiB at 4 KB per page. In return, SQLite
stopped examining 200,000 rows and performed a B-tree lookup for ten matching
orders. This is a concrete storage-versus-read-speed trade-off: the index makes
reads much cheaper, but it consumes space and must be updated on writes.

The final file size was 8,880,128 bytes (`2168 * 4096`), confirming that the
database is organized in complete pages. WAL mode was reported by
`PRAGMA journal_mode`.

For architectural context, a separate isolated PostgreSQL 17.9 instance
reported an 8 KB block size and 128 MB of shared buffers. These are not used as
a direct speed comparison—the PostgreSQL experiment used a different join
workload—but they demonstrate the server-managed cache and larger default page.

### Interpretation limits

The timing is a local warm-cache measurement, not a claim that SQLite is always
faster or slower. Dataset shape, cache state, client/server communication, SQL
shape, and durability settings can dominate small benchmarks.

## 6. Key Learnings

- SQLite's simplicity comes from keeping the database engine in the application,
  not from omitting transactions or indexes.
- PostgreSQL accepts server and maintenance complexity to coordinate concurrent
  users safely.
- Both systems use pages and B-trees, but ownership of caching, files, and
  concurrency is fundamentally different.
- An index converts work from repeated scans into extra storage and write
  maintenance; the measured plan change made that trade-off visible.
- Database selection should begin with workload ownership and concurrency, not
  only database size.

## Sources Consulted

- [SQLite architecture](https://www.sqlite.org/arch.html)
- [SQLite database file format](https://www.sqlite.org/fileformat.html)
- [SQLite WAL](https://www.sqlite.org/wal.html)
- [PostgreSQL architectural fundamentals](https://www.postgresql.org/docs/current/tutorial-arch.html)
- [PostgreSQL page layout](https://www.postgresql.org/docs/current/storage-page-layout.html)
- [PostgreSQL concurrency control](https://www.postgresql.org/docs/current/mvcc.html)
