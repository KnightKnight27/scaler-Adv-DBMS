# PostgreSQL vs SQLite — An Architecture Comparison

Two of the most widely used relational databases on the planet sit at opposite
ends of a design spectrum. SQLite is the most deployed database engine in
existence (it ships in every Android phone, iOS app, browser, and a lot of
aircraft entertainment systems), yet it has no server, no separate process, and no
network port. PostgreSQL powers banks, analytics platforms, and multi-terabyte
warehouses with a full client-server stack.

They solve different problems, and almost every difference between them traces
back to one early decision: embedded library versus client-server process. This
document follows that single choice as it ripples through storage, concurrency,
durability, and scalability.

All experiments below were run locally on PostgreSQL 18.3 and SQLite 3.51.0, and
the outputs are copied from the actual runs.

---

## 1. Problem Background

### Why SQLite exists
SQLite was written in 2000 by D. Richard Hipp, originally for a U.S. Navy program
that needed a database which could run without a database administrator and
without a server process. The goal was an SQL engine you could link directly into
your application, the same way you link any other library.

The design target is the edge: one application, often one user, talking to a local
file. Think of a phone app storing your messages, or a browser caching history.
There is no network, so paying the cost of a network protocol, an authentication
layer, and a separate daemon makes no sense.

As SQLite's own documentation puts it, it is not competing with PostgreSQL. It is
competing with `fopen()`.

### Why PostgreSQL exists
PostgreSQL descends from the POSTGRES research project at UC Berkeley (1986,
Michael Stonebraker). Its target is the opposite: many users, many connections,
large datasets, and strict correctness and durability, running as shared
infrastructure that lots of applications connect to over a network.

When dozens of clients read and write concurrently, you need a process that owns
the data, arbitrates access, enforces permissions, and survives crashes without
corruption. That process is a server.

### The one decision that drives everything
| Question | SQLite's answer | PostgreSQL's answer |
|---|---|---|
| Who owns the data file? | The application process itself | A dedicated server process |
| How do you "connect"? | Call a C function | Open a TCP/Unix socket |
| How many writers at once? | One (whole-database) | Many (row-level) |
| Where does it run? | Inside your app | On a server you talk to |

---

## 2. Architecture Overview

### SQLite — embedded, in-process

```
┌────────────────────────────────────────┐
│            Your Application             │
│                                         │
│   app code  ──calls──►  SQLite library  │
│                          (linked .so/.a)│
│                              │          │
│   SQL compiler → VDBE bytecode          │
│              → B-tree layer             │
│              → Pager (cache + journal)  │
│              → OS file syscalls         │
└──────────────────────────────┬──────────┘
                               │ read()/write()/fsync()
                               ▼
                    ┌────────────────────┐
                    │   single file:     │
                    │   mydata.db        │
                    │  (+ -wal, -shm)    │
                    └────────────────────┘
```

There is no process boundary here. A SQL query is just a function call. The entire
engine (parser, planner, virtual machine, B-tree, pager) is library code running
inside your application's address space.

### PostgreSQL — client-server, multi-process

```
   Client A        Client B        Client C
   (psql)          (app)           (ORM)
      │  TCP/Unix      │               │
      └────────┬───────┴───────┬───────┘
               ▼               ▼
        ┌─────────────────────────────────┐
        │           Postmaster            │  ← listens, forks a backend per client
        └─────────────────────────────────┘
          │            │            │
      ┌───────┐    ┌───────┐    ┌───────┐
      │backend│    │backend│    │backend│   ← one process per connection
      └───┬───┘    └───┬───┘    └───┬───┘
          └─────┬──────┴──────┬─────┘
                ▼             ▼
        ┌──────────────────────────────────┐
        │   Shared Memory (shared_buffers)  │  ← shared page cache
        └──────────────────────────────────┘
                       │
   Background processes:  WAL writer · Checkpointer · Autovacuum · Bgwriter
                       │
                       ▼
            ┌───────────────────────┐
            │  data files + WAL dir  │
            └───────────────────────┘
```

