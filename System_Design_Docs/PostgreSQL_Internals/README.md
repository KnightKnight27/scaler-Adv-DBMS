# PostgreSQL Internal Architecture

## 1. Problem Background

PostgreSQL needs to give many concurrent clients a consistent, durable view of
shared data while a single set of files sits on disk. Three hard sub-problems
fall out of that requirement, and three subsystems exist specifically to solve
them: the **buffer manager** (don't go to disk for every read), **MVCC** (let
readers and writers coexist without blocking each other), and **WAL** (survive
a crash without losing committed work). The B-Tree implementation (`nbtree`)
exists because almost every non-trivial query needs faster-than-sequential
access to rows, and the query planner needs accurate statistics to decide how
to use those indexes well. These pieces aren't independent features — they are
interlocking solutions to "give correct, fast, durable access to shared
data," and understanding one in isolation without the others misses why each
is shaped the way it is.

## 2. Architecture Overview

```
                     ┌─────────────────────────────────────────────┐
                     │                Backend Process              │
                     │   Parser → Rewriter → Planner → Executor    │
                     └───────────────┬───────────────┬─────────────┘
                                     │ reads/writes  │ emits WAL records
                                     ▼               ▼
        ┌────────────────────────────────┐  ┌─────────────────────────┐
        │     Shared Buffer Pool         │  │      WAL Buffers        │
        │  (shared_buffers, 8KB pages)   │  │  (in-memory, flushed by │
        │  pages pinned/evicted via      │  │  WAL writer / on commit)│
        |                                |  |                         |
        │  clock-sweep replacement       |  └────────────┬────────────┘
        └───────────────┬────────────────┘               │
                        │ bgwriter / checkpointer flush  │ fsync
                        ▼                                ▼
                ┌───────────────────┐                ┌────────────────────┐
                │   Heap & Index    │                │   WAL Segments     │
                │   data files      │                │   (pg_wal/)        │
                └───────────────────┘                └────────────────────┘
```

Data flows in one direction for reads (disk → buffer pool → backend) and in
two directions for writes (backend → WAL buffer → WAL file *first*, backend →
buffer pool page marked dirty → eventually flushed to the data file). The WAL
file is always ahead of the data file — that ordering is the entire basis of
crash recovery.

## 3. Internal Design

### 3.1 Buffer Manager (`src/backend/storage/buffer/`)

The buffer manager mediates every page access between a backend and disk. Its
job is to answer "is this page already in shared memory?" cheaply, and to
decide what to evict when it isn't.

- **Shared buffers** is a fixed-size array of buffer slots, each holding one
  8KB page plus a buffer descriptor (tag, pin count, dirty bit, usage count).
- **Lookup** goes through a buffer hash table (`BufTable`) keyed by
  `(relfilenode, fork, blocknum)`, partitioned across multiple lock partitions
  to reduce contention from many backends hashing concurrently.
- **Pinning**: a backend that wants to use a page increments its pin count so
  it can't be evicted mid-use; pins are released when the backend is done with
  the page (not necessarily at transaction end).
- **Replacement** uses a **clock-sweep** algorithm — a circular pointer walks
  the buffer array, decrementing each unpinned buffer's usage count, and
  evicting the first buffer it finds with usage count zero. This approximates
  LRU at much lower cost (no need to maintain a linked list under every
  access), at the price of being only an approximation.
- **Dirty pages** are written back lazily by the **background writer**, not
  synchronously on every change — this decouples "the page changed in memory"
  from "the page is safely on disk," which is exactly why WAL must be flushed
  first (see 3.4): if the system crashes before bgwriter flushes a dirty page,
  WAL replay reconstructs the change.
- **Checkpointer** periodically forces a known-consistent point: all WAL up to
  the checkpoint's LSN is guaranteed reflected on disk (or replayable), which
  bounds how far back crash recovery ever needs to scan.

This page movement is the literal answer to "how do pages move through the
buffer manager": disk → buffer pool on first touch → pinned/used by backends →
marked dirty on write → unpinned → eventually written back by bgwriter or
checkpointer → evicted by clock-sweep when space is needed for a new page.

#### Page Flow Through the Buffer Manager


#### Page Flow Through the Buffer Manager

```text
Disk Page
    ↓
Shared Buffer
    ↓
Pinned by Backend
    ↓
Modified (Dirty)
    ↓
BgWriter / Checkpointer
    ↓
Disk
```

