# PostgreSQL vs SQLite: Architecture Comparison

> Advanced DBMS, System Design Discussion
> Experiments below were run locally on **PostgreSQL 18.3** and **SQLite 3.45.1**.

---

## 1. Problem Background

Both PostgreSQL and SQLite are relational databases that speak SQL, but they were built to solve completely different problems. Almost every difference between them comes from one decision: who runs the database engine?

- **PostgreSQL** (1986, from Berkeley's POSTGRES project) is a **client-server** database. It is a long-running server program that many clients connect to over a socket. It was designed for the classic multi-user case, like a web app or an analytics system where many users hit the same data at the same time and need correctness, durability, and access control. Its priorities are scalability, concurrency, centralization, and control.

- **SQLite** (2000, by D. Richard Hipp) is an **embedded** database. It is just a C library that you link into your program. There is no server, no daemon, and no network port. The whole database is a single ordinary file on disk. The SQLite project puts it nicely:

  > *"SQLite does not compete with client/server databases. SQLite competes with `fopen()`."*

  So SQLite is meant to replace the application's own file format, giving you transactional SQL instead of hand-written file parsing. Its priorities are economy, efficiency, reliability, and simplicity.

This one split, server vs. library, explains almost everything else: the process model, how data is stored, how concurrency works, and where each one is the right tool. SQLite is the most widely deployed database engine in the world (an estimated 1 trillion-plus databases in use, since it ships in every phone, every major browser, and lots of other devices), and that is precisely *because* it has no server to install or administer.

---

## 2. Architecture Overview

### PostgreSQL: client-server, one process per connection

```
   Client A      Client B      Client C        (psql, app, ORM…)
      │             │             │              connect over TCP / unix socket
      ▼             ▼             ▼
 ┌─────────────────────────────────────────────┐
 │             POSTMASTER (supervisor)          │  listens, authenticates,
 │     forks one backend per new connection     │  spawns & monitors everything
 └───────┬──────────┬──────────┬────────────────┘
         ▼          ▼          ▼
   ┌─────────┐ ┌─────────┐ ┌─────────┐
   │ backend │ │ backend │ │ backend │   one OS PROCESS per connection
   │ (proc)  │ │ (proc)  │ │ (proc)  │   parses/plans/executes that session
   └────┬────┘ └────┬────┘ └────┬────┘
        └───────────┼───────────┘
                    ▼
   ┌───────────────────────────────────────────┐
   │            SHARED MEMORY                    │
   │   shared_buffers (8KB page cache)           │  ← all backends share this
   │   WAL buffers, lock tables, …               │
   └───────────────────────────────────────────┘
        ▲                ▲              ▲
   background helpers (their own processes):
   bgwriter · checkpointer · walwriter · autovacuum · stats
                    │
                    ▼
   ┌───────────────────────────────────────────┐
   │   DATA DIRECTORY on disk (the "cluster")    │
   │   base/  (heap + index files)  pg_wal/ …    │
   └───────────────────────────────────────────┘
```

**Process model.** PostgreSQL uses one OS process per connection, not threads. The *postmaster* is the first process, and on each new connection it `fork()`s a dedicated **backend process** that handles all of that session's queries. Backends do not touch each other's memory directly. They share data through a region of **shared memory** that the postmaster sets up at startup (`shared_buffers`, WAL buffers, lock tables). Several background processes run alongside them: the background writer and checkpointer (flush dirty pages), the WAL writer (flushes the log), and autovacuum workers (clean up dead rows).

- Default `max_connections = 100`. Each backend costs a few MB of RAM, which is why busy systems put a connection pooler (PgBouncer) in front.
- Data flow of a query: client → backend parses SQL → planner picks a plan using statistics → executor reads/writes 8KB pages through `shared_buffers` → changes recorded in the WAL → committed.

### SQLite: embedded library, single file

```
 ┌───────────────────────────────────────────────┐
 │              YOUR APPLICATION                   │
 │   (phone app, browser, CLI, test suite…)        │
 │                                                 │
 │   ┌─────────────────────────────────────────┐  │
 │   │        SQLite library (sqlite3.c)        │  │  linked IN-PROCESS,
 │   │  ┌──────────────────────────────────┐   │  │  no server, no socket
 │   │  │ Tokenizer → Parser → Code Gen    │   │  │
 │   │  ├──────────────────────────────────┤   │  │
 │   │  │ VDBE  (bytecode virtual machine) │   │  │  runs the query plan
 │   │  ├──────────────────────────────────┤   │  │
 │   │  │ B-Tree module                    │   │  │  tables & indexes
 │   │  ├──────────────────────────────────┤   │  │
 │   │  │ Pager (cache + transactions)     │   │  │  locking, journal
 │   │  ├──────────────────────────────────┤   │  │
 │   │  │ OS Interface (VFS)               │   │  │  portable file I/O
 │   │  └──────────────────────────────────┘   │  │
 │   └────────────────────┬────────────────────┘  │
 └────────────────────────┼───────────────────────┘
                          ▼
              ┌──────────────────────────┐
              │   ONE database file       │   mydata.db
              │  (+ -journal or -wal)     │   on the local filesystem
              └──────────────────────────┘
```

SQLite is a layered library. SQL text flows down through the tokenizer, parser, and code generator, which compile it into **VDBE bytecode** (a small portable instruction set). The **VDBE** (Virtual Database Engine) runs those opcodes, calling the **B-tree module** for storage, which calls the **pager** (page cache plus transaction and locking control), which calls the **VFS**. The VFS is the OS abstraction layer that does the real `read`/`write`/`lock`, so SQLite runs the same way on Linux, Windows, iOS, and so on.

The whole engine ships as the **amalgamation**: a single C file (`sqlite3.c`, around 238K lines) that you compile into your program. No process, no config, no admin.

I checked the bytecode model locally. `EXPLAIN` shows the VDBE program that SQLite actually runs:

```
sqlite> EXPLAIN QUERY PLAN SELECT * FROM students WHERE email='a@b.com';
`--SEARCH students USING INDEX sqlite_autoindex_students_2 (email=?)

sqlite> EXPLAIN SELECT * FROM students WHERE email='a@b.com';
addr  opcode      p1  p2  p3  p4         comment
0     Init        0   16  0
1     OpenRead    0   2   0   7              -- open the students table b-tree (rootpage 2)
2     OpenRead    1   4   0   k(2,,)         -- open the email index b-tree   (rootpage 4)
3     String8     0   1   0   a@b.com
4     SeekGE      1   15  1   1              -- seek into the index
5     IdxGT       1   15  1   1
...
```

---

## 3. Internal Design

### 3.1 Database file organization

| | PostgreSQL | SQLite |
|---|---|---|
| Layout | A directory ("the cluster") with many files | One single file for the whole DB |
| One table = | one (or more) files (a *relation*) | one B-tree inside the shared file |
| Page size | 8 KB (fixed at compile time) | 4096 B default (since v3.12, 2016; was 1024 B) |
| Large tables | split into 1 GB segments (`filenode`, `filenode.1`, …) | grows the one file; rows that overflow a page spill to overflow pages |
| Big values | TOAST (out-of-line storage for oversized columns) | overflow page chains |
| Extra per-table files | Free Space Map (`_fsm`), Visibility Map (`_vm`) | none, it is all in the one file |

The key thing about SQLite is that *everything* in the file is a B-tree. The table is a B-tree keyed by an integer `rowid` (the row data sits in the leaves), and each index is its own separate B-tree. I verified this directly on `students.db`:

```
sqlite> PRAGMA page_size;  -- 4096
sqlite> PRAGMA page_count; -- 4   (entire DB is 4 pages = 16384 bytes)
sqlite> SELECT type, name, rootpage FROM sqlite_schema;
table  students                      2     <- table b-tree starts at page 2
index  sqlite_autoindex_students_1   3     <- PK index b-tree at page 3
index  sqlite_autoindex_students_2   4     <- UNIQUE(email) index b-tree at page 4

$ xxd students.db | head -1
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
```

The first 100 bytes of the file are a fixed header (notice the literal text `SQLite format 3`), and `rootpage` says which page each B-tree starts on. One file, three B-trees.

PostgreSQL's heap files, by contrast, are unordered (that is what "heap" means). Rows go wherever there is free space, and ordering is provided by separate index files.

### 3.2 Memory management

- PostgreSQL caches 8 KB pages in `shared_buffers` (shared across all backends) using a clock-sweep replacement policy, plus per-backend `work_mem` for sorts and hashes. Because the memory is shared, all sessions benefit from one cache.
- SQLite has a per-connection page cache in the pager (`PRAGMA cache_size`). There is no shared server memory because there is no server. Each process that opens the file caches its own pages and relies on the OS page cache underneath.

### 3.3 Transactions, concurrency, and durability

This is the most important practical difference.

**PostgreSQL: full MVCC, many writers.** PostgreSQL uses Multi-Version Concurrency Control. An `UPDATE` writes a new version of the row instead of overwriting it in place, so readers never block writers and writers never block readers. Many transactions can read and write different rows at the same time. Locking is row-level when it is actually needed. Durability comes from Write-Ahead Logging: the change is written and `fsync`'d to the WAL before COMMIT returns, and the WAL is replayed after a crash.

**SQLite: file-level locking, one writer at a time.** SQLite's concurrency is coarse-grained because it is coordinating separate processes through one shared file, using OS file locks. It has five lock states:

```
UNLOCKED → SHARED (read; many allowed) → RESERVED (plans to write, readers still allowed)
        → PENDING (wants exclusive; blocks NEW readers) → EXCLUSIVE (write; alone)
```

Two journaling modes control durability and concurrency:

- **Rollback journal (default):** before modifying pages, SQLite copies the originals into a `-journal` file, and commit means deleting that journal. While one connection writes, readers are blocked (the writer has to reach EXCLUSIVE). A crash leaves a "hot journal" that gets rolled back on the next open.
- **WAL mode** (`PRAGMA journal_mode=WAL`): new changes append to a `-wal` file. Now readers do not block the writer and the writer does not block readers, but there is still only one writer at a time. I switched `demo.db` into WAL mode and the side file appeared:

  ```
  sqlite> PRAGMA journal_mode=WAL;   -- "wal"
  # a -wal (and -shm shared-memory index) file is created next to the DB
  ```

`PRAGMA synchronous` tunes how aggressively SQLite calls `fsync` (OFF / NORMAL / FULL / EXTRA). The default for the rollback journal is FULL. On `students.db`, `PRAGMA synchronous` returned `2` (which is FULL).

In short: PostgreSQL gives true multi-writer concurrency, while SQLite gives unlimited readers but a single writer. Serializing writers through one file is simple and correct, and SQLite's target workloads rarely need more.

---

## 4. Design Trade-Offs

| Dimension | PostgreSQL | SQLite |
|---|---|---|
| Deployment | Install, configure, run and administer a server | Zero-config: link a library / open a file |
| Concurrency | Many concurrent readers and writers (MVCC) | Many readers, one writer at a time |
| Scalability | Scales to many clients, big data, replication | Bounded by a single file on one machine |
| Network access | Built-in (clients connect remotely) | None, same machine and same process only |
| Access control | Roles, GRANT/REVOKE, row-level security | Whoever can read the file can read the data |
| Crash safety | WAL + checkpoints + replication | Atomic-commit journal / WAL; very robust for 1 file |
| Resource cost | MBs per backend; needs RAM and a daemon | Tiny (~1 MB library); runs on microcontrollers |
| Feature surface | Huge: types, extensions, parallelism, FDWs | Deliberately minimal but surprisingly complete |
| Best at | Multi-user server workloads | Embedded / on-device / app file format |

Why the trade-offs make sense:
- PostgreSQL pays the price of a server process (memory, admin, a network hop) to buy concurrency, isolation between users, central control, and the ability to scale out. For a banking backend or a SaaS app with thousands of users, that price is clearly worth it.
- SQLite refuses to pay that price. By being a library with file-level locking it becomes basically invisible: no port to secure, no service to crash, nothing to tune. The cost is that heavy concurrent-write or multi-machine workloads are simply not its job.

---

## 5. Experiments / Observations

All run locally, and the outputs are real.

**(a) SQLite really is one file, and every object is a B-tree inside it.**
`students.db` is 16384 bytes, which is exactly 4 pages of 4096 B. `sqlite_schema` lists the table and both auto-indexes with distinct `rootpage` values (2, 3, 4), so that is three B-trees in one file (see section 3.1). This is the embedded model made concrete.

**(b) SQLite compiles SQL to VDBE bytecode.** `EXPLAIN` printed the actual opcode program (`Init`, `OpenRead`, `SeekGE`, `IdxGT`, etc.). The query used the `email` UNIQUE index, exactly as `EXPLAIN QUERY PLAN` predicted (`SEARCH … USING INDEX`). This makes the tokenizer → parser → VDBE → B-tree layering visible.

**(c) SQLite WAL mode.** Setting `journal_mode=WAL` returned `wal` and created the `-wal` and `-shm` side files. That is the mechanism that lets readers keep going during a write while still allowing only one writer.

**(d) PostgreSQL is a very different setup.** On the same machine, the Postgres server runs as background processes with shared memory:
```
$ pg_lsclusters
Ver Cluster Port Status  Owner    Data directory
18  main    5433 online  postgres /var/lib/postgresql/18/main
```
A real multi-table join on a 500k-row dataset used parallel workers and hash joins, which only make sense for a server expecting big, concurrent workloads. SQLite has no parallel workers because it runs inside your single-threaded query call.

Observation: the same SQL (`SELECT … WHERE email = ?`) is served by two completely different machines. SQLite serves it with an in-process bytecode VM reading a local file, and PostgreSQL serves it with a dedicated backend process talking to shared memory and a WAL. The SQL hides the architecture, but the architecture is the whole story.

---

## 6. Key Learnings

1. **One decision drives everything.** Server vs. library decides the process model, storage layout, concurrency model, and use cases. PostgreSQL is a service, SQLite is a file format with SQL.
2. **Why SQLite is great on mobile:** no server to run, no admin, tiny footprint, one file you can copy/back up/ship, and ACID transactions for free. A phone app has exactly one process touching its data, so SQLite's single-writer model is a perfect fit, not a limitation.
3. **Why PostgreSQL wins for large multi-user systems:** MVCC lets many users read and write at once without blocking each other, and the client-server model centralizes data, enforces access control, allows remote clients, and supports replication and parallelism. A single shared file cannot offer those.
4. **Concurrency is the sharpest trade-off.** PostgreSQL has many writers via MVCC (at the cost of dead-tuple cleanup and VACUUM). SQLite has one writer via file locks (at the cost of write throughput under contention, but with much more simplicity).
5. **Surprising fact:** the world's most-deployed database is not the "big" one. SQLite's 1-trillion-plus deployments come precisely from being small and serverless. It competes with `fopen()`, not with PostgreSQL, so the two are not really rivals. They sit in different niches.

---

## References

- PostgreSQL docs: [Architectural Fundamentals / Glossary](https://www.postgresql.org/docs/current/glossary.html), [Database File Layout](https://www.postgresql.org/docs/current/storage-file-layout.html), [MVCC](https://www.postgresql.org/docs/current/mvcc.html)
- SQLite docs: [Architecture](https://www.sqlite.org/arch.html), [When To Use SQLite](https://www.sqlite.org/whentouse.html) ("competes with fopen()"), [File Format](https://www.sqlite.org/fileformat.html), [Locking v3](https://www.sqlite.org/lockingv3.html), [WAL mode](https://www.sqlite.org/wal.html), [Most Deployed](https://www.sqlite.org/mostdeployed.html), [Amalgamation](https://www.sqlite.org/amalgamation.html)
- Experiments run on PostgreSQL 18.3 and SQLite 3.45.1 (this repository's `students.db`).