Data flow on a write:
1. The client sends SQL over a socket to its backend process.
2. The backend modifies pages in `shared_buffers` (in RAM).
3. The change is recorded in the WAL and `fsync`-ed before commit returns.
4. The background checkpointer and bgwriter later flush dirty pages to the heap
   files.
5. Autovacuum reclaims space from dead row versions in the background.

The structural difference is worth stating plainly: SQLite's background work
simply doesn't exist, because there are no helper processes. PostgreSQL offloads
durability, cleanup, and flushing to dedicated processes precisely because it
serves many clients at once.

---

## 3. Internal Design

### 3.1 Storage layout

SQLite keeps everything in one file. Every table, every index, and the schema
itself live in a single file divided into fixed-size pages. Confirmed locally:

```
PRAGMA page_size;  -->  4096
```

Each table is a B-tree keyed by `rowid` (or by an `INTEGER PRIMARY KEY`, which
becomes the rowid). Each index is a separate B-tree. The first page holds the
100-byte file header and the schema. Everything (tables, indexes, free list) is
packed into that one file, which is why "copy the database" really is just "copy
one file."

PostgreSQL spreads data across many files, and separates the table from its
indexes. Each table is a heap: an unordered collection of 8 KB pages. Rows are
inserted wherever there is free space, with no inherent ordering. Indexes (B-tree
by default) are entirely separate files that point back into the heap via a
physical address called the `ctid` (page number, item number).

```
8 KB heap page:
┌──────────────┬───────────────────────────┬──────────────┐
│ page header  │ item pointers →            │ ... free ... │
├──────────────┴───────────────────────────┤              │
│  ◄── tuples grow upward from the bottom   │              │
└───────────────────────────────────────────┴──────────────┘
```

Because the heap is unordered, PostgreSQL leans on indexes to find anything fast,
and a row's physical location (`ctid`) can change when it is updated. That last
detail matters enormously for MVCC, below.

The deeper contrast is clustered versus heap storage. SQLite clusters table data
inside the primary-key B-tree, so the row data lives in the leaf. PostgreSQL keeps
the table heap and the indexes separate. (This is the same split you see between
InnoDB and PostgreSQL.)

### 3.2 Memory management

- SQLite uses a per-connection page cache in the application's heap, sized by
  `PRAGMA cache_size`. By default there is no shared cache across processes;
  caching is the application's concern.
- PostgreSQL uses a process-shared `shared_buffers` pool in shared memory, so
  every backend benefits from pages another backend already read. It manages them
  with a clock-sweep replacement policy and treats the OS page cache as a second
  tier.

This follows directly from the architecture. You can only share a cache if there
is a shared process to host it.

### 3.3 Concurrency control

This is the sharpest difference between the two engines.

SQLite locks the whole database. Its default concurrency model allows one writer
at a time for the entire database. In rollback-journal mode, a writer takes an
`EXCLUSIVE` lock that blocks all readers. WAL mode (which I enabled below) relaxes
this so that readers don't block the writer and the writer doesn't block readers,
but there is still only ever one writer.

```
PRAGMA journal_mode = WAL;   -->  wal
```

PostgreSQL uses MVCC with row-level versioning. Every row version carries two
hidden system columns:
- `xmin` — the transaction id that created this version
- `xmax` — the transaction id that deleted or superseded it (0 if live)

A transaction sees a row only if its `xmin` is committed and visible to the
transaction's snapshot, and its `xmax` is not. This is snapshot isolation: readers
never block writers and writers never block readers, because an `UPDATE` doesn't
overwrite the old row. It writes a new version and marks the old one's `xmax`. I
demonstrated this live (Section 5): updating one row changed its `ctid` from
`(0,1)` to `(28,67)` and bumped `xmin`, leaving a dead version behind.

The catch is that those dead versions accumulate, so PostgreSQL runs `VACUUM`
(automatically, via autovacuum) to reclaim them. SQLite avoids that problem
entirely by updating in place, but it pays with coarse-grained locking instead.