This flow summarizes how PostgreSQL pages move through memory before being written back to disk.


### 3.2 B-Tree Implementation (`nbtree`)

Postgres's default index type is a balanced B+-tree variant tuned for
concurrent access:
- **Page layout**: each B-tree page (also 8KB) has a header, an array of index
  tuples (key + TID pointing to the heap), and "high key" metadata on
  non-rightmost pages used during concurrent splits.
- **Search**: descend from the root, doing a binary search within each page on
  the key, following the appropriate downlink, until reaching a leaf; at the
  leaf, the matching index tuples' TIDs are returned (or used directly for
  Index-Only Scans if the visibility map says the heap page is all-visible).
- **Insert**: find the correct leaf, insert in sorted order; if the page lacks
  room, a **page split** occurs — roughly half the tuples move to a new page,
  a new separator key is inserted into the parent, and this can cascade
  upward if the parent is also full (this is how the tree grows in height).
- **Concurrency**: nbtree uses a right-link + high-key protocol (derived from
  the Lehman–Yao algorithm) so readers can detect and follow concurrent splits
  without taking a tree-wide lock — only the pages actually being modified are
  locked, which is essential since many backends search the same index
  simultaneously.

### 3.3 MVCC

#### Heap Tuple Versioning

Every heap tuple carries hidden system columns: `xmin` (the transaction ID
that created it) and `xmax` (the transaction ID that deleted/updated it, or
zero if still live).
- An `UPDATE` never overwrites a tuple in place. It marks the old tuple's
  `xmax` with the updating transaction's ID and inserts a brand new tuple
  with that transaction as its `xmin`.

#### Visibility Rules

- **Visibility** is determined by comparing a tuple's `xmin`/`xmax` against the
  querying transaction's **snapshot** — a record of which transactions were
  in-progress, committed, or aborted at the moment the snapshot was taken.
  A tuple is visible if its creating transaction is committed-and-before-the-
  snapshot and its deleting transaction (if any) is not committed-and-before
  the snapshot.
- This is why **VACUUM is necessary**: every update leaves behind a dead tuple
  version that is no longer visible to anyone but still occupies page space.
  Without reclamation, heap files would grow unboundedly. VACUUM scans for
  tuples whose `xmax` predates the oldest snapshot any transaction in the
  system could still need, and marks that space reusable. VACUUM also updates
  the **visibility map** (enabling Index-Only Scans) and prevents transaction
  ID wraparound by freezing very old tuples.

#### Snapshot Isolation

- **Snapshot isolation** falls directly out of this design: a transaction's
  snapshot is fixed (for `REPEATABLE READ`/`SERIALIZABLE`) or re-taken per
  statement (`READ COMMITTED`), and visibility rules are purely a function of
  comparing transaction IDs — no locks are needed for readers to avoid
  blocking on writers, which is the central payoff of MVCC.

### 3.4 WAL (Write-Ahead Logging)

