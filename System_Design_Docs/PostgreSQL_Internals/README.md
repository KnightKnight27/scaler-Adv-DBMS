# PostgreSQL Internal Architecture

An analysis of how PostgreSQL is built underneath the SQL surface: how pages move
between disk and memory, how rows are versioned for concurrency, how durability is
guaranteed without making every commit pay for random disk writes, and why the
cleanup machinery (VACUUM) is not an optional bolt-on but a structural consequence
of the concurrency design. The emphasis throughout is on *why* each subsystem is
shaped the way it is, and what each design choice costs.

---

## 1. Problem Background

**Why PostgreSQL was created.** PostgreSQL traces back to the POSTGRES project led
by Michael Stonebraker at UC Berkeley in the mid-1980s, a successor to the earlier
Ingres relational system. The driving idea was that the pure relational model of
the day was too rigid: it lacked rich data types, user-defined operators, rules,
and the extensibility that real applications needed. POSTGRES set out to support
complex objects and an extensible type/operator system while keeping the
relational foundation. When SQL was added (Postgres95, then PostgreSQL in 1996),
the system became a general-purpose, open-source, standards-tracking RDBMS — but
the extensibility DNA remained, visible today in custom types, index access
methods, and procedural languages.

**Goals of PostgreSQL.** The system optimizes for *correctness and capability of a
shared system of record*, in roughly this priority order: data integrity and
durability (never silently lose or corrupt committed data), strong concurrency
(many sessions reading and writing simultaneously without blocking each other more
than necessary), standards-compliant and expressive SQL, and extensibility. Raw
single-threaded speed is deliberately *not* the top goal; the system will spend
CPU, memory, and disk to keep data correct under concurrent access and across
crashes.

**Why modern databases need sophisticated storage and transaction systems.** A
naïve database that wrote rows straight to their final disk location on every
statement would be simple but unusable in practice. Three pressures force
sophistication:

- *The memory/disk gap.* Disk (even SSD) is orders of magnitude slower than RAM,
  and random I/O is far worse than sequential. A serious engine must cache hot
  data in memory and convert random writes into sequential ones — hence a buffer
  manager and a write-ahead log.
- *Concurrency.* Many transactions touch overlapping data at once. Without a
  concurrency-control scheme, readers and writers would constantly block one
  another, or worse, see inconsistent data. PostgreSQL's answer is MVCC.
- *Crashes.* Power can fail mid-write. The engine must be able to recover to a
  consistent state where every committed transaction survives and every
  uncommitted one vanishes. That requires logging changes before applying them.

The rest of this document walks through the subsystems that exist to answer these
three pressures.

---

## 2. Architecture Overview

PostgreSQL is a multi-process client-server system. A supervisor process (the
*postmaster*) listens for connections and forks a dedicated *backend* process per
client. Backends do not share an address space, so they coordinate through a fixed
region of **shared memory** allocated at startup. Background processes handle
durability and cleanup outside the request path.

```
   psql / app          app             app
      |                 |               |
      |  libpq wire protocol (TCP / unix socket)
      v                 v               v
 +-----------------------------------------------------+
 |                 postmaster (listener)               |
 |        authenticates, forks one backend / client    |
 +-----------------------------------------------------+
        | fork()           | fork()         | fork()
        v                  v                v
   +----------+       +----------+     +----------+
   | backend  |       | backend  |     | backend  |
   |  Parser  |       |   ...    |     |   ...    |
   |  Planner |       +----------+     +----------+
   | Executor |
   +----+-----+
        |  buffer requests / tuple visibility checks
        v
 +-----------------------------------------------------+
 |                    SHARED MEMORY                     |
 |  +---------------------+   +----------------------+  |
 |  |  shared_buffers     |   |   WAL buffers        |  |
 |  |  (8KB page cache)   |   |   (pending records)  |  |
 |  +---------------------+   +----------------------+  |
 |  | lock table | proc array (snapshots) | CLOG |     |
 +-----------------------------------------------------+
     ^  read pages          | dirty pages       | WAL records
     |                      v                   v
 +-----------+   +----------+----------+   +-----------------+
 | bgwriter  |   | checkpointer        |   |  WAL writer     |
 +-----+-----+   +----------+----------+   +--------+--------+
       |                    |                       |
       v                    v                       v
 +-----------------------------------------------------+
 |  Data directory:  base/ (heap + index files)        |
 |                   pg_wal/ (WAL segments)             |
 +-----------------------------------------------------+
                            ^
                            |  autovacuum launcher + workers
                            +-- reclaim dead tuples, update stats
```

