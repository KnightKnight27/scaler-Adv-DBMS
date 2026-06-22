# PostgreSQL vs SQLite: Comparing Two Database Architectures

> Advanced DBMS · System Design Discussion · Topic 1

Both of these are fully ACID, both speak SQL, and both store relations as tables of rows. But
they were built for almost opposite situations. PostgreSQL is a client-server *system*. SQLite is
an embedded *library*. The cleanest way I found to understand either one is to read them next to
each other and watch a single question, "where does the database actually run?", decide the
process model, the locking, the on-disk format, and the workloads each one is good at.

---

## 1. Problem Background

"RDBMS" covers a huge range of situations, and the right design depends entirely on which one
you're building for. Two concrete examples make the split obvious.

- **A multi-tenant web app.** Hundreds of app servers, thousands of concurrent connections, lots
  of users reading and writing the same tables at once. The data has to survive crashes, be
  reachable over a network, and be guarded by per-user permissions. Operators want to tune,
  replicate, and scale the database on its own, separate from the application.

- **A mobile app, a browser, or an IoT sensor.** Here the database lives *inside* the application
  binary. No DBA, no network, no server to install. The whole dataset is one file on local disk.
  What matters is a tiny footprint, no configuration, and staying out of the developer's way.

PostgreSQL was built for the first case, SQLite for the second. Neither is "better". They sit at
different points on the same trade-off: concurrency, manageability, and scale on one side;
simplicity, small footprint, and zero administration on the other. The goal here is to follow how
that one positioning choice works its way through the whole architecture, so the trade-offs read
as consequences rather than arbitrary differences.

The question this document keeps coming back to: both are ACID relational engines, so why do
their internals diverge so far, and what does each design buy and cost?

---

## 2. Architecture Overview

The most important difference is where the engine runs relative to the application.

### 2.1 SQLite — an embedded library

SQLite isn't a server. It's a C library the application links against, statically or dynamically.
Call `sqlite3_open()` and there's no separate process, no socket, no daemon. The parser, planner,
virtual machine, B-tree layer, and pager all run inside the application's address space, on the
application's threads. A "connection" is just an in-process object. A SQL statement ends up as an
ordinary function call that bottoms out in a `read()`/`write()` against one file on local disk.

That's why people describe SQLite as a replacement for `fopen()` rather than a replacement for
Oracle. It competes with reading and writing application files, not with running a database
service.

### 2.2 PostgreSQL — a client-server system

PostgreSQL runs as a set of cooperating OS processes that outlive any one client. Applications
connect over a socket (TCP or Unix domain) using a wire protocol. The engine, the data, and the
clients are decoupled. Clients can sit on other machines, be written in any language, and the
server keeps running between connections.

### 2.3 The two models side by side

```
        SQLITE (embedded, in-process)                 POSTGRESQL (client-server)

   +---------------------------------+         +-------------+   +-------------+
   |        Application Process      |         |   Client A  |   |   Client B  |
   |                                 |         | (libpq/JDBC)|   | (libpq/...) |
   |   App code                      |         +------+------+   +------+------+
   |      |                          |                |   TCP / Unix socket
   |      v                          |                v                v
   |  +--------------------------+   |         +======================================+
   |  |  SQLite library          |   |         |  POSTMASTER (supervisor, listens)    |
   |  |  parser -> planner ->     |   |         |     forks one backend per connection |
   |  |  VDBE -> B-tree -> pager  |   |         +======================================+
   |  +-----------+--------------+   |              |  fork()        |  fork()
   |              |                  |              v                v
   +--------------|------------------+         +----------+     +----------+
                  |  read()/write()             | Backend  |     | Backend  |
                  v                              | process  |     | process  |
        +--------------------+                   +----+-----+     +----+-----+
        |  one .db file      |                        |               |
        |  (on local disk)   |                        v               v
        +--------------------+                  +================================+
                                                |  SHARED MEMORY                 |
                                                |  shared_buffers, WAL buffers,  |
                                                |  lock table, proc array        |
                                                +================+===============+
                                                                 |
                                       +----------------+--------+--------+-----------------+
                                       v                v                 v                 v
                                  +---------+      +---------+       +-----------+    +-------------+
                                  | bgwriter|      |  WAL    |       | autovacuum|    | checkpointer|
                                  +---------+      | writer  |       +-----------+    +-------------+
                                                   +---------+
                                                          |
                                                          v
                                            +-------------------------------+
                                            |  Cluster data directory       |
                                            |  (many files: base/, pg_wal/) |
                                            +-------------------------------+
```

