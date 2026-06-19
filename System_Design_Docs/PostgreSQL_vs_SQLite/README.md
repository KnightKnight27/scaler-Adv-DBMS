# PostgreSQL vs SQLite Architecture Comparison

A study of how two relational database engines, built for opposite ends of the
deployment spectrum, arrive at very different internal designs from the same SQL
foundation. The focus here is *why* each system is built the way it is, not a
feature checklist.

---

## 1. Problem Background

Both PostgreSQL and SQLite expose SQL and the relational model, but they were
created to solve problems that barely overlap.

**Why PostgreSQL exists.** PostgreSQL descends from the POSTGRES project at UC
Berkeley (mid-1980s), which set out to extend the relational model with richer
types, rules, and extensibility. The modern system targets the *server* role:
one database engine serving many concurrent clients over a network, holding data
that must survive crashes, and remaining correct when dozens or thousands of
transactions touch the same rows at once. Every major design choice — a
multi-process server, shared memory, write-ahead logging, multi-version
concurrency control — follows from the assumption that the database is a
long-running shared service that owns a machine (or a cluster).

**Why SQLite exists.** SQLite was written by D. Richard Hipp in 2000, originally
for a system that needed a database without a database *administrator* and
without a server process at all. Its goal is to be the database equivalent of
`fopen()`: an application calls a library function, the library reads and writes
an ordinary file, and there is no separate process to install, configure, or
keep running. SQLite is not competing with client-server databases; its stated
competitor is the ad-hoc file format. It is the most widely deployed database
engine in the world precisely because it disappears into the application.

**Historical motivation and divergent goals.** PostgreSQL grew up answering the
question *"how do we let many users share consistent data safely?"* SQLite grew
up answering *"how do we give one program reliable structured storage with zero
operational overhead?"* These questions push in opposite directions:

| Dimension            | PostgreSQL goal                          | SQLite goal                              |
|----------------------|------------------------------------------|------------------------------------------|
| Primary deployment   | Standalone networked server              | Linked into the host application         |
| Concurrency target   | Many writers, many readers               | One application, mostly one writer       |
| Administration       | Expects a DBA / ops process              | Zero administration                      |
| Footprint            | Hundreds of MB, multiple processes       | ~1 MB library, in-process               |
| Data scope           | Shared system of record                  | Local/embedded store, app file format    |

Keeping these goals in mind makes the architectural differences predictable
rather than arbitrary.

---

## 2. Architecture Overview

### PostgreSQL: client-server, multi-process

A PostgreSQL instance is a collection of cooperating OS processes that share a
common memory region and a single on-disk data directory. Clients never touch
the files; they speak a wire protocol to the server.

```
   Client A        Client B        Client C
      |               |               |
      |  libpq / wire protocol (TCP / unix socket)
      v               v               v
 +-----------------------------------------------+
 |              postmaster (supervisor)          |
 |   listens, authenticates, forks a backend     |
 +-----------------------------------------------+
        | fork()        | fork()       | fork()
        v               v              v
   +---------+      +---------+    +---------+
   | backend |      | backend |    | backend |   <- one process per connection
   +----+----+      +----+----+    +----+----+
        |                |              |
        |  read/write through shared memory
        v                v              v
 +-----------------------------------------------+
 |        Shared Memory (shared_buffers,         |
 |        WAL buffers, lock table, ...)          |
 +-----------------------------------------------+
        ^                                  |
        | flush                            | dirty pages
        |                                  v
   +----------+   +-----------+   +-------------------+
   | WAL      |   | checkptr  |   | bgwriter / autovac|  <- background workers
   | writer   |   |           |   |                   |
   +----+-----+   +-----+-----+   +---------+---------+
        |               |                   |
        v               v                   v
 +-----------------------------------------------+
 |   Data directory:  pg_wal/  base/  ...        |  (heap files, indexes, WAL)
 +-----------------------------------------------+
```

### SQLite: embedded, in-process

SQLite has no server and no background processes. The engine is a library
compiled into the application. The "architecture" is a stack of layers inside
the application's own address space, sitting on top of one database file.

