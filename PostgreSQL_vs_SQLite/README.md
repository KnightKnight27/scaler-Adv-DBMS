# PostgreSQL vs SQLite — A Tale of Two Architectures

Both are relational, both speak SQL, both have been around for decades. Yet they sit at
opposite ends of the design spectrum. The interesting part isn't *that* they differ — it's
that almost every difference traces back to a single early decision: **is the database a
separate server, or is it just a library you link into your program?** Everything else
follows from that.

---

## 1. Problem Background

**SQLite** was written by D. Richard Hipp in 2000, originally for a system running on a
guided-missile destroyer where there was no guarantee a database server would even be
running. The goal was a database that needed *zero administration* and lived entirely inside
the application. No daemon, no config, no network — just a C library and a file on disk.

**PostgreSQL** descends from the POSTGRES project at Berkeley (1986), which set out to build
a serious, extensible, multi-user RDBMS. The assumption was the opposite of SQLite's: many
clients, possibly on different machines, all hammering the same data at once, with strong
guarantees about correctness and durability.

So they were never really competitors. They were answers to different questions.

---

## 2. Architecture Overview

### SQLite — embedded, in-process

```
   +-------------------------------------+
   |        Your application process     |
   |                                     |
   |   app code  --->  SQLite library    |
   |                        |            |
   |                   (function calls)  |
   +------------------------|------------+
                            |
                            v
                   +-----------------+
                   |  database.db    |   <- one file, plus a -wal / -journal
                   +-----------------+
```

There is no SQLite "server." When you call `sqlite3_step()`, you are running SQLite's B-tree
and pager code *on your own thread*, reading and writing a single file through normal
filesystem calls. Concurrency between processes is coordinated with file locks.

### PostgreSQL — client/server, process-per-connection

```
   client ---TCP/socket---\
   client ---TCP/socket----\        postmaster (listener)
   client ---TCP/socket-----+----->  forks a backend per connection
                            /              |
                           /     backend  backend  backend ...
                          /          \      |      /
                         /            \     |     /
                  +-------------------------------------+
                  |   Shared memory: shared_buffers,    |
                  |   WAL buffers, lock tables          |
                  +-------------------------------------+
                          |                 |
                     bgwriter          checkpointer, WAL writer,
                                        autovacuum (helper processes)
                          |
                          v
                  data files + WAL on disk
```

A `postmaster` listens for connections and forks a dedicated backend process for each one.
Backends coordinate through a big shared-memory segment (the buffer pool, lock tables, WAL
buffers) and a set of background helper processes do the housekeeping.

---

## 3. Internal Design

| Concern | SQLite | PostgreSQL |
|---|---|---|
| Process model | In-process library | Multi-process, one backend per connection |
| Storage unit | Single file, default 4 KB pages | Heap files per table, 8 KB pages |
| Table layout | B-tree keyed by `rowid` (or `WITHOUT ROWID`) | Heap (unordered) + separate index files |
| Index → row link | Index B-tree → rowid → table B-tree | Index → `ctid` (page, offset) into heap |
| Concurrency | One writer at a time (file lock); WAL mode allows readers + 1 writer | True MVCC, many concurrent writers |
| Durability | Rollback journal or WAL | WAL + periodic checkpoints |
| Types | Dynamic typing (type affinity) | Strict, rich static type system |

**Storage.** SQLite keeps *everything* — every table, every index, the schema itself — in
one file, organized as a forest of B-trees with a shared free-page list. PostgreSQL splits
things up: table rows live in an unordered **heap**, and indexes are separate structures
whose leaves point back into the heap by physical location (`ctid`). This is why a Postgres
index lookup is two steps (find the tuple pointer, then fetch the heap page), while SQLite's
rowid tables *are* the B-tree.

**Concurrency** is the sharpest contrast. SQLite's classic model is a single database-level
write lock — fine when one app owns the file, painful under many concurrent writers. WAL mode
softens this by letting readers proceed against the last committed snapshot while one writer
appends. PostgreSQL went all-in on **MVCC**: every row carries `xmin`/`xmax` transaction
stamps, writers create new versions instead of blocking readers, and readers never block
writers. That machinery is overkill for a phone app but essential for a 500-connection
server.

**Durability.** SQLite's default rollback journal writes the *original* pages aside before
modifying the file, so a crash can roll back. WAL mode flips it: new pages go to a WAL file
and are later checkpointed back. PostgreSQL uses write-ahead logging for the whole cluster —
changes hit the WAL and are fsync'd before commit returns; dirty data pages are flushed
lazily at checkpoints.

---

## 4. Design Trade-Offs

**SQLite trades concurrency for simplicity.** No server means no setup, no IPC, no network
round-trip, a tiny footprint (~1 MB), and a database you can copy with `cp`. The cost is that
heavy multi-writer concurrency just isn't its game — the whole-file write lock is the price
of that simplicity.

**PostgreSQL trades simplicity for scale and correctness.** The process-per-connection model
and MVCC let hundreds of clients write concurrently with strong isolation, and the extension
system (custom types, index access methods, FDWs) makes it enormously flexible. The cost is
operational weight: a server to run and tune, connection overhead (each backend is a real
OS process — hence the popularity of poolers like PgBouncer), and MVCC's bloat, which is why
`VACUUM` exists.

The honest summary: **SQLite optimizes for the single-user, zero-admin case; PostgreSQL
optimizes for the concurrent, multi-user case.** Picking the "wrong" one usually means you
misjudged which case you were in.

---

## 5. Experiments / Observations

A couple of things you can see for yourself:

- **`.dump` vs raw file.** Open any SQLite DB and run `PRAGMA page_size;` and
  `PRAGMA page_count;` — multiply them and you get the file size on disk. The whole database
  really is just that page array. Then enable WAL with `PRAGMA journal_mode=WAL;` and watch a
  `-wal` sidecar file appear; concurrent readers now stop blocking on a writer.

- **Connection cost in Postgres.** `SELECT count(*) FROM pg_stat_activity;` shows one row per
  backend process. Spin up 200 connections and you'll see 200 processes in `ps` — a concrete
  reminder of why connection pooling matters and why SQLite, with zero connection overhead,
  feels instant for local access.

- **Index indirection.** In Postgres, `SELECT ctid, * FROM t LIMIT 5;` exposes the physical
  `(page, offset)` tuple address an index would point to. Update a row and the `ctid`
  changes — proof that an update writes a *new* tuple version rather than editing in place.

---

## 6. Key Learnings

- The embedded-vs-server choice is the root from which everything else grows. Storage layout,
  locking, durability, even the type system are downstream of it.
- **SQLite is great on mobile** precisely because there's nothing to run: no daemon to keep
  alive, no socket, low memory, and a single file that's trivial to back up or ship inside an
  app. For one-user-at-a-time access it's hard to beat.
- **PostgreSQL wins for large multi-user systems** because MVCC and the shared-memory,
  multi-process model were built for exactly that — concurrent writers without readers
  blocking, plus the extensibility to grow into new workloads.
- "Which database is better?" is the wrong question. The real one is *how many writers, and
  who runs the thing?* — and the two designs answer that question in opposite directions on
  purpose.