- **WAL records** describe a physical or logical change to a page (e.g., "set
  these bytes at this offset on this page") and are appended to an in-memory
  WAL buffer, then flushed to the WAL segment file on disk.
- **The fundamental rule**: a data page must never be flushed to disk *before*
  the WAL record describing the change that produced it has been flushed.
  This is enforced via the page's LSN (log sequence number) — bgwriter checks
  that the WAL up to that LSN is durable before letting the page out.
- **Durability guarantee**: once a transaction's `COMMIT` WAL record is
  fsynced, the transaction is durable, *even if the actual data pages are
  still only in the buffer pool and have not yet hit the heap file*. A crash
  at that point loses nothing, because WAL replay will redo the change.
- **Crash recovery** replays WAL starting from the last checkpoint's LSN
  forward, re-applying each record's described change to the data pages,
  bringing the database back to the exact state it was in at the moment of
  the crash (redo); aborted transactions are then rolled back logically using
  MVCC visibility rather than physical undo.
- **Checkpointing** writes all currently dirty buffers to disk and records the
  checkpoint's LSN, which bounds how much WAL must be replayed after a crash
  — without checkpoints, recovery would need to replay every WAL record since
  the database was created.

## 4. Design Trade-Offs

- **Clock-sweep vs strict LRU**: cheaper to maintain under concurrency, at the
  cost of being a coarser approximation of actual access recency.
- **MVCC's append-new-version model vs in-place update (InnoDB-style)**:
  Postgres avoids undo logs and gets simpler crash recovery (no undo phase),
  but pays for it with VACUUM overhead, table bloat, and the need to actively
  manage transaction ID wraparound.
- **Physical WAL records vs logical**: physical (page-based) WAL is simpler to
  replay deterministically but is larger and ties recovery to exact page
  layout; this is why logical replication is a separate, additional layer on
  top of physical WAL rather than the default.
- **B-tree split cost**: keeping pages balanced gives O(log n) lookups but
  splits are not free — a hot insertion key range can cause repeated splits
  and page churn, which is one reason sequential/random key choice (e.g.
  UUIDs vs serial IDs) materially affects index write performance.

## 5. Experiments / Observations

Running `EXPLAIN ANALYZE` on a multi-table join surfaces exactly how planner
estimates and statistics interact with the structures above:

```sql
EXPLAIN ANALYZE
SELECT o.id, c.name
FROM orders o
JOIN customers c ON o.customer_id = c.id
WHERE o.status = 'shipped';
```

A representative plan:

```
Hash Join  (cost=45.50..210.30 rows=1200 width=40) (actual time=2.1..14.7 rows=980 loops=1)
  Hash Cond: (o.customer_id = c.id)
  ->  Seq Scan on orders o  (cost=0.00..150.00 rows=1200 width=24) (actual time=0.02..8.5 rows=980 loops=1)
        Filter: (status = 'shipped'::text)
        Rows Removed by Filter: 9020
  ->  Hash  (cost=30.00..30.00 rows=2000 width=24) (actual time=1.9..1.9 rows=2000 loops=1)
        ->  Seq Scan on customers c  (cost=0.00..30.00 rows=2000 width=24)
```

Observations:
- The **estimated rows (1200)** vs **actual rows (980)** divergence for the
  `orders` scan comes directly from `pg_statistic` — specifically the
  most-common-values list and histogram bounds collected by `ANALYZE` for the
  `status` column. The planner doesn't know the true selectivity of
  `status='shipped'`; it estimates it from a sampled histogram.
- The planner chose a **Seq Scan** over an index scan on `orders.status`
  because the estimated selectivity (1200/10000 rows ≈ 12%) is high enough
  that random-access index lookups would cost more than one sequential
  pass — this cost crossover is computed using `random_page_cost` and
  `seq_page_cost` settings combined with the row estimate.
- If `status` were indexed and far more selective (e.g. filtering to 0.1% of
  rows), the plan would flip to an Index Scan — re-running `EXPLAIN ANALYZE`
  after `CREATE INDEX` and `ANALYZE` makes this flip directly observable, and
  is a good way to see the planner's cost model react to changed statistics
  rather than changed data.

## 6. Key Learnings

- The buffer manager, MVCC, and WAL aren't separable features — WAL exists
  specifically because the buffer manager defers writing dirty pages, and
  MVCC's append-only versioning is what makes "redo-only" crash recovery
  sufficient (no need for physical undo of in-place changes).
- VACUUM is not an optional maintenance task; it's the other half of MVCC.
  Any system using append-and-mark-dead versioning *must* have a reclamation
  mechanism, or storage grows unbounded.
- Query planning is a statistical, not exact, process — the planner is making
  cost-based bets using sampled statistics (`pg_statistic`), and visible
  symptoms like "wrong plan chosen" are very often a stale-statistics problem,
  not a query-logic problem.
- B-tree concurrency protocols (Lehman–Yao-style right-links) are what let
  searches proceed safely during concurrent splits without locking the whole
  tree — a detail that matters a lot under high write concurrency on indexed
  columns.

## Architectural Lessons

PostgreSQL demonstrates how modern database systems separate concerns between memory management, concurrency control, indexing, and recovery.

The buffer manager reduces disk access, MVCC enables non-blocking concurrency, WAL guarantees durability, and B-Trees provide efficient data access. Each subsystem solves a specific problem, but their interaction is what enables PostgreSQL to achieve both correctness and performance.

The most important lesson is that database internals are composed of interconnected trade-offs rather than isolated features. PostgreSQL achieves high concurrency and durability by combining these subsystems into a cohesive architecture rather than relying on any single mechanism.

## References
- PostgreSQL source tree: `src/backend/storage/buffer/`, `src/backend/access/nbtree/`, `src/backend/access/heap/`
- PostgreSQL documentation: WAL internals, VACUUM, planner statistics chapters
- "The Internals of PostgreSQL" — Hironobu Suzuki