```
 +-----------------------------------------------+
 |             Application process               |
 |                                               |
 |   Application code                            |
 |        |  sqlite3_* API calls                 |
 |        v                                      |
 |   +---------------------------------------+   |
 |   | SQL compiler (tokenizer, parser,      |   |
 |   | code generator) -> VDBE bytecode      |   |
 |   +-------------------+-------------------+   |
 |                       v                       |
 |   +---------------------------------------+   |
 |   | VDBE (virtual machine executes ops)   |   |
 |   +-------------------+-------------------+   |
 |                       v                       |
 |   +---------------------------------------+   |
 |   | B-Tree layer (tables + indexes)       |   |
 |   +-------------------+-------------------+   |
 |                       v                       |
 |   +---------------------------------------+   |
 |   | Pager (cache, transactions, locks)    |   |
 |   +-------------------+-------------------+   |
 |                       v                       |
 |   +---------------------------------------+   |
 |   | OS interface (VFS): read/write/lock   |   |
 |   +-------------------+-------------------+   |
 +-----------------------|-----------------------+
                         v
              +---------------------+
              |  database file      |  (+ -wal / -journal)
              +---------------------+
```

### Request / data flow compared

| Stage              | PostgreSQL                                              | SQLite                                                |
|--------------------|--------------------------------------------------------|------------------------------------------------------|
| Connection         | TCP/socket handshake, auth, dedicated backend forked   | Function call into linked library; no connection cost |
| Parse / plan       | Per-backend parser + cost-based optimizer              | Parser + code generator emit VDBE bytecode           |
| Execution          | Executor runs plan tree, reads shared buffers          | VDBE interprets bytecode, calls B-tree layer          |
| Cache              | Shared across all backends (`shared_buffers`)          | Per-connection page cache in the process              |
| Disk access        | Background processes flush; backends rarely fsync data | The calling thread does the I/O via the VFS          |
| Crossing a boundary| Network / IPC round trips per statement                | Plain function calls — no IPC, no serialization      |

The single most consequential difference is that PostgreSQL puts a process
boundary (and usually a network) between the application and the data, while
SQLite puts the data behind a function call in the same process. Almost every
trade-off in later sections traces back to that one structural fact.

---

## 3. Internal Design

### PostgreSQL

**Process model.** PostgreSQL uses a process-per-connection model rather than
threads. A supervisor process, the *postmaster*, listens for connections and
`fork()`s a dedicated *backend* for each client. Process isolation means a crash
or memory corruption in one backend cannot directly scribble over another's
memory; the postmaster detects the failure, resets shared memory, and recovers.
The cost is that each backend is relatively heavy (its own address space, plan
cache, work memory), which is why production deployments place a connection
pooler such as PgBouncer in front of the server instead of opening thousands of
raw connections.

**Shared memory.** Because backends are separate processes, they need an
explicit shared region to cooperate. At startup PostgreSQL allocates a block of
shared memory holding the buffer pool (`shared_buffers`), WAL buffers, the lock
table, and various coordination structures. This is the meeting point of all
backends: it is how one transaction's committed changes become visible to
others and how locks are negotiated.

**Buffer manager.** The buffer manager caches 8 KB pages in `shared_buffers`.
When a backend needs a page it asks the buffer manager, which returns the cached
copy or reads it from disk into a free buffer. Pages modified in memory are
marked *dirty* and are not written back immediately; eviction uses a clock-sweep
replacement policy. Critically, a dirty data page is never allowed to reach disk
before the corresponding WAL record — this is the WAL rule that makes crash
recovery possible.

**WAL (Write-Ahead Logging).** Before any change is applied to a data page, a
record describing that change is written to the write-ahead log. On `COMMIT`,
the WAL up to that point is flushed (`fsync`) to durable storage; the heap pages
themselves can be written lazily later. This gives two big wins: durability
without forcing random writes of every touched page at commit time, and a replay
log for recovery — after a crash, PostgreSQL replays WAL from the last
checkpoint to bring data files back to a consistent state. WAL is also the
foundation for streaming replication and point-in-time recovery.