| | SQLite | PostgreSQL |
|---|---|---|
| Granularity | Whole database | Per row (MVCC) |
| Concurrent writers | 1 | Many |
| Update strategy | In-place | New row version + VACUUM |
| Reader/writer blocking | None *in WAL mode* | None (MVCC) |

### 3.4 Durability and recovery

Both engines use write-ahead logging, but at very different scales.

SQLite offers either a rollback journal (the default: write the old pages to a
`-journal` file, then modify the database, and on crash roll back) or WAL mode
(append new pages to a `-wal` file and checkpoint later). A single `fsync` at
commit guarantees the change survives a crash.

PostgreSQL maintains a dedicated, continuously growing WAL stream in `pg_wal/`.
Every change is logged before the data page is flushed. On crash, PostgreSQL
replays WAL from the last checkpoint to recover a consistent state. The same WAL
stream also powers replication and point-in-time recovery, features that only make
sense for a long-running server.

Again the asymmetry comes back to purpose. SQLite needs to survive a phone losing
power. PostgreSQL needs that too, plus streaming changes to replicas and
recovering multi-terabyte databases.

---

## 4. Design Trade-Offs

### SQLite
Advantages
- Zero configuration, zero administration, no separate process.
- The whole database is one portable file, trivial to copy, ship, or embed.
- No network round-trip: a query is a function call, so latency is in
  microseconds.
- Tiny footprint (~1 MB library), runs anywhere C runs.

Limitations
- One writer at a time. Concurrent write-heavy workloads serialize.
- No network access, no users/roles, no built-in replication.
- Limited type checking (dynamic typing; `STRICT` tables are opt-in).
- Scales down beautifully, but not out, and not up to many concurrent writers.

### PostgreSQL
Advantages
- True multi-user concurrency via MVCC; hundreds of simultaneous writers.
- Rich SQL, strong typing, constraints, and extensions (PostGIS, JSONB, etc.).
- Replication, PITR, roles and permissions, and a mature query planner.

Limitations
- You have to run and administer a server (config, connections, vacuum tuning).
- The per-connection process model makes thousands of connections expensive, which
  is why poolers like PgBouncer are common.
- MVCC produces dead tuples, so `VACUUM` is mandatory maintenance.
- Overkill for a single-user local app: you pay for a server you don't need.

### The engineering decision, summarized
You don't really choose between them on "which is better." You choose on
concurrency and topology:

- One application, local data, mostly one writer → SQLite. Adding a server would
  only add cost and failure modes.
- Many clients, shared data, concurrent writes, durability and replication needs →
  PostgreSQL. The server isn't overhead; it is the entire point.

---

## 5. Experiments / Observations

### 5.1 SQLite: the query plan uses the index, and the whole DB is one file
Dataset: 5,000 users, 20,000 orders, with an index on `orders.user_id`.

```sql
EXPLAIN QUERY PLAN
SELECT u.city, COUNT(*), SUM(o.amount)
FROM users u JOIN orders o ON o.user_id = u.id
WHERE u.city = 'NYC' GROUP BY u.city;
```
```
QUERY PLAN
|--SCAN u
`--SEARCH o USING INDEX idx_orders_user (user_id=?)
```
SQLite picks a nested-loop join: scan `users`, and for each match probe the
`orders` index. The entire 5k+20k-row database lives in a single 634 KB file, and
switching durability mode was a one-liner: `PRAGMA journal_mode=WAL`.

### 5.2 PostgreSQL: the planner chooses a different join and shows real statistics
Same dataset and query, via `EXPLAIN (ANALYZE, BUFFERS)`:

```
GroupAggregate (actual time=7.295..7.296 rows=1 loops=1)
  Buffers: shared hit=138
  ->  Hash Join  (cost=112.33..473.87 rows=6664) (actual rows=6664)
        Hash Cond: (o.user_id = u.id)
        ->  Seq Scan on orders o   (actual rows=20000)
        ->  Hash  (rows=1666)
              ->  Seq Scan on users u
                    Filter: (city = 'NYC')
                    Rows Removed by Filter: 3334
 Planning Time: 6.534 ms
 Execution Time: 7.690 ms