### 2.4 PostgreSQL's process model in detail

PostgreSQL uses a process per connection, not threads.

- The **postmaster** is the supervisor. It binds the listening socket, owns shared memory, and
  starts and monitors every other process. It doesn't run SQL itself.
- For each new connection the postmaster `fork()`s a dedicated **backend process** that handles
  all of that client's queries for the life of the session. Because backends are separate
  processes, a crash in one can be contained: the postmaster resets shared memory instead of
  letting corruption spread quietly.
- Backends coordinate through one region of **shared memory** created at startup. The two biggest
  pieces are `shared_buffers` (the shared page cache of 8KB data pages, so one backend's read
  helps all the others) and the WAL buffers (where write-ahead log records are staged before
  being flushed). Shared memory also holds the lock table and the process array used for MVCC
  visibility.
- A set of **background processes** handles work no single backend should own. The WAL writer
  flushes WAL, the checkpointer periodically writes dirty buffers out and records a consistent
  point, the background writer trickles dirty pages to disk to smooth I/O, and autovacuum workers
  reclaim space (more on that later).

This costs memory and a fork per connection, which is exactly why production deployments usually
put a connection pooler like PgBouncer in front of PostgreSQL. In return it gets strong isolation
between sessions and lets the OS scheduler spread work across cores on its own.

### 2.5 SQLite's (non-)process model

SQLite has none of that. No postmaster, no backend, no shared memory segment, no IPC, no
background maintenance threads. When different processes open the same file, they coordinate
through filesystem locks on the database file (plus, in WAL mode, a small shared-memory index
file). All the work runs on the caller's thread. Having no server is the whole point: nothing to
install, nothing to start, nothing to administer.

---

## 3. Internal Design

### 3.1 File organization

This is where the embedded-vs-server split turns physical.

**SQLite keeps everything in one file.** A SQLite database is a single file. Inside it, every
table and every index is a B-tree, and all those B-trees are interleaved across the file's pages.
Page 1 doubles as the schema: it starts with a 100-byte header and then holds the
`sqlite_schema` table, which records the `CREATE` statements for everything else. The default
page size is 4 KB, and the file grows a page at a time. The single-file format is deliberate. A
database is trivial to copy, back up, and move between machines and operating systems (the format
is endian-independent), because there's exactly one thing to move.

**PostgreSQL is a directory full of files.** A PostgreSQL *cluster* is a data directory
(`PGDATA`). Each database is a subdirectory under `base/`, and inside it every relation, table or
index, gets its own file named by its internal OID. Tables and indexes are physically separate
objects. A large relation is split into 1 GB segments (`<oid>`, `<oid>.1`, `<oid>.2`, …) so no
single file grows without bound. The page size is 8 KB. Alongside the heap, each table keeps a
few *fork* files: the main data fork, the Free Space Map (`_fsm`, tracks which pages have room so
inserts can find space without scanning), and the Visibility Map (`_vm`, marks pages where all
tuples are visible to everyone, which enables index-only scans and lets VACUUM skip work). The
write-ahead log lives separately under `pg_wal/`.

One immediate consequence: create a single table with one index and SQLite gives you one file
while PostgreSQL gives you several. We come back to this in the experiments.

### 3.2 Heap tables vs B-tree tables

The two engines make opposite choices about how table rows are physically arranged.