**Heap storage.** Tables are stored as *heap files*: collections of 8 KB pages
with no inherent row ordering. Each page has a header, an array of item pointers
(line pointers) growing from the front, and tuples growing from the back, with
free space in the middle. A row's physical address is a `(block number, item
pointer)` pair called the TID. Because the heap is unordered, finding a specific
row generally requires an index or a sequential scan.

**B-Tree indexes.** The default index type is a B-tree (specifically a
Lehman-Yao high-concurrency variant) that supports equality and range queries
and ordered traversal. Index entries point at heap TIDs. PostgreSQL indexes are
*secondary*: the table data lives in the heap, and indexes reference it — there
is no clustered "primary" storage by default. PostgreSQL also offers Hash, GiST,
GIN, SP-GiST, and BRIN index types for other access patterns, which reflects its
extensibility heritage.

**MVCC overview.** PostgreSQL implements Multi-Version Concurrency Control by
keeping multiple physical versions of a row. Each tuple carries `xmin` (the
transaction that created it) and `xmax` (the transaction that deleted/replaced
it). An `UPDATE` does not overwrite in place; it writes a new tuple version and
marks the old one's `xmax`. A transaction sees a snapshot — the set of row
versions visible as of its start (or statement, depending on isolation level).
The huge payoff is that **readers never block writers and writers never block
readers**. The cost is dead tuples accumulating on disk, which the *autovacuum*
background process must reclaim; this bookkeeping (and transaction ID wraparound
management) is a defining operational characteristic of PostgreSQL.

### SQLite

**Embedded engine model.** SQLite is a library, not a service. Compiling it into
an application is the entire "installation." There is no listener, no
authentication layer, and no inter-process protocol; the SQL compiler, virtual
machine, B-tree layer, and pager all run inside the calling thread. SQL is
compiled into bytecode for the VDBE (Virtual Database Engine), a small register
machine that executes the query plan one opcode at a time.

**Single database file.** An entire SQLite database — every table, index, view,
and trigger, plus the schema catalog — lives in one ordinary file on disk. That
file is portable across machines and architectures (the format is endian-defined
and stable), which is why SQLite is also used as an application *file format*,
not just a cache.

**Pager layer.** The pager is the heart of SQLite's reliability. It sits between
the B-tree layer and the OS, and is responsible for reading/writing pages,
caching them, acquiring file locks, and implementing atomic commit and rollback.
The B-tree layer thinks in terms of pages; the pager makes those page operations
transactional and durable.

**Page cache.** The pager maintains an in-memory cache of recently used pages
(default ~2000 pages, tunable with `PRAGMA cache_size`). Because there is no
shared server, this cache is *per database connection*, living in the
application's heap. Two connections to the same file (even in the same process)
keep separate caches and coordinate through file locks, not shared memory.

**B-Tree storage.** SQLite stores everything in B-trees within the file. Tables
are stored as B+-trees keyed by a 64-bit integer rowid, with the actual row data
in the leaf pages — so an ordinary SQLite table is effectively a *clustered*
index on rowid. Declaring `WITHOUT ROWID` makes the table a B-tree keyed by its
primary key instead. Separate indexes are stored as their own B-trees whose
entries hold the indexed columns plus the rowid to find the full row.

**Journaling modes.** To make commits atomic, SQLite must be able to undo a
half-finished write after a crash. It offers two main strategies:

- *Rollback journal* (default): before modifying pages, SQLite copies the
  original pages into a separate `-journal` file. On commit it deletes the
  journal; on crash it replays the journal to restore the originals. This
  requires an exclusive lock during writes.
- *WAL mode* (`PRAGMA journal_mode=WAL`): new changes are appended to a `-wal`
  file and only later *checkpointed* back into the main database. WAL mode lets
  readers continue against the main file while a writer appends to the WAL, so
  readers and one writer can proceed concurrently — a substantial concurrency
  improvement over rollback journaling.

**Transaction model.** SQLite transactions are ACID and serialized. In rollback
mode a writer takes an exclusive lock and other connections wait; in WAL mode
one writer and many readers coexist, but there is still **at most one writer at a
time** per database. Locking is enforced through OS file-locking primitives,
which is also why SQLite over a network filesystem is discouraged — those
filesystems often implement locking incorrectly.

### Storage Comparison

| Aspect            | PostgreSQL                                      | SQLite                                          |
|-------------------|-------------------------------------------------|-------------------------------------------------|
| File organization | A *data directory* with many files (one+ per relation, plus `pg_wal/`) | A single database file (+ `-wal`/`-journal`)    |
| Default page size | 8 KB (compile-time fixed)                        | 4 KB default, configurable via `PRAGMA page_size` (512–65536) |
| Table storage     | Unordered **heap**; rows located by TID         | **Clustered B-tree** keyed by rowid (or PK if `WITHOUT ROWID`) |
| Index storage     | Always secondary; entries point to heap TIDs    | Secondary B-tree; entries point to rowid        |
| Primary clustering| None by default (`CLUSTER` is a one-off reorg)  | Table *is* the rowid B-tree — inherently clustered |
| Type system       | Strict, rich static types per column            | Dynamic typing (type *affinity*), value type stored per cell |

The heap-vs-clustered split has real query consequences. In SQLite a lookup by
rowid lands directly on the row in one B-tree descent. In PostgreSQL an index
lookup yields a TID that then requires a separate heap fetch — which is why
*index-only scans* (serving a query entirely from the index plus the visibility
map) are an important optimization there.

### Transaction & Concurrency Comparison

**PostgreSQL MVCC.** Concurrency is version-based. Each writer creates new row
versions; each reader works from a snapshot. The result is high multi-writer
throughput: many backends can modify different rows simultaneously, and a
long-running report does not block writes. Row-level locks are taken only for
conflicting writes to the *same* row. Isolation levels (Read Committed,
Repeatable Read, Serializable) are implemented on top of snapshots, with
Serializable using Serializable Snapshot Isolation to detect dangerous
read/write dependency cycles.

**SQLite locking.** Concurrency is lock-based and coarse. The classic rollback
model uses a lock progression (`UNLOCKED → SHARED → RESERVED → PENDING →
EXCLUSIVE`): multiple readers may hold SHARED locks, but a writer must escalate
to EXCLUSIVE, which blocks everyone. WAL mode relaxes this so readers don't block
the writer and vice versa, but the single-writer rule remains. There is no
row-level locking; the unit of contention is effectively the whole database.

| Property                  | PostgreSQL                          | SQLite (rollback)        | SQLite (WAL)               |
|---------------------------|-------------------------------------|--------------------------|----------------------------|
| Concurrent readers        | Many                                | Many                     | Many                       |
| Concurrent writers        | Many (row-level)                    | One at a time (DB lock)  | One at a time              |
| Readers block writers?    | No (MVCC)                           | Yes (writer needs EXCL)  | No                         |
| Writers block readers?    | No                                  | Yes                      | No                         |
| Lock granularity          | Row / page / table as needed        | Whole database file      | Whole database file        |
| Mechanism                 | Snapshots + lightweight locks       | OS file locks            | OS file locks + WAL frames |

**Multi-user behavior.** PostgreSQL is designed for many concurrent writers and
degrades gracefully under contention because conflicts are localized to
individual rows. SQLite assumes one application as the gatekeeper; with multiple
writers it serializes them and surfaces `SQLITE_BUSY` when a writer cannot get
the lock within the busy timeout. For a single-writer / many-reader embedded
workload this is perfectly adequate and very fast; for a busy multi-writer
service it is the wrong tool.

---

## 4. Design Trade-Offs

Each row below is a direct consequence of the client-server vs embedded split.

| Dimension              | PostgreSQL                                                                 | SQLite                                                                      | Why the trade-off exists                                                                                                   |
|------------------------|---------------------------------------------------------------------------|----------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------|
| Scalability            | Scales up to many cores/connections; replication for read scale-out       | Scales down to tiny footprints; bounded by single-writer + single machine  | PG's shared memory + MVCC coordinate many backends; SQLite deliberately omits that machinery to stay small.               |
| Performance            | Higher peak throughput under concurrency; per-query network + planning overhead | Extremely low latency for local single calls; no IPC, no network          | SQLite executes in-process function calls; PG pays for a process/network boundary in exchange for sharing and isolation.   |
| Deployment             | Install server, configure, run a daemon, manage users                     | Link a ~1 MB library; ship one file; nothing to run                        | A shared service must exist independently of clients; an embedded store should not.                                        |
| Concurrency            | Many concurrent writers via row-level MVCC                                 | One writer at a time; many readers (esp. WAL)                              | MVCC needs versioning + vacuum; SQLite trades that for simplicity and a single file.                                       |
| Durability             | WAL + checkpoints + replication + PITR                                     | WAL/rollback journal gives ACID on one file                               | Both are crash-safe; PG adds machinery (archiving, replicas) a shared system of record needs.                             |
| Resource consumption   | Hundreds of MB baseline, per-backend memory, background workers           | Kilobytes of overhead; cache sized to the app                             | A standalone server reserves shared resources up front; a library borrows the host's.                                     |
| Operational complexity | Tuning, autovacuum, connection pooling, backups, upgrades                  | Effectively none; back up by copying the file                             | Operational surface is the price of a configurable multi-user server; SQLite removes the surface by removing the server.   |

The unifying theme: **PostgreSQL spends complexity and resources to make shared,
concurrent, durable data correct at scale; SQLite spends correctness-preserving
restrictions (one writer, one file, one process) to make embedded data trivial
to deploy.** Neither is "better" — they optimize different cost functions.

---

## 5. Experiments / Observations

These are observations consistent with the architecture, not benchmarks.

**`PRAGMA page_size` in SQLite.** The page size is a property of the file and
must be set *before* the first table is created (or followed by a `VACUUM` to
rewrite the file):

```sql
PRAGMA page_size = 8192;   -- must come before tables exist, or VACUUM after
PRAGMA page_size;          -- read back the value actually in effect
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
```

Observation tied to architecture: the page size is baked into the file header
because the *entire* database is that one file and every B-tree node is a page of
that size. Larger pages mean fewer, larger I/O operations and shallower B-trees
(good for big sequential rows), while smaller pages waste less space on tiny
rows. PostgreSQL, by contrast, fixes its page size at 8 KB at compile time — you
do not tune it per database, because the design assumes a managed server, not a
portable file.

**Inspecting a SQLite database.** Useful read-only inspection without external
tools:

```sql
.tables                         -- list tables
.schema                         -- dumped DDL, reconstructed from sqlite_master
SELECT type, name, rootpage FROM sqlite_master;   -- the schema catalog itself
PRAGMA table_info(t);           -- columns, types, PK flags
PRAGMA index_list(t);           -- indexes on a table
PRAGMA journal_mode;            -- 'delete' (rollback) vs 'wal'
PRAGMA freelist_count;          -- unused pages == internal fragmentation
```

What this reveals about the design: the schema is *self-describing data* inside
the same file (`sqlite_master` is itself a table whose `rootpage` points at each
object's B-tree root). There is no external catalog, no system process — opening
the file is sufficient to learn its entire structure. Turning WAL mode on and
watching a `-wal` file appear next to the database makes the journaling layer
visible directly on the filesystem. Running many inserts and then observing
`freelist_count` grow after deletes shows why a `VACUUM` is occasionally needed
to shrink the file.

**PostgreSQL `EXPLAIN ANALYZE`.** PostgreSQL exposes its cost-based planner
directly:

```sql
EXPLAIN ANALYZE SELECT * FROM orders WHERE customer_id = 42;
```

Discussion: `EXPLAIN` prints the chosen plan with estimated costs; adding
`ANALYZE` actually runs it and reports real row counts and timing per node. You
can watch the planner switch from a *Seq Scan* to an *Index Scan* as a table
grows and an index becomes selective enough to be worth the random heap fetches,
and you can see a *Bitmap Heap Scan* appear when many scattered rows must be
gathered. Crucially the plan you get reflects table statistics maintained by
`ANALYZE`/autovacuum — a direct consequence of PG's "managed server with
background workers" architecture. SQLite has a simpler planner (`EXPLAIN QUERY
PLAN`) and historically far fewer knobs, matching its goal of predictable
behavior without a DBA.

**Behavior caused by architectural differences.**

- Concurrent writers in SQLite eventually return `SQLITE_BUSY`; the same
  workload in PostgreSQL simply proceeds because MVCC lets writers touch
  different rows independently. This is the single-writer file lock vs row-level
  versioning split made observable.
- A connection to PostgreSQL has measurable setup cost (process fork, auth);
  opening a SQLite database is a near-instant function call. Applications that
  open/close connections per request feel this difference immediately.
- After heavy `UPDATE`/`DELETE` traffic, a PostgreSQL table's on-disk size grows
  with dead tuples until autovacuum reclaims them — a visible artifact of MVCC.
  SQLite's file grows with freelist pages until `VACUUM`. Same problem (reclaim
  space from a versioned/append-style writer), two operational answers.

---

## 6. Key Learnings

**Most important architectural insights.**

1. The deepest divergence is *where the database lives relative to the
   application*: behind a process/network boundary (PostgreSQL) versus inside the
   process as a library (SQLite). Nearly every other difference — shared memory,
   connection cost, locking granularity, deployment story — is downstream of that
   single decision.
2. Both systems solve durability with write-ahead logging, but for different
   reasons: PostgreSQL needs WAL to coordinate many backends and feed
   replication; SQLite needs a journal/WAL purely to make a *single file's*
   commit atomic across a crash.
3. Concurrency models are a choice between *versioning* (PostgreSQL MVCC: many
   writers, with vacuum as the tax) and *serialization* (SQLite: one writer, with
   simplicity as the reward).
4. Storage layout matters: PostgreSQL's unordered heap plus secondary indexes vs
   SQLite's rowid-clustered B-tree produces genuinely different query mechanics
   (heap fetch vs single descent).

**Engineering decisions worth remembering.** PostgreSQL chose process isolation
over threads for robustness, paid for with per-connection weight (mitigated by
pooling). SQLite chose to omit a server entirely, paid for with the single-writer
constraint (mitigated by WAL mode). In both cases the headline limitation is the
deliberate price of the headline strength.

**When to choose PostgreSQL.** Many concurrent writers; data shared across
applications or services; need for replication, point-in-time recovery,
fine-grained security, advanced types/extensions, or sophisticated query
planning; a system of record that outlives any one client.

**When to choose SQLite.** Embedded or on-device storage; application file
formats; local caches and configuration; single-application data with at most one
writer at a time; environments where "no server to operate" is the decisive
requirement (mobile, desktop, edge, tests, prototypes). SQLite's own guidance is
apt: it is an alternative to `fopen()`, not to a client-server RDBMS — and it is
chosen for exactly the workloads where that framing fits.

The practical lesson from comparing them is that database architecture is the
study of which costs you are willing to pay. PostgreSQL pays in resources and
operational complexity to make concurrent shared data correct; SQLite pays in
concurrency limits to make embedded data effortless. Understanding a system means
being able to predict its behavior from the cost it chose to pay — and these two,
sharing SQL but almost nothing else, make that lesson unusually clear.

---

## References

1. PostgreSQL Global Development Group — *PostgreSQL Documentation*: "Database
   Physical Storage", "Write-Ahead Logging (WAL)", "Concurrency Control (MVCC)",
   and "Internals". https://www.postgresql.org/docs/current/
2. SQLite — *Architecture of SQLite*. https://www.sqlite.org/arch.html
3. SQLite — *The SQLite Database File Format*. https://www.sqlite.org/fileformat2.html
4. SQLite — *Write-Ahead Logging* and *File Locking And Concurrency*.
   https://www.sqlite.org/wal.html , https://www.sqlite.org/lockingv3.html
5. SQLite — *Appropriate Uses For SQLite* ("alternative to fopen()").
   https://www.sqlite.org/whentouse.html
6. SQLite — *PRAGMA Statements*. https://www.sqlite.org/pragma.html
7. M. Stonebraker, L. Rowe — "The Design of POSTGRES", UC Berkeley, 1986.
8. P. Lehman, S. B. Yao — "Efficient Locking for Concurrent Operations on
   B-Trees", ACM TODS, 1981.
9. T. Lane et al. — *PostgreSQL: Inside the Buffer Manager / Vacuum* (project
   documentation and source comments, `src/backend/storage/`).
