# PostgreSQL vs SQLite — Architecture Comparison

This is my write-up for Topic 1 of the system design discussion. I picked
this one first because I'd already used both databases in the earlier
labs in this course (Lab 2 ran benchmarks on them, Lab 4 actually
opened a SQLite file with `xxd` and looked at the bytes), so I had
something to compare against the official documentation as I read.

## 1. Problem Background

PostgreSQL and SQLite both speak SQL and both store data in pages on
disk, but the two systems were built to solve very different problems.

SQLite was originally written for a Navy program where running a
separate database server alongside the application wasn't an option.
The whole point was "act like a relational database, but be a library,
not a daemon." You open a file, read and write rows, close the file.
No daemon, no port number, no DBA.

PostgreSQL came from the older Postgres / Ingres research at Berkeley
(Stonebraker's lab). The goal there was the classic multi-user RDBMS —
hundreds of users hitting the same database at once, with strict ACID
guarantees, replication, roles, the whole package. So from the start
it was a server process you connect to over a socket.

So we end up with two systems that pass the same SQL conformance
tests but make almost the opposite choices everywhere else.

| | SQLite | PostgreSQL |
|---|---|---|
| Where it lives | Inside your app (linked as a library) | Standalone server process |
| Default page size | 4 KB | 8 KB |
| Concurrent writers | 1 at a time | Many |
| Network access | None — local file only | TCP, default port 5432 |
| Admin | None (the file is the database) | Roles, GRANTs, replication, etc. |
| Compiled size | ~700 KB | ~50 MB |

## 2. Architecture Overview

### SQLite

When you `#include <sqlite3.h>` and call `sqlite3_open("file.db")`,
the whole engine — parser, planner, B-tree code, pager, OS file
abstraction — runs inside your own process. There is no other
process. A query is a function call.

```
+--------------------------------------+
|         my application               |
|                                      |
|   app code  -->  sqlite3 library     |
|                       |              |
|                       v              |
|        B-tree + pager + VFS          |
+--------------------|-----------------+
                     |
                     v
               students.db   (one file)
               students.db-journal / -wal
```

### PostgreSQL

PostgreSQL is a server that runs on its own. When the server boots,
the `postmaster` process listens on a port. When a client connects,
postmaster `fork()`s a new backend process for that connection. So if
50 clients are connected, there are 50 backend processes plus a
handful of helper processes, all sharing one chunk of memory.

```
clients  -->  postmaster (port 5432)
                  |  fork() per connection
                  v
              backend1   backend2   backend3 ... ---+
                                                    |
                                            shared memory
                                            (buffer pool, locks, WAL)
                                                    ^
                                                    |
              bgwriter    checkpointer    wal-writer    autovacuum
                  |             |              |              |
                  v             v              v              v
              base/<db>/    pg_xact/         pg_wal/       (vacuum work)
              (heap files)
```

The helper processes are:

- **bgwriter** — writes dirty pages out to disk in the background so
  backends don't stall on cache evictions.
- **checkpointer** — every few minutes, flushes everything and lets the
  WAL be recycled.
- **wal writer** — actually fsyncs the WAL.
- **autovacuum** — runs `VACUUM` on tables that have built up too many
  dead tuples (because MVCC leaves old versions behind).

So just visually you can see SQLite is "one box" and PostgreSQL is "a
bunch of cooperating boxes." Most of the differences below come from
that one fact.

## 3. Internal Design

### Process model

SQLite has no process model. If my app uses two threads to talk to
SQLite, SQLite locks the database file (or WAL frames) and serialises
writes. In rollback-journal mode only one writer can hold the write
lock. In WAL mode one writer can run concurrently with many readers.

PostgreSQL gives one process per connection. The benefit is hard
isolation — if one backend crashes, the others don't go down with it,
and there's no shared mutable memory inside the backend itself. The
cost is the `fork()` per new connection. That's why most real PG
deployments sit behind something like PgBouncer that pools connections.

### Storage layout

Both use **slotted pages**: each fixed-size page has a small header,
then an array of pointers at the top of the page, then free space,
then variable-length records growing upward from the bottom of the
page.

The big difference is what goes in those pages:

- **SQLite** puts the whole database in one file. The schema catalog
  (the `sqlite_master` table) lives in the same file as the user
  tables. In Lab 4 I dumped `students.db` with `xxd` and could
  actually see the `CREATE TABLE students(...)` string sitting in
  page 1 of the file. The records I inserted (Alice, Bob, etc.) were
  packed near the bottom of page 2.

- **PostgreSQL** uses one file per relation (heap or index), broken into
  1 GB segments. The schema catalog (`pg_class`, `pg_attribute`,
  etc.) is itself a set of tables — same on-disk format as user
  tables, just in a different OID.

### B-trees and indexes

Both use B-trees on disk. The split-on-the-way-down algorithm I wrote
in Lab 4's `b_tree.cpp` is the same idea both engines use.

The difference is in how rows relate to indexes:

- **SQLite** stores the row data *inside* the B-tree, keyed by `rowid`.
  If I declared `id INTEGER PRIMARY KEY`, then `id` *is* the rowid and
  the table B-tree is keyed on it directly. Looking up by primary key
  is one B-tree traversal — no extra hop.

- **PostgreSQL** stores rows in an unordered heap. Every index,
  including the primary key, is a separate B-tree whose leaf entries
  hold `(blockno, offset)` pointers into the heap. So `SELECT * FROM t
  WHERE id = 42` does two lookups: descend the primary key B-tree to
  get the heap pointer, then read the heap page.

(PG has no "clustered" indexes the way MySQL/InnoDB does. I'll
probably come back to that in the InnoDB write-up.)

### Buffer manager

Both keep frequently-used pages in memory.

- SQLite has a small per-connection page cache, default 2000 pages.
- PostgreSQL has `shared_buffers`, a single buffer pool shared by every
  backend, default 128 MB. The replacement policy is **clock-sweep**.

Lab 3 in this course is literally a clock-sweep implementation. Each
frame has a reference bit; a hand walks the ring of frames; on insert,
when the cache is full, the hand advances clearing reference bits
until it finds one already at zero, and that frame is evicted. PG's
version uses a small `usage_count` (0–5) instead of a single bit, so
popular pages get more "chances" before eviction, but the structure
is the same.

### Concurrency

This is where the two systems diverge the most.

SQLite locks at the file level (rollback mode) or WAL frame level (WAL
mode). The simplest mental model: one writer at a time. Many readers
can run with that writer in WAL mode.

PostgreSQL uses **MVCC** at the row level. Every row carries two
transaction IDs: `xmin` (the txn that inserted it) and `xmax` (the
txn that deleted or updated it). An `UPDATE` doesn't overwrite the
row — it inserts a new version with `xmin = current_txn` and stamps
the old version's `xmax = current_txn`. A reader with snapshot `S`
sees a row only if `xmin` committed before `S` and `xmax` is either
zero, after `S`, or aborted.

In Lab 6 I wrote a stripped-down version of this — a transaction
manager that combines MVCC with Strict 2PL plus a waits-for graph for
deadlock detection. Scenario 1 of the demo is the same situation
PostgreSQL handles natively: T2 begins, T3 commits a new version
afterwards, but T2's read still sees the old value.

The trade-off is that MVCC leaves dead row versions behind. That's
what `VACUUM` is for — it's PG's garbage collector.

### Durability

Both rely on writing a log before modifying data pages (write-ahead
logging).

- **SQLite** writes either a rollback journal (the *old* version of any
  page it's about to modify) or, in WAL mode, the *new* page contents
  to a `.wal` file alongside the database.
- **PostgreSQL** writes WAL records to `pg_wal/`. Every modification —
  insert, update, delete, index update, even commit itself — is
  recorded there before the dirty page is flushed.

PostgreSQL's WAL is the foundation for replication, point-in-time
recovery, and logical decoding (CDC). SQLite's WAL is purely local —
it exists so that one writer doesn't block readers.

### Query planning

Both have cost-based planners.

- SQLite's optimiser is lightweight. It uses `ANALYZE` stats but the
  planner is intentionally small (it has to stay fast on embedded
  hardware).
- PostgreSQL has a heavier planner with per-column statistics in
  `pg_statistic` — most-common-value lists, histograms, n-distinct
  estimates. It picks between nested-loop, hash, and merge joins. The
  result is visible in `EXPLAIN ANALYZE`.

## 4. Design Trade-Offs

### Things SQLite gave up

- Concurrent writers — there's exactly one. If 20 threads try to write
  at once, 19 of them are waiting for a lock.
- Network access. If I want SQL over a socket I have to wrap SQLite in
  my own server (or use a different database).
- Replication, roles, extensions like PostGIS, stored procedures, etc.

In return:

- Zero administration. No daemon to start. Backup = `cp database.db`.
- Latency is just a function call.
- It runs literally everywhere — every browser, every mobile OS, every
  desktop.

### Things PostgreSQL gave up

- Embeddability. I can't link `libpq` into my app and *be* the database
  the way SQLite can.
- Single-file portability. A PG cluster is a directory with many files.
- Startup time. SQLite is "open the file." PG needs `initdb` once and
  a server start each time.

In return:

- Thousands of concurrent connections with isolation between them.
- MVCC means readers never block writers on the same row.
- An ecosystem (PostGIS, pgvector, TimescaleDB, replication tooling)
  that SQLite just doesn't have.

### Which to use

Roughly:

- Mobile / desktop / embedded → SQLite.
- One-user web app on my laptop → SQLite is fine.
- Multi-user, concurrent writes, real load → PostgreSQL.
- Anything with replication, geospatial, vector search, multi-tenant
  SaaS → PostgreSQL.

## 5. Experiments / Observations

### Lab 2 — running both side by side

In Lab 2 I loaded both engines with the same set of synthetic student
records (around 250k rows) and ran the same insert/select/aggregate
workloads through Python scripts. Looking at the outputs in
`Lab2/results/`:

- **Bulk inserts**: SQLite was faster on a single connection. There's
  no IPC, no network round-trip; each `INSERT` is just a function
  call.
- **Concurrent reads**: PostgreSQL got faster as I added more
  concurrent readers. SQLite serialises at the file/WAL level so
  adding threads didn't help much.
- **Aggregates over the full table**: PostgreSQL's planner picked a
  hash aggregate; SQLite did a sort + group. PG was faster on the
  bigger dataset.

The pattern matched the architecture: SQLite wins at low-concurrency
latency, PG wins at high-concurrency throughput.

### Lab 4 — looking at the SQLite file directly

In Lab 4 I ran `xxd` on `students.db` and could see, with my own
eyes, every architectural claim the docs make:

- Offset 0: `53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00` — the
  literal ASCII `SQLite format 3\0` magic.
- Offset 16: page size as a 2-byte big-endian integer (`0x1000` =
  4096).
- Offset 28: database size in pages (`0x00000002` = 2).
- Page 1 contains the file header *and* the schema catalog. Near the
  end of page 1 the bytes `43 52 45 41 54 45 20 54 41 42 4c 45` spell
  `CREATE TABLE` — the schema is literally embedded in the file as
  ASCII.
- Page 2 starts with `0x0D` (table B-tree leaf), then `0x000A` (ten
  cells), then ten 2-byte cell pointers, then a gap of zeros, then
  the ten records (Alice, Bob, ...) packed from the bottom up.

That kind of inspection is much harder with PostgreSQL — its tuple
format is more complex and most people use `pg_filedump` instead of
`xxd`. But the architectural idea (slotted pages, variable-length
records growing upward from the bottom) is the same in both.

## 6. Key Learnings

A few things stuck with me after writing this up:

- **Deployment shapes everything.** SQLite is a library because the
  original use case had no daemon. PostgreSQL is a server because the
  original use case had many users. Almost every other design
  difference (process model, concurrency control, file layout)
  follows from that one decision.

- **Concurrency control is the biggest divergence.** Coarse file
  locking is fine for one writer. The moment you have many concurrent
  writers, you need either MVCC, row-level locks, or both. PG paid
  the cost of MVCC (version chains + VACUUM); SQLite chose not to.

- **One file vs many files is a real trade-off.** Having the whole
  database in one file made my Lab 4 inspection possible and makes
  backups trivial. But it also means there's no way to parallelise
  I/O across tables the way PG can.

- **Each lab in this course is a simplified version of a real PG
  component.** Lab 3's `ClockSweep<T>` ≈ PG's buffer manager
  replacement policy. Lab 4's B-Tree ≈ what's inside `nbtree`. Lab 6's
  MVCC + 2PL ≈ a tiny version of PG's concurrency layer. Building
  these in isolation made it much easier to see what PG is actually
  doing under the hood.

- **The phrase "PostgreSQL is better than SQLite" or vice versa
  doesn't really mean anything.** They solve different problems. The
  honest answer is "depends on the workload."

## References

- SQLite file format: <https://www.sqlite.org/fileformat.html>
- PostgreSQL internals book (Hironobu Suzuki): <https://www.interdb.jp/pg/>
- PG source — buffer manager: `src/backend/storage/buffer/`
- PG source — `nbtree`: `src/backend/access/nbtree/`
- This repo: `Lab2/` (benchmarks), `Lab3/` (clock sweep), `Lab4/`
  (SQLite xxd), `Lab6/` (MVCC + 2PL).