- **PostgreSQL uses heap tables.** Rows ("tuples") sit in an unordered heap, and a tuple's
  physical location is a `(block number, item offset)` pair called a **ctid**. Indexes are
  separate structures that point at heap tuples by ctid. Since the table itself isn't sorted by
  any key, there's no automatic clustering; a primary-key index is just another B-tree pointing
  back into the heap. Oversized field values get pushed out of line by **TOAST** (The
  Oversized-Attribute Storage Technique): a value too big for a page is compressed and/or split
  into chunks in an associated TOAST table, keeping the main heap tuple small.

- **SQLite stores tables *as* B-trees.** An ordinary table is a B-tree keyed by an integer
  **rowid**, with the row data living in that B-tree's leaf pages. So the table *is* its primary
  index, and the data is clustered by rowid. A `WITHOUT ROWID` table clusters directly on the
  declared primary key instead. Big values aren't toasted; a cell that doesn't fit on a page
  **overflows** onto a chain of overflow pages.

### 3.3 Page layout

Both engines read and write fixed-size pages, but the internal layout follows from their storage
models.

**PostgreSQL heap page (8 KB).** A heap page uses a slotted-page layout. A small header sits at
the start, then an array of **line pointers** (`ItemId`s, 4-byte slots that say "tuple X is at
offset Y, length Z"). The tuples themselves are written from the *end* of the page backward, with
the free space as the gap in the middle. That indirection through line pointers is what keeps
ctids stable: a tuple can move within a page, or be marked dead, by editing its line pointer
without rewriting every index.

```
PostgreSQL 8KB heap page (slotted layout)
+-----------------------------------------------------------+ offset 0
| PageHeader (24 bytes: LSN, checksum, free-space pointers) |
+-----------------------------------------------------------+
| ItemId[0] ItemId[1] ItemId[2] ...   (line pointer array)  |  grows ->
+-----------------------------------------------------------+
|                                                           |
|                  free space (the "hole")                  |
|                                                           |
+-----------------------------------------------------------+
| ... tuple 2 | tuple 1 | tuple 0 |                          |  <- grows
+-----------------------------------------------------------+ offset 8191
   ^ tuples are appended from the END of the page backward
```

**SQLite B-tree page (4 KB).** A page has a header, then a **cell pointer array** (2-byte
offsets, kept in key order), then the **cells** growing from the end, with free space in between.
Structurally it's a lot like PostgreSQL's slotted page, except the ordering of the pointer array
is what keeps the B-tree sorted. SQLite separates **interior** pages (cells hold a child-page
pointer and a key, forming the navigation structure) from **leaf** pages (cells hold the key and
payload). A *table* B-tree leaf holds the row data; an *index* B-tree leaf holds index keys plus
the rowid that points back into the table B-tree.

```
SQLite 4KB B-tree leaf page
+-------------------------------------------------+
| page header (8 or 12 bytes)                     |
+-------------------------------------------------+
| cell-pointer array (2-byte offsets, key-sorted) |  grows ->
+-------------------------------------------------+
|              free space                         |
+-------------------------------------------------+
| ... cell 2 | cell 1 | cell 0 |   (payload+key)  |  <- grows
+-------------------------------------------------+
```

### 3.4 Indexes

Both default to **B-tree** indexes, and in both an index is a balanced search tree mapping key
values to row locations. The difference is physical packaging and breadth.

- In **SQLite**, an index B-tree lives in the same file as everything else and points at table
  rows by rowid. The realistic choice is "B-tree or nothing" (R-trees come as an optional
  extension, and there's FTS for full text), which fits the goal of a small, predictable engine.

- In **PostgreSQL**, each index is its own file, and the engine ships a whole family of access
  methods matched to data shapes that show up in large systems: B-tree for ordered and equality
  lookups; GIN (an inverted index) for multi-valued data like full text, JSONB, and arrays; GiST
  for geometric, nearest-neighbor, and range data; BRIN (Block Range Index) for huge,
  naturally-ordered tables where a tiny per-block-range summary is enough; and Hash for
  equality-only lookups. That breadth is part of PostgreSQL's extensibility story, and it feeds a
  cost-based optimizer that can choose among them.

### 3.5 Transactions, concurrency, and durability

This is the deepest split, and it falls straight out of "one process" vs "many processes".

**PostgreSQL: MVCC.** PostgreSQL uses Multi-Version Concurrency Control. Rather than updating a
row in place, an `UPDATE` writes a *new* version of the tuple and marks the old one expired; a
`DELETE` just marks the old version expired. Every tuple carries two hidden system columns,
`xmin` (the transaction id that created this version) and `xmax` (the one that expired it). When
a transaction reads, it compares a tuple's `xmin`/`xmax` against its **snapshot** to decide which
single version it can see. The headline result is that readers don't block writers and writers
don't block readers. A long report runs against a consistent snapshot while writes go on creating
new versions. The price is **bloat**: expired tuples pile up as dead rows, so **VACUUM** (run
automatically by autovacuum) has to reclaim the space and head off transaction-id wraparound.
Writer-vs-writer conflicts on the same row still serialize on a row lock, but that's far narrower
than a table-level lock.

**SQLite: file-level locking.** SQLite keeps no multiple row versions. It manages concurrency at
the granularity of the whole database file, using OS file locks. There are two modes:

- **Rollback journal (the default, historical mode).** Before changing pages, SQLite copies the
  original pages into a separate `-journal` file, then writes the changes in place. On commit it
  `fsync`s and deletes the journal; on a crash or rollback it copies the journal pages back to
  restore the prior state. While a write commits, the writer holds an EXCLUSIVE lock, so no
  readers can proceed during that window.

- **WAL mode.** Changes are appended to a separate `-wal` file instead of being written in place,
  and the original database pages stay untouched until a periodic **checkpoint** folds the WAL
  back into the main file. The gain is that readers and one writer can run at the same time:
  readers see a consistent point in the main file plus WAL frames up to their snapshot, while the
  single writer appends. Even so, SQLite still allows only one writer at a time for the whole
  database.

So SQLite is single-writer, multiple-reader, full stop. PostgreSQL allows many writers as long as
they touch different rows. That one fact is the most consequential difference in this whole
comparison.

**Durability.** Both are ACID, and both lean on write-ahead logging plus `fsync`. A transaction
is durable only once its log record is forced to stable storage *before* the commit is
acknowledged, so after a crash the engine can redo committed work and discard uncommitted work.
PostgreSQL uses its central WAL under `pg_wal/`, and crash recovery replays from the last
checkpoint. SQLite reaches the same guarantee with either the rollback journal (undo style: keep
the old pages so you can roll back) or WAL mode (redo style: keep the new pages so you can roll
forward), with the `synchronous` PRAGMA controlling how aggressively it `fsync`s.

---

## 4. Design Trade-Offs

The table sums up the dimensions above. The prose after it covers the reasoning the table can
only gesture at.

| Dimension | PostgreSQL | SQLite |
|---|---|---|
| Deployment model | Client-server (separate process) | Embedded library (in-process) |
| Process model | postmaster + per-connection backends + background procs | None — runs on caller's thread |
| IPC / coordination | Shared memory (`shared_buffers`, WAL buffers, lock table) | OS file locks (+ shared-memory index in WAL mode) |
| Storage unit | One file *per relation*, in a cluster dir; 8 KB pages | One file for the *whole* DB; 4 KB pages |
| Table storage | Heap (unordered tuples, ctid); TOAST for big values | B-tree keyed by rowid; overflow pages for big values |
| Index types | B-tree, GIN, GiST, BRIN, Hash (each its own file) | B-tree (same file); R-tree/FTS via extensions |
| Concurrency | MVCC — many writers, readers never block writers | File-level locks — single writer, many readers |
| Reclaiming space | VACUUM / autovacuum (because MVCC leaves dead tuples) | Not needed for MVCC; `VACUUM` only defragments |
| Durability | Central WAL + fsync + checkpoints | Rollback journal *or* WAL file + fsync |
| Network access | Yes (TCP / Unix socket, wire protocol) | No — local file only |
| Admin / config | Roles, permissions, tuning, replication; needs a DBA | Zero-config, no server to run, no users |
| Footprint | Heavyweight server install | ~single small library, no dependencies |
| Sweet spot | Large multi-user, concurrent, networked systems | Mobile, embedded, edge, app-local storage |

### 4.1 Why SQLite fits mobile / embedded / edge

Every SQLite decision is about staying out of the way. Zero configuration means an app on a phone
or a sensor can ship a database with no installer, no service, and no DBA; there's nothing to set
up. The single-file format means backup is `cp`, deployment is bundling one asset, and the data
moves across architectures unchanged. The in-process model means a query is a function call with
no socket round-trip, so for the local, mostly-single-user access pattern of an app it's very
fast and very small. The cost it accepts, single-writer concurrency and no network access, simply
doesn't bite when the database serves one local application. SQLite works *with* its constraints,
and that's why it ships in basically every smartphone, browser, and operating system.

### 4.2 Why PostgreSQL fits large multi-user systems

PostgreSQL pays real costs: a server to run, a process per connection, VACUUM to manage,
configuration to tune. In exchange it gets capabilities those costs make possible. MVCC lets
thousands of users read and write at once without one long query freezing the system, which is
the defining need of a busy web backend. Network access plus a documented wire protocol lets many
app servers in many languages share one database. Roles and permissions give multi-tenant systems
the security model they need. Extensibility (custom types, index access methods, procedural
languages, extensions like PostGIS) and a cost-based planner let it handle complicated analytical
and operational queries at scale. None of that is free, and none of it would make sense bolted
onto a library meant to live inside a phone app. That's the reason these are two separate tools
and not one tool with a config flag.

### 4.3 The architectural decisions that cause the differences

If you trace the differences back, almost all of them come from one root choice and a second that
follows it:

1. **Where the engine runs.** In-process library vs separate server decides the process model,
   the IPC mechanism (filesystem locks vs shared memory), whether there's network access, and how
   much administration exists at all.
2. **How concurrency is handled.** Multi-version (PostgreSQL) vs single-file locking (SQLite)
   decides whether many writers can run at once, whether there are dead versions to clean up
   (VACUUM), and how durability is logged. SQLite could only really afford file-level locking
   *because* it's embedded and usually single-user; PostgreSQL needed MVCC *because* it serves
   many concurrent clients. The second choice is downstream of the first.

Everything else (heap vs B-tree tables, one file vs many, a rich index catalog vs B-tree-only) is
a consistent elaboration of those two.

---

## 5. Experiments / Observations

These are observations a student can reproduce. They make the architecture concrete.

### 5.1 Concurrent writers

Open two clients and have both try to write at the same time.

- **SQLite.** With two processes writing the same file (especially in rollback-journal mode), the
  second writer often fails with the classic error:

  ```
  Error: database is locked   (SQLITE_BUSY)
  ```

  That's not a bug. It's the single-writer model showing through. Switching to WAL mode
  (`PRAGMA journal_mode=WAL;`) lets readers keep going during a write, but two *writers* still
  serialize; one waits or times out (`busy_timeout`).

- **PostgreSQL.** Two transactions updating *different* rows both commit with no waiting, because
  MVCC lets them work on independent tuple versions. Two transactions updating the *same* row
  serialize on a row lock: the second just blocks until the first commits, then proceeds, instead
  of erroring out.

The "database is locked" experience versus transparent concurrency is the most visible sign of
file-level locking versus MVCC.

### 5.2 What an UPDATE physically does in PostgreSQL

Run:

```sql
CREATE TABLE t (id int PRIMARY KEY, v text);
INSERT INTO t VALUES (1, 'a');
SELECT ctid, xmin, xmax, * FROM t;     -- note the ctid, e.g. (0,1)
UPDATE t SET v = 'b' WHERE id = 1;
SELECT ctid, xmin, xmax, * FROM t;     -- ctid has changed, e.g. (0,2); new xmin
```

The `ctid` changes after the update because PostgreSQL didn't edit the row in place. It wrote a
new tuple version (new physical location, new `xmin`) and marked the old one expired. The old
version still takes up space until VACUUM reclaims it. Update the same row over and over and watch
the table size grow before VACUUM runs, and you've seen MVCC bloat firsthand.

### 5.3 File-count and on-disk differences

Create one table with one index in each engine and look at the filesystem.

- **SQLite:** still essentially one file (`app.db`), plus a transient `-journal` or `-wal`
  companion only while a transaction is active. The table and its index share the file.
- **PostgreSQL:** the cluster's `base/<db_oid>/` directory has separate files for the table, its
  index, and the table's `_fsm`/`_vm` forks, each named by OID. You can map a name to its file
  with:

  ```sql
  SELECT pg_relation_filepath('t');
  ```

  Watching several files show up for one logical table makes the heap/index/fork separation
  tangible.

### 5.4 Footprint and startup

Starting PostgreSQL means `initdb` to build a cluster, then launching the postmaster and its
background processes. You can see them with `ps` (postmaster, checkpointer, WAL writer, autovacuum
launcher, and so on). "Starting" SQLite means opening a file. There are no processes to list, and
that absence is the observation: no server shows up precisely because there's nothing to start.

### 5.5 Read latency for a single local lookup

For a trivial single-row primary-key lookup from a local application, SQLite is usually faster
end to end, because there's no connection, no socket round-trip, and no process boundary. The
query is a function call into a B-tree already mapped into the process. PostgreSQL's real
strengths (concurrency, planning, scale) don't show up in this micro-benchmark, which is exactly
why a benchmark like this would be a misleading way to pick between them.

---

## 6. Key Learnings

1. **One requirement shapes everything.** Where the engine runs, in-process library vs separate
   server, is the root cause of nearly every other difference: the process model, the IPC, the
   file layout, the locking strategy, even the index catalog. A lot of "architecture" turns out
   to be the disciplined follow-through on a single founding constraint.

2. **Concurrency is the defining trade-off.** PostgreSQL's MVCC (readers never block writers, many
   concurrent writers) is what makes it work for busy multi-user systems, and it pays for that
   with dead-tuple bloat and VACUUM. SQLite's file-level locking gives single-writer,
   multi-reader simplicity with no garbage to collect, and it pays with the "database is locked"
   wall under concurrent writers. Each accepted a cost that's invisible in its target
   environment.

3. **Storage layout encodes the philosophy.** Heap tables, separate index/fork files, and TOAST
   describe a system meant to be tuned, extended, and operated at scale. A single file where
   tables *are* B-trees describes a library meant to be copied, embedded, and forgotten. Neither
   layout is more correct; each is correct for its goal.

4. **Durability is one idea with two implementations.** Both reach ACID through write-ahead
   logging and `fsync`, but PostgreSQL centralizes it in one WAL while SQLite offers an undo-style
   rollback journal and a redo-style WAL mode. Same principle, never acknowledge a commit until
   the log is on stable storage, different mechanisms under different constraints.

5. **"Better" means nothing without context.** SQLite is the most deployed database on Earth and
   PostgreSQL is a default choice for serious backends, and both are true *because* they target
   different worlds. The engineering lesson is to pick the tool whose accepted trade-offs are
   invisible in your environment, not the one with the longest feature list.

---

## References

1. **PostgreSQL Documentation — "Internals" and "Server Administration"**, The PostgreSQL Global
   Development Group. <https://www.postgresql.org/docs/current/> — especially the chapters on
   Database Physical Storage, MVCC / Concurrency Control, WAL, and Index Types.
2. **SQLite Documentation — "The SQLite Database File Format"**, SQLite Consortium.
   <https://www.sqlite.org/fileformat2.html> — the authoritative description of the single-file
   layout, page structure, and B-tree organization.
3. **SQLite Documentation — "Write-Ahead Logging"** and **"File Locking And Concurrency"**.
   <https://www.sqlite.org/wal.html> and <https://www.sqlite.org/lockingv3.html>.
4. **SQLite — "Appropriate Uses For SQLite"**, on the embedded/edge design intent.
   <https://www.sqlite.org/whentouse.html>.
5. **Hironobu Suzuki, "The Internals of PostgreSQL"**, <https://www.interdb.jp/pg/> — a strong
   free book on the process architecture, heap tuple/page layout, MVCC, VACUUM, and WAL.