```

Given the same query, PostgreSQL's cost-based planner chose a hash join (build a
hash table of NYC users, stream orders through it) instead of SQLite's nested
loop. Its estimate of `rows=6664` matched the actual `6664` exactly, because the
planner relies on statistics collected by `ANALYZE` and stored in `pg_statistic`.
`Buffers: shared hit=138` shows all 138 pages were served from `shared_buffers`,
so zero disk reads. SQLite has no comparable cost model or shared-buffer
accounting, because it has neither a planner this elaborate nor a shared cache.

### 5.3 PostgreSQL: watching MVCC create a dead tuple
```sql
SELECT ctid, xmin, xmax, city FROM users WHERE id = 1;
-- (0,1)  | 814 | 0 | LA      ← original version, on heap page 0

UPDATE users SET city = 'BOS' WHERE id = 1;

SELECT ctid, xmin, xmax, city FROM users WHERE id = 1;
-- (28,67)| 819 | 0 | BOS     ← brand-new version on a different page

SELECT n_dead_tup FROM pg_stat_user_tables WHERE relname='users';
-- n_dead_tup = 1             ← the old (0,1) version is now dead
```

The `UPDATE` did not edit the row in place. It wrote an entirely new tuple at a new
physical location `(28,67)` with a new `xmin`, and the old `(0,1)` version became a
dead tuple awaiting `VACUUM`. This is MVCC made visible, and it is exactly why
PostgreSQL needs background vacuuming while SQLite, which updates in place, does
not.

---

## 6. Key Learnings

The clearest lesson is how far a single decision reaches. Embedded-vs-server is not
one feature among many; it predicts the storage model, the concurrency model, the
durability machinery, and the operational story. Knowing only "library vs server,"
you could re-derive most of the other differences.

Concurrency turned out to be the real dividing line. SQLite serializes writers
because a single local app rarely needs more, and that simplicity buys a lot of
robustness. PostgreSQL's MVCC exists to let many writers proceed without blocking,
and the price of that is dead tuples and VACUUM, which I watched happen in a single
`UPDATE`.

A couple of things stood out as genuine trade-offs rather than one option being
strictly better:

- In-place vs versioned updates. SQLite's in-place update is simple and needs no
  cleanup, but it forces coarse locking. PostgreSQL's copy-on-write versioning
  enables snapshot isolation but requires a garbage collector. Neither wins
  outright.
- The query planner reflects the workload it was built for. Same data, same query,
  two different join algorithms. PostgreSQL invests in a cost-based,
  statistics-driven planner because it must serve unpredictable queries at scale;
  SQLite stays lean because most of its queries are simple point or range lookups.

The takeaway I'd keep is that "best database" is the wrong question. SQLite being
installed on billions more devices than PostgreSQL doesn't make it better, it makes
it right for a different job. A database is a bundle of trade-offs, and the skill
is matching the bundle to the workload: local single-app data leans SQLite, shared
concurrent infrastructure leans PostgreSQL.

---

### References
- SQLite — *"Appropriate Uses For SQLite"*, *"Write-Ahead Logging"*, and the file
  format docs: <https://www.sqlite.org/whentouse.html>,
  <https://www.sqlite.org/wal.html>, <https://www.sqlite.org/fileformat2.html>
- PostgreSQL Documentation — *Internals: Database Physical Storage*, *Concurrency
  Control (MVCC)*, *Write-Ahead Logging*, *Routine Vacuuming*:
  <https://www.postgresql.org/docs/current/storage.html>,
  <https://www.postgresql.org/docs/current/mvcc.html>,
  <https://www.postgresql.org/docs/current/wal-intro.html>
- M. Stonebraker & L. Rowe, *"The Design of POSTGRES"*, SIGMOD 1986.

*All experiments performed locally on PostgreSQL 18.3 and SQLite 3.51.0; the
outputs above are copied from actual runs.*