**Backend processes.** Each connection gets one backend that runs the full query
pipeline for that session. Process-per-connection gives strong isolation (a crash
in one backend cannot corrupt another's memory) at the cost of per-connection
weight, which is why production setups front the server with a pooler such as
PgBouncer.

**Shared memory components.** The large allocations are `shared_buffers` (the page
cache shared by all backends) and the WAL buffers (WAL records not yet flushed).
Alongside them sit the lock table, the *proc array* (used to build transaction
snapshots), and the commit log (CLOG/`pg_xact`) recording each transaction's
commit/abort state. Shared memory is the only place backends meet, so it is how a
committed change in one session becomes visible to another.

**Query execution flow.** A statement travels through a fixed pipeline:

```
Client SQL
   |
   v
[ Parser ]      -> raw parse tree -> semantic analysis (resolve tables, types)
   |
   v
[ Rewriter ]    -> applies rules / expands views
   |
   v
[ Planner /     -> cost-based: enumerates plans, estimates cost from statistics,
  Optimizer ]      picks the cheapest (scan methods, join order, join algorithms)
   |
   v
[ Executor ]    -> walks the plan tree, pulling tuples node by node
   |
   v
[ Buffer Manager ] -> serves 8KB pages from shared_buffers or reads from disk
   |
   v
[ Storage ]     -> heap files + index files in the data directory
```

The planner is where PostgreSQL earns much of its reputation: it is *cost-based*,
not rule-based, so the same query can produce different plans as the data and its
statistics change. The executor uses a demand-pull ("Volcano") model in which each
plan node asks its child for the next tuple, which keeps memory bounded for large
scans.

---

## 3. Internal Design

### Buffer Manager

All table and index data lives in 8 KB **pages** (blocks). Backends never read or
write data files directly for normal operations; they go through the buffer
manager, which caches pages in the `shared_buffers` region.

**Shared buffers.** `shared_buffers` is a fixed array of 8 KB buffer frames plus a
descriptor table tracking, for each frame, which page it holds, a dirty flag, a
pin count (how many backends are currently using it), and usage information for
replacement. Because it is in shared memory, a page read by one backend is
immediately reusable by every other backend.

**Page reads.** When the executor needs a page, it asks the buffer manager for
`(relation, block number)`. The manager hashes that identifier into a buffer
mapping table:

```
   need (rel=orders, block=42)
        |
        v
   hash lookup in buffer table
        |
   +----+------------------------------+
   | hit                          miss |
   v                                   v
 pin frame, return            find a victim frame (clock-sweep),
 page to caller               if victim is dirty -> write it out first,
                              read block 42 from disk into the frame,
                              record mapping, pin, return
```

A *pin* prevents the page from being evicted while in use; the caller unpins when
done.

**Page writes and dirty pages.** Modifications happen *in memory*. The backend
edits the cached page and sets its **dirty** flag — it does not immediately write
to disk. This is central: many updates to the same hot page collapse into a single
eventual disk write, and writes can be batched and ordered. Dirty pages reach disk
later, written by the **background writer** (trickles dirty buffers out to smooth
I/O) and the **checkpointer** (flushes all dirty buffers at checkpoint
boundaries). A backend only writes a data page itself when it must evict a dirty
victim to make room.

**Buffer replacement strategy.** PostgreSQL uses a *clock-sweep* algorithm, an
approximation of LRU that is cheap under concurrency. Each buffer has a usage
counter incremented on access. A circular "clock hand" sweeps the frames: if a
frame's usage counter is above zero it is decremented and skipped; the first frame
found at zero (and unpinned) becomes the victim. This favors frequently used pages
without the lock contention a strict LRU list would create.

**The WAL ordering rule.** A dirty page may not be written to disk before the WAL
record describing its change has been flushed. This is what makes the lazy-write
scheme crash-safe (see WAL, below).

**Why buffer management is critical.** It is the layer that hides the
memory/disk performance gap. It turns repeated access to hot data into RAM-speed
operations, converts many small in-place updates into fewer batched writes, and
gives the system a single place to enforce the write-ahead ordering that underpins
durability. Sizing `shared_buffers` correctly is one of the highest-leverage
tuning decisions precisely because this layer governs almost all I/O.

### B-Tree Implementation

The default index type is a B-tree — specifically a variant of the Lehman-Yao
high-concurrency B+-tree, which adds *right-links* between sibling pages so that
searches and inserts can proceed correctly even while another process is splitting
a page.

**Page structure.** Each B-tree node is one 8 KB page. The tree has three roles:

```
                        +-----------------+
   root page  --------> |  k1 | k2 | k3   |   (one page; entries are
                        +--+----+----+----+    separator keys + child ptrs)
                           |    |    |
            +--------------+    |    +--------------+
            v                   v                   v
   +-----------------+  +-----------------+  +-----------------+
   | internal page   |  | internal page   |  | internal page   |  (separator keys
   +--+-----+-----+--+  +--+-----+-----+--+  +--+-----+-----+--+   + child ptrs)
      |     |     |        ...                   ...
      v     v     v
   +------+ +------+ +------+
   | leaf |-| leaf |-| leaf |  <- leaves are doubly linked (range scans),
   +------+ +------+ +------+     entries are (index key -> heap TID)
```

- **Root page.** The single entry point; for a small index it may also be a leaf.
  As the tree grows the root holds separator keys pointing down to internal pages.
- **Internal pages.** Hold separator keys and child pointers only — no heap
  pointers. They route a search toward the correct leaf.
- **Leaf pages.** Hold the actual index entries: the indexed key plus a **TID**
  (`(block, offset)`) pointing to the row's location in the heap. Leaves are linked
  left-to-right (and right-to-left), which makes ordered range scans a simple walk
  along the leaf chain.

**Search path.** A lookup starts at the root and performs a binary search within
each page to choose the child whose key range contains the target, descending until
it reaches a leaf. Because the tree is shallow and wide (high fan-out — hundreds of
keys per page), even a large table is reached in a handful of page accesses; a
billion-row index is typically 4–5 levels deep.

**Inserts and page splits.** An insert descends to the correct leaf and adds the
entry in key order. If the leaf has room, done. If it is full, the page **splits**:
roughly half the entries move to a newly allocated sibling, and a separator key is
*propagated up* to the parent. If the parent is also full, it splits too, and the
split can cascade up to the root; splitting the root adds a new level. The Lehman-
Yao right-links let concurrent searchers that arrive mid-split still find moved
entries by following the sibling link, avoiding a global lock during the split.

**Why B-trees are the default.** B-trees are the best general-purpose index for
relational workloads because they serve the widest set of access patterns from one
structure: equality lookups, range scans (`BETWEEN`, `<`, `>`), prefix matching,
`ORDER BY` (the leaf order *is* sorted), and `MIN`/`MAX`. They stay balanced under
arbitrary insert/delete patterns, guaranteeing logarithmic access, and their high
fan-out keeps them shallow, which minimizes disk reads. Specialized types (Hash,
GiST, GIN, BRIN) beat B-trees for specific cases, but none match their breadth.

### MVCC (Multi-Version Concurrency Control)

PostgreSQL lets many transactions read and write concurrently without readers and
writers blocking each other by keeping **multiple physical versions** of each row.

**Heap tuples, xmin, xmax.** Every row version (tuple) stored in the heap carries a
header with two key transaction-id fields:

- `xmin` — the ID of the transaction that *created* this version.
- `xmax` — the ID of the transaction that *deleted or superseded* this version
  (0/invalid if the version is still live).

An `INSERT` writes a tuple with `xmin = current txid`, `xmax = 0`. A `DELETE` does
not erase the row; it sets `xmax = current txid` on the existing version. An
`UPDATE` is a delete + insert: it sets `xmax` on the old version and writes a new
version with `xmin = current txid`.

**Tuple-version example.** Suppose transaction 100 inserts a row, transaction 150
updates it, and transaction 180 deletes it:

```
After txid 100 INSERT (value = 'A'):
   [ xmin=100 | xmax=0   | value='A' ]   <- live

After txid 150 UPDATE value -> 'B':
   [ xmin=100 | xmax=150 | value='A' ]   <- old version, expired by 150
   [ xmin=150 | xmax=0   | value='B' ]   <- new live version

After txid 180 DELETE:
   [ xmin=100 | xmax=150 | value='A' ]
   [ xmin=150 | xmax=180 | value='B' ]   <- now expired by 180
```

All three physical versions can coexist on disk simultaneously. Which one a given
transaction *sees* depends on its snapshot.

**Visibility rules and snapshots.** When a statement (or transaction, depending on
isolation level) begins, PostgreSQL takes a **snapshot**: essentially the set of
transactions that had already committed at that instant, plus the list of
transactions still in flight. A tuple version is visible to a snapshot if:

- its `xmin` is a transaction that committed *before* the snapshot was taken (and
  is not still in-progress per the snapshot), **and**
- its `xmax` is invalid, or refers to a transaction that had *not* committed as of
  the snapshot (i.e., the deletion is not yet visible).

The commit status of an `xmin`/`xmax` is looked up in the commit log (CLOG). So in
the example above, a transaction whose snapshot was taken between txid 150 and 180
sees value `'B'`; one whose snapshot predates 150 still sees `'A'`; one after 180
sees no row at all — all reading the same heap concurrently.

**Snapshot isolation.** Repeatable Read in PostgreSQL is implemented as snapshot
isolation: one snapshot is taken at transaction start and reused for every
statement, so the transaction sees a stable, consistent view for its entire life.
Read Committed takes a fresh snapshot per statement. Serializable adds Serializable
Snapshot Isolation (SSI), which monitors read/write dependencies between concurrent
transactions and aborts one if a dangerous cycle that could violate serializability
is detected.

**Why MVCC improves concurrency.** Because writers create new versions instead of
overwriting in place, **readers never block writers and writers never block
readers**. A long analytical query can scan a table for minutes while OLTP writes
proceed unblocked; each simply sees the versions consistent with its snapshot. Only
two writers attempting to modify the *same* row contend (via a row lock). This is a
large concurrency win over lock-based schemes where reads and writes fight over
shared locks. The price is that obsolete versions accumulate — which is exactly
what VACUUM exists to clean up.

### VACUUM

**Dead tuples.** Once no active or future snapshot can see a tuple version (its
deleting/updating transaction is committed and older than every running
transaction's snapshot), that version is **dead** — pure garbage occupying space.
MVCC produces dead tuples as a normal byproduct of every `UPDATE` and `DELETE`.

**Storage bloat.** If dead tuples were never reclaimed, tables and indexes would
grow without bound: a table updated repeatedly could occupy many times the space of
its live data, slowing every scan because the engine reads dead rows only to
discard them by visibility check. This is **bloat**.

**What VACUUM does.** VACUUM scans relations and marks the space occupied by dead
tuples as reusable (recorded in the free space map), so future inserts/updates can
fill it. Plain VACUUM does not usually return space to the OS (the file stays the
same size but has reusable holes); `VACUUM FULL` rewrites the table compactly and
shrinks the file, but it takes an exclusive lock and is therefore disruptive.
VACUUM also updates the **visibility map** (enabling index-only scans), and it
performs **freezing** — rewriting very old `xmin` values to a special frozen marker
to prevent transaction-ID *wraparound*, since the 32-bit txid space is finite and
must be recycled safely.

**Autovacuum.** Manually scheduling VACUUM is error-prone, so PostgreSQL runs an
**autovacuum** launcher that spawns worker processes to vacuum and analyze tables
automatically once their churn (dead-tuple count relative to table size) crosses a
threshold. Autovacuum also keeps planner statistics current via ANALYZE.

**Why cleanup is necessary.** VACUUM is not a flaw or an afterthought — it is the
*structural counterpart* to MVCC. Because the concurrency model deliberately leaves
old versions behind to avoid blocking, something must later collect that garbage and
guard against txid wraparound. A PostgreSQL instance where autovacuum cannot keep up
will suffer bloat, degrading performance, and ultimately a forced shutdown to
prevent wraparound data loss. Understanding MVCC means accepting VACUUM as part of
the deal.

### Write-Ahead Logging (WAL)

**The core rule.** Before any change to a data page is allowed to reach disk,
a record describing that change must first be written and flushed to the
write-ahead log. Log first, then data — hence "write-ahead."

**WAL records.** Every modification (insert, update, delete, index change, page
split, etc.) generates a compact WAL record describing how to redo that change. WAL
records are appended sequentially to log segment files in `pg_wal/`, each tagged with
a monotonically increasing Log Sequence Number (LSN). Each data page also stores the
LSN of the last WAL record that modified it, which links the two.

**WAL buffer.** WAL records are first written into a small ring of WAL buffers in
shared memory, then flushed to disk. On `COMMIT`, the WAL up to that transaction's
commit record is flushed with `fsync` to durable storage — this flush is the moment
the transaction becomes durable. Crucially, only the (small, sequential) log is
forced to disk at commit time; the (large, scattered) data pages can be written
lazily afterward.

**Checkpoints.** A checkpoint is a point at which the checkpointer guarantees that
all dirty buffers up to a certain LSN have been flushed to the data files. It writes
a checkpoint record to WAL and updates control information. Checkpoints bound how
much WAL must be replayed after a crash: recovery need only start from the last
checkpoint, not the beginning of time. They are spread out over time to avoid I/O
spikes.

**Crash recovery.** After an unclean shutdown, PostgreSQL performs REDO recovery:

```
   crash
     |
     v
   find last checkpoint LSN in control file
     |
     v
   replay WAL forward from that LSN:
     for each record, if the target page's stored LSN < record LSN,
        re-apply the change (idempotent redo)
     |
     v
   transactions without a commit record are simply never made visible (MVCC),
   so uncommitted work disappears automatically
     |
     v
   database is consistent; accept connections
```

Because redo is idempotent (guarded by the per-page LSN), replaying a record that
was already applied is harmless.

**Why WAL is written before data pages.** Consider the alternative: writing the
data page first, then the log. If the system crashes between the two, the data file
holds a change with no log record describing it — and if that change was part of an
uncommitted or torn write, there is no way to know or undo it. By forcing the log
first, PostgreSQL guarantees that for *any* change that might be on a data page,
there is already a durable description of it in WAL, so recovery can always
reconstruct a consistent state. WAL-first is what permits the lazy, batched,
sequential write strategy that makes the whole engine fast while remaining
crash-safe. As a bonus, the same WAL stream feeds streaming replication and
point-in-time recovery.

---

## 4. Design Trade-Offs

**MVCC.**

| Advantages                                              | Disadvantages                                                  |
|---------------------------------------------------------|---------------------------------------------------------------|
| Readers don't block writers and vice versa             | Dead tuples accumulate → bloat, requiring VACUUM              |
| Consistent snapshots without read locks                | Updates are delete+insert → more write amplification          |
| Long reads coexist with heavy OLTP writes              | All non-HOT index entries must be added for the new version   |
| Natural fit for high read/write concurrency            | 32-bit txid space forces freezing / wraparound management     |

**WAL: overhead vs durability.**

| Cost of WAL                                          | What it buys                                              |
|-----------------------------------------------------|----------------------------------------------------------|
| Every change written twice (log + eventual page)    | Crash recovery: committed data always survives           |
| `fsync` on commit adds latency                      | Durability without forcing random data-page writes       |
| WAL volume to store/archive                          | Streaming replication, PITR, physical backups            |

The trade is deliberate: WAL converts the expensive part of durability (random data
writes) into a cheap sequential append, paying a modest, tunable overhead
(`synchronous_commit`, `wal_compression`, commit grouping) for a strong guarantee.

**Buffer manager.**

| Decision                         | Benefit                              | Cost / risk                                  |
|----------------------------------|--------------------------------------|----------------------------------------------|
| Large `shared_buffers`           | More cache hits, less disk I/O       | Less RAM for OS cache / work_mem; double-buffering with OS cache |
| Lazy (deferred) writes           | Batches and orders writes            | Dirty data lost on crash without WAL (hence WAL) |
| Clock-sweep replacement          | Cheap, scalable under concurrency    | Approximate LRU — can mis-evict in edge cases |

**Index maintenance costs.**

| Aspect                | Benefit of indexes               | Cost of indexes                                      |
|-----------------------|----------------------------------|-----------------------------------------------------|
| Reads                 | Fast lookups, ranges, ordering   | —                                                   |
| Writes                | —                                | Each insert/update may touch every index; page splits |
| Storage               | —                                | Indexes consume significant disk; also bloat and need vacuuming |
| MVCC interaction      | —                                | Non-HOT updates add a new index entry per version    |

Every index speeds reads but taxes writes and storage; the *number* of indexes is a
direct read/write trade-off. (HOT — heap-only tuple — updates mitigate this when an
update changes no indexed column and the new version fits on the same page, letting
PostgreSQL skip new index entries.)

**VACUUM.**

| Benefits                                          | Drawbacks                                                |
|---------------------------------------------------|----------------------------------------------------------|
| Reclaims dead-tuple space, controls bloat         | Consumes I/O and CPU while running                        |
| Updates visibility map → enables index-only scans | Plain VACUUM doesn't return space to OS                   |
| Prevents txid wraparound (freezing)               | `VACUUM FULL` shrinks files but takes an exclusive lock   |
| Keeps statistics fresh (with ANALYZE)             | Mistuned autovacuum either lags (bloat) or steals I/O     |

---

## 5. Experiments / Observations

### EXPLAIN ANALYZE example

Consider a two-table join: orders joined to customers, filtered by region.

```sql
EXPLAIN ANALYZE
SELECT c.name, count(*)
FROM orders o
JOIN customers c ON c.id = o.customer_id
WHERE c.region = 'EU'
GROUP BY c.name;
```

A representative plan shape (exact costs/timings vary by data and hardware):

```
HashAggregate  (cost=… rows=… )  (actual time=… rows=… loops=1)
  Group Key: c.name
  ->  Hash Join  (cost=… )  (actual time=… rows=… loops=1)
        Hash Cond: (o.customer_id = c.id)
        ->  Seq Scan on orders o   (actual rows=… loops=1)
        ->  Hash  (actual rows=… loops=1)
              ->  Index Scan using customers_region_idx on customers c
                    Index Cond: (region = 'EU')
                    (actual rows=… loops=1)
```

**How to read it.**

- *Execution plan / planner decisions.* The planner chose a **Hash Join**: it builds
  an in-memory hash table on the smaller, filtered side (`customers` where
  region='EU', reached via an **Index Scan** because the filter is selective) and
  probes it with a **Seq Scan** of `orders` (scanned fully because the join needs all
  orders and no selective filter applies to it). Had `orders` also been filtered
  selectively, a Nested Loop with an index lookup might have won; had both sides been
  large and sorted, a Merge Join might have been cheaper. The choice is *cost-driven*.
- *Cost estimates.* The `cost=start..total` numbers are the planner's unit-less
  estimate (relative, anchored to `seq_page_cost`), used only to compare candidate
  plans — not milliseconds.
- *Actual execution statistics.* With `ANALYZE`, each node reports `actual time`,
  `rows`, and `loops` from really running the query. The single most useful check is
  comparing estimated `rows` to actual `rows`: a large divergence means the planner
  mis-estimated selectivity and may have picked a poor plan (e.g., chose a hash join
  expecting 100 rows but got 1,000,000). `BUFFERS` can be added to see cache hits vs
  disk reads per node.

The reasoning lesson: the plan is an emergent consequence of statistics + cost
model + available access paths, not a fixed recipe. Changing an index, the data
distribution, or the statistics can flip the chosen strategy.

### Statistics

**pg_statistic / pg_stats.** The planner cannot estimate costs without knowing the
data's shape. `ANALYZE` (run manually or by autovacuum) samples each table and
stores per-column statistics in the system catalog `pg_statistic` (exposed readably
via the `pg_stats` view): the fraction of NULLs, the number of distinct values
(`n_distinct`), the **most common values** and their frequencies (the MCV list),
and a **histogram** of value distribution for range estimation, plus physical
correlation between row order and column order.

**Cardinality estimation.** From these, the planner estimates how many rows each
operation will produce — the *cardinality*. For `region = 'EU'` it consults the MCV
list/histogram to estimate the matching fraction; for a join it combines per-column
distinct counts to estimate result size. These estimates drive every downstream
decision: which scan method, which join algorithm, join order, and whether a sort or
hash fits in `work_mem`.

**Why statistics matter.** Cardinality errors compound multiplicatively through a
plan: a wrong estimate at a low-level scan can cause the planner to pick a nested
loop that executes millions of times instead of a hash join, turning a
sub-second query into minutes. This is why stale statistics (after a bulk load, or
when autovacuum lags) are a common cause of sudden plan regressions, and why
`ANALYZE` after large data changes is standard practice. Extended statistics
(`CREATE STATISTICS`) exist for cases where columns are *correlated* and the
default independence assumption produces bad estimates.

---

## 6. Key Learnings

- **How pages move through PostgreSQL.** Data lives in 8 KB pages on disk. The
  buffer manager reads them into `shared_buffers` on demand (clock-sweep evicts
  victims to make room), modifications happen in memory and mark pages dirty, and
  dirty pages are written back lazily by the background writer and checkpointer —
  but never before their WAL record is durable. The buffer manager is the layer that
  hides the memory/disk gap and enforces write-ahead ordering.
- **How MVCC works internally.** Rows are versioned with `xmin`/`xmax`; writes
  create new versions rather than overwriting; each transaction reads through a
  snapshot that, via the commit log, determines which versions are visible. This is
  why readers and writers don't block each other.
- **Why WAL exists.** To make durability cheap and crashes survivable: by logging
  changes sequentially before touching data files, PostgreSQL can defer and batch the
  expensive random data writes while guaranteeing that committed work can always be
  replayed and uncommitted work discarded.
- **Why VACUUM is required.** MVCC's non-blocking concurrency is paid for in dead
  tuples; VACUUM (driven by autovacuum) reclaims that space, maintains the visibility
  map, refreshes statistics, and freezes old transaction IDs to prevent wraparound.
  It is the structural counterpart to MVCC, not an optional add-on.
- **Architectural lessons.** PostgreSQL's design is a coherent web of trade-offs:
  almost every "feature" has a matching "cost" subsystem (MVCC ↔ VACUUM; lazy
  buffered writes ↔ WAL; rich indexing ↔ write/maintenance overhead; cost-based
  planning ↔ dependence on fresh statistics). Understanding the system means seeing
  those pairings — knowing not just what each component does, but what other
  component exists to pay for it.

---

## References

1. PostgreSQL Global Development Group — *PostgreSQL Documentation*: "Internals",
   "Concurrency Control (MVCC)", "Write-Ahead Logging (WAL)", "Routine
   Vacuuming", and "Using EXPLAIN". https://www.postgresql.org/docs/current/
2. H. Suzuki — *The Internals of PostgreSQL* (buffer manager, heap, WAL, VACUUM,
   process architecture). https://www.interdb.jp/pg/
3. P. Lehman, S. B. Yao — "Efficient Locking for Concurrent Operations on B-Trees",
   ACM Transactions on Database Systems, 1981.
4. M. Stonebraker, L. A. Rowe — "The Design of POSTGRES", UC Berkeley, 1986.
5. M. A. Olson, M. Stonebraker et al. — POSTGRES storage system papers.
6. PostgreSQL source tree — `src/backend/storage/buffer/`, `src/backend/access/nbtree/`,
   `src/backend/access/heap/`, `src/backend/access/transam/` (WAL/CLOG).
7. PostgreSQL Documentation — "Planner/Optimizer", "Statistics Used by the Planner",
   and `pg_statistic` / `pg_stats` catalog references.
