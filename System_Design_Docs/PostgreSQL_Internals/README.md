# PostgreSQL Internal Architecture

> Advanced DBMS · System Design Discussion · Topic 2

---

## 1. Problem Background

A relational database has to satisfy a handful of demands that fight each other. It has to serve
many concurrent transactions without letting them corrupt each other's view of the data. It has
to survive a power loss the instant after acknowledging a commit. It has to keep hot data fast to
reach even though it lives on a comparatively slow disk. And it has to find a row out of millions
in roughly logarithmic time. These pull in opposite directions. Durability wants every change
flushed right away; performance wants writes batched and deferred. Concurrency wants long-lived
consistent views; storage efficiency wants stale data thrown away as soon as possible.

PostgreSQL is interesting because of *how* it settles those tensions, which is what this document
is about. Its lineage goes back to the POSTGRES project at Berkeley under Michael Stonebraker in
the mid-1980s. The central idea there was that a "no-overwrite" storage model, never updating a
row in place, only ever appending new versions, could deliver concurrency and recoverability more
cleanly than the lock-heavy, update-in-place designs of the time. That one commitment shows up
almost everywhere below. It's why PostgreSQL needs multi-version concurrency control, why it needs
`VACUUM`, why readers and writers don't block each other, and why its tuple headers carry
transaction ids.

The write-up digs into four subsystems that together carry that philosophy: the **Buffer Manager**
(the in-memory cache over disk), the **B-tree** (`nbtree`, the workhorse index), **MVCC** (the
no-overwrite concurrency model), and **WAL** (the write-ahead log that makes the whole thing
crash-safe). Source references point into the PostgreSQL backend tree (`src/backend/...`).

---

## 2. Architecture Overview

PostgreSQL runs as a process-per-connection server: a supervisor (`postmaster`) forks a dedicated
backend process for each client. Those backends don't share memory by default, with one
exception, a single large shared region the whole system is built around. That region holds the
buffer pool, the WAL buffers, the lock tables, and the structures that coordinate visibility
between transactions.

A query's life touches every subsystem in this document:

```
        SQL text
           │
     ┌─────▼──────┐   parse / analyze / rewrite
     │  Parser    │
     └─────┬──────┘
     ┌─────▼──────┐   cost-based planning using pg_statistic
     │  Planner   │◄──────────────── ANALYZE gathers stats
     └─────┬──────┘
     ┌─────▼──────┐   pulls tuples via access methods
     │  Executor  │
     └─────┬──────┘
   ┌───────┼────────────┐
   │       │            │
┌──▼──┐ ┌──▼────┐  ┌─────▼───────┐
│Heap │ │B-tree │  │   MVCC      │  visibility check on every tuple
│ AM  │ │(nbtree)│  │ (xmin/xmax) │
└──┬──┘ └──┬────┘  └─────────────┘
   │       │
┌──▼───────▼───────────────────────────────┐
│        Buffer Manager (shared_buffers)     │  8 KB pages cached in RAM
└──────────────┬─────────────────────────────┘
   ┌───────────┼──────────────────┐
   │           │                  │
┌──▼──┐   ┌────▼────┐       ┌──────▼──────┐
│ WAL │   │bgwriter │       │ checkpointer│
│fsync│   │(trickle │       │ (periodic   │
│on   │   │ flush)  │       │  full flush)│
│COMMIT   └─────────┘       └─────────────┘
└──┬──┘
   │
┌──▼─────────────────────┐
│ Disk: data files + WAL  │
└─────────────────────────┘
```

The relationships worth holding onto: the **executor** never touches disk directly, it asks the
**buffer manager** for pages. The buffer manager caches 8 KB pages in `shared_buffers`. Any change
to a buffered page has to be described in a **WAL record** that reaches disk before the dirty page
does. **MVCC** sits across all data access, deciding which tuple versions a given transaction is
even allowed to see. And the **B-tree** is an access method built on the same buffer and WAL
machinery; its pages are just 8 KB blocks with a specialized layout.

---

## 3. Internal Design

### 3.1 Buffer Manager — `src/backend/storage/buffer/`

The buffer manager is the layer that lets PostgreSQL pretend disk is fast. It owns
`shared_buffers`, a fixed array of 8 KB *buffer frames* allocated at startup, each frame holding
one disk page. Three data structures coordinate them:

1. **The buffer pool** — the raw array of page-sized frames (`BufferBlocks`).
2. **The buffer descriptor table** — one descriptor per frame, holding metadata: which page it
   currently holds (its *buffer tag*), a pin count, a dirty flag, a `usage_count`, and a content
   lock.
3. **The buffer mapping hash table** — maps a *buffer tag* `(relfilenode, fork number, block
   number)` to the frame index currently holding that page. This is how a backend answers "is
   page X already in memory, and where?".

The buffer tag is a page's identity. `relfilenode` names the physical file of a relation, the
fork number distinguishes the main data fork from the free-space map and visibility map, and the
block number is the page offset within that fork.

**Pinning** is reference counting. Before a backend reads or writes a page it pins the buffer
(increments the pin count), which guarantees the page won't be evicted out from under it. When
it's done it unpins. A buffer with pin count > 0 can't be evicted.

#### Page request flow

```
  Backend wants page (rel, fork, blocknum)
                │
                ▼
  ┌──────────────────────────────────┐
  │ Compute buffer tag, look up in     │
  │ buffer mapping hash table          │
  └───────────────┬────────────────────┘
          ┌────────┴─────────┐
        HIT                  MISS
          │                   │
          ▼                   ▼
  ┌──────────────┐   ┌─────────────────────────┐
  │ Pin buffer,  │   │ Run clock-sweep to find  │
  │ usage_count++│   │ a victim frame           │
  │ return it    │   └───────────┬──────────────┘
  └──────────────┘               ▼
                      ┌─────────────────────────┐
                      │ Victim dirty? Flush it   │
                      │ (WAL already on disk)    │
                      └───────────┬──────────────┘
                                  ▼
                      ┌─────────────────────────┐
                      │ Read page from disk into │
                      │ frame, update hash table,│
                      │ pin, return              │
                      └─────────────────────────┘
```

#### Replacement: clock-sweep, not LRU

True LRU would have to reorder a shared linked list on every single page access, and that list
head turns into a contention hotspot under concurrency, with every backend fighting for the same
lock. PostgreSQL uses **clock-sweep** instead, an LRU approximation that needs no global ordering.

Each descriptor carries a small `usage_count`, capped (typically at 5). On every access the count
gets bumped up. A shared "clock hand" sweeps in a circle through the descriptors. At each frame:
if `usage_count > 0`, decrement it and move on (a second chance); if `usage_count == 0` and the
buffer is unpinned, evict it. Hot pages keep getting re-bumped and survive; cold pages decay to
zero and get reclaimed. An access is O(1) and only touches the local descriptor, so it scales
across cores far better than list-based LRU.

#### Dirty pages and who flushes them

A page modified in memory is marked **dirty** and is *not* written back right away; doing that
would throw away the whole point of caching. Two background processes drain dirty pages so
foreground backends rarely have to:

- The **background writer (`bgwriter`)** continuously trickles a few dirty buffers to disk ahead
  of the clock hand, so when a backend needs a victim it more often finds a clean one and skips a
  synchronous write.
- The **checkpointer** periodically flushes *all* dirty buffers and writes a checkpoint record
  (see WAL).

Before any dirty page is written, the WAL records describing its changes have to be on disk
already (the write-ahead rule). The buffer manager enforces that ordering on flush.

#### Double buffering vs the OS page cache

PostgreSQL reads and writes through ordinary buffered file I/O, so a page can sit in *both*
`shared_buffers` and the OS page cache at the same time. That's double buffering, and yes, it
wastes some RAM. The design accepts the waste on purpose. It keeps PostgreSQL portable, with no
dependence on `O_DIRECT` behaving the same way across platforms, and it lets the OS handle
readahead and writeback heuristics. The practical fallout is the sizing rule in §4: you don't hand
`shared_buffers` all your RAM, because the OS cache is a useful second tier.

### 3.2 B-tree — `src/backend/access/nbtree/`

PostgreSQL's default index is a B+tree following the **Lehman & Yao (1981)** design with later
refinements. It's a B+tree (all entries with payloads live in the leaves, internal pages are pure
routing), and the Lehman-Yao twist is what makes it friendly to concurrency.

#### Page layout

Every btree page is a standard 8 KB block, but its *special area* (the tail region every
PostgreSQL page reserves) holds btree-specific metadata: `btpo` flags marking the page as leaf or
internal, left and right sibling pointers, and a **high key**. The high key is an upper-bound
separator: every key on the page is guaranteed `<=` the high key. The index tuples store the
indexed key plus a **heap TID** (`(block, offset)`) pointing at the actual row in the table's
heap. The leaves form a doubly-linked sequence through the sibling links, which is what makes
ordered range scans cheap.

#### Search path

A search starts at the root and descends. At each internal page it binary-searches the separator
keys to pick the child subtree, follows the downlink, and repeats until it reaches a leaf, then
binary-searches the leaf for the key. Logarithmic in the number of entries.

#### Splits and the right-link insight

The hard problem in concurrent B-trees is the **page split**. When a leaf overflows on insert it
has to split into two, push a new separator key up to the parent, and maybe cascade splits
further up. Done naively, a searcher arriving mid-split could follow a stale downlink to a page
that no longer holds its key.

Lehman & Yao solve this with the high key plus the right-link. A split happens bottom-up: the new
right page is created and linked in through the right-link *before* the parent's downlink is
updated. So even if a concurrent searcher reaches a page whose high key is now *smaller* than its
target key (meaning the data it wants migrated to the new right sibling during the split), it just
notices "my key > this page's high key" and walks right along the sibling link to find it. The
searcher never has to lock the whole tree, or even the parent. It corrects itself locally.

That's the payoff: searches run concurrently with splits because the right-link guarantees any
key, even one mid-migration, is always reachable by moving rightward. Only the individual pages
being modified take short-lived locks; there's no global lock on the structure.

```
Before split (leaf full):        After split:
┌───────────────────┐            ┌───────────┐ right ┌──────────┐
│ k1 k2 k3 k4 k5 ...│            │ k1 k2 k3  │──────▶│ k4 k5 ...│
│ highkey = ∞       │            │ highkey=k4│       │ highkey=∞│
└───────────────────┘            └───────────┘       └──────────┘
                                  parent downlink to right page
                                  added *after* the link exists
```

### 3.3 MVCC — Multi-Version Concurrency Control

This is the heart of the no-overwrite philosophy. PostgreSQL never changes a live row in place.
The visibility of each row version is governed by transaction ids stamped into the **heap tuple
header**.

#### Heap tuple header

```
┌────────────────────────────────────────────────────────────┐
│ HeapTupleHeader                                              │
├───────────┬───────────┬──────────┬──────────┬───────────────┤
│  t_xmin   │  t_xmax   │  t_cid / │  t_ctid  │  t_infomask   │
│(inserting │ (deleting │  t_xvac  │ (block,  │  (hint bits)  │
│   xid)    │   xid; 0  │          │  offset) │ cached commit │
│           │ if live)  │          │ self or  │   status of   │
│           │           │          │ next ver │  xmin / xmax  │
└───────────┴───────────┴──────────┴────┬─────┴───────────────┘
                                         │
                                         ▼
                            points to the *newer* version
                            of this row after an UPDATE
```

- **`t_xmin`** — the transaction id that inserted this version.
- **`t_xmax`** — the transaction id that deleted or updated (and so superseded) this version; 0 or
  invalid if the version is still live.
- **`t_ctid`** — normally points to the tuple itself; after an UPDATE it points to the next
  version, forming an update chain.
- **`t_infomask`** — hint bits caching the commit/abort status of `xmin`/`xmax`, so visibility
  checks don't have to keep consulting the commit log (`pg_xact`/CLOG).

#### UPDATE is DELETE + INSERT

An `UPDATE` doesn't overwrite. It writes a new tuple version (with a fresh `xmin`, the updating
transaction) and stamps the old version's `xmax` with that same id, pointing the old version's
`ctid` at the new one. The old version becomes a **dead tuple** once no transaction can still see
it. That's the whole reason tables build up bloat and need `VACUUM`. An update is a delete plus an
insert under the hood, and that one fact explains most of what follows.

#### Snapshots and visibility rules

A **snapshot** captures which transactions had committed at a point in time: roughly, the highest
assigned xid plus the list of xids still in progress. A tuple version is visible to a transaction
when its `xmin` committed before the snapshot *and* its `xmax` is either invalid or belongs to a
transaction the snapshot can't see (the deletion hasn't "happened" yet from this transaction's
point of view). Loosely: the insert is committed-and-visible, the delete is not-yet-visible.

- Under **READ COMMITTED** (the default), each *statement* takes a fresh snapshot, so a
  transaction sees data others commit while it runs. Non-repeatable reads are possible.
- Under **REPEATABLE READ**, the snapshot is taken once at the start of the transaction and
  frozen, giving a stable view for the whole transaction (snapshot isolation).

The defining property: because writers create new versions instead of overwriting, and readers
consult their own snapshot, readers don't block writers and writers don't block readers. A long
analytical query reads the versions visible to its snapshot while concurrent updates create new
ones alongside. Only writer-vs-writer conflicts on the same row need waiting, through row locks.

#### Why VACUUM is necessary

The no-overwrite model has a bill: dead tuples pile up. `VACUUM` is the garbage collector that
pays it. It does four jobs:

1. **Reclaim dead tuples** — space from versions no snapshot can see is freed for reuse, which
   keeps table bloat from growing without bound.
2. **Update the Free Space Map (FSM)** — records where reusable space is, so future inserts can
   fill gaps instead of always extending the file.
3. **Update the Visibility Map (VM)** — marks pages where every tuple is visible to everyone. This
   is what enables **index-only scans**: if the VM says a page is all-visible, the executor can
   trust the index TID without visiting the heap to recheck visibility.
4. **Freeze old xids** — transaction ids are 32-bit and wrap around. Without intervention, ancient
   committed tuples would suddenly look like they're "in the future" after a wraparound. `VACUUM`
   freezes old tuples (marks them permanently visible), advancing the frozen horizon and heading
   off catastrophic **transaction ID wraparound**.

### 3.4 WAL — Write-Ahead Logging — `src/backend/access/transam/`

WAL is what makes everything above durable and crash-safe without flushing data pages
synchronously. The rule is in the name: the log record describing a change must reach stable
storage before the data page it describes does (write-*ahead*).

Each modification (insert a tuple, split a btree page, and so on) generates a **WAL record**
describing the physical change to a page, tagged with a monotonically increasing **LSN** (Log
Sequence Number), effectively the byte offset of that record in the log stream. Every page also
stores the LSN of the last WAL record that modified it (`pd_lsn`). The flush rule is enforced
through LSN comparison: a dirty page may only be written once the WAL has been flushed up to that
page's LSN.

#### Durability: COMMIT fsyncs WAL, not heap pages

When a transaction commits, PostgreSQL writes a commit WAL record and `fsync`s the WAL to
guarantee it's physically on disk, then returns success to the client. It does *not* fsync the
modified heap pages; those can still be sitting dirty in `shared_buffers`. That's the central
performance trick. WAL is written sequentially, which is cheap on any storage, while heap pages
are scattered, which means random I/O and is expensive. By making durability depend only on a
sequential log flush, commits stay fast and stay crash-safe. If the server dies, the dirty heap
pages are gone, but the WAL on disk fully describes how to rebuild them.

#### Checkpoints and crash recovery

A **checkpoint** flushes all currently dirty buffers to disk and writes a checkpoint record to
WAL. Its purpose is to bound recovery work: it sets a known-good point from which all earlier
changes are guaranteed already on the data files.

On crash recovery PostgreSQL finds the last checkpoint record and **redoes** forward, replaying
every WAL record after that checkpoint and reapplying each change to its page (skipping any whose
effect is already there, detected via the page's stored LSN ≥ the record's LSN). That rolls the
data files forward to the last committed state. Uncommitted transactions are simply never made
visible (their tuple versions fail the visibility check), so PostgreSQL's recovery is redo-only in
the common case. There's no separate undo pass over the data, because MVCC already keeps
uncommitted versions invisible.

#### WAL as the basis of replication

Because the WAL stream fully describes every change in commit order, it doubles as a
**replication** feed. Streaming replication ships WAL records to standby servers that continuously
redo them, keeping a byte-identical copy. The same mechanism that recovers from a crash keeps
replicas in sync: one stream, two uses.

---

## 4. Design Trade-Offs

Every subsystem above is a chosen point on a spectrum. The interesting engineering is in what got
traded away.

**Clock-sweep vs true LRU.** LRU is provably better at predicting which page to evict, but it has
to mutate a shared ordered list on every access, and that list's lock is a serialization point
that strangles throughput on many-core machines. Clock-sweep accepts a worse eviction decision
(it only approximates recency via `usage_count`) in exchange for O(1), mostly lock-free accesses.
On a busy system the concurrency win dwarfs the occasional bad eviction. This is a recurring
PostgreSQL pattern: prefer a slightly worse algorithm that scales over an optimal one that
contends.

**MVCC bloat vs lock-free reads.** Never overwriting means readers and writers never block, a big
concurrency and latency win, especially for mixed OLTP/analytics workloads. The cost is dead
tuples. Every update and delete leaves garbage that has to be collected, tables physically grow,
indexes accumulate dead pointers, and `VACUUM` has to run forever in the background eating I/O.
For update-heavy workloads, bloat and vacuum tuning become real operational work. The trade is
"predictable read concurrency now, amortized cleanup later," and the cleanup bill (autovacuum)
isn't small.

**WAL write amplification vs durability.** Logging every change means each logical write is
written twice, once to WAL and once (eventually) to the data file. On top of that, by default the
first change to a page after a checkpoint logs the entire page image (full-page writes) to guard
against torn pages on crash. That's significant write amplification. The payoff is that durability
and crash recovery cost only a sequential fsync per commit instead of random fsyncs of scattered
data pages. You spend extra total bytes to make the latency-critical path, commit, sequential and
fast.

**`shared_buffers` sizing vs the OS cache.** Because of double buffering, a page in
`shared_buffers` may also be cached by the OS, so memory handed to `shared_buffers` is partly
taken from a cache that would help anyway. Set it too small and you miss in the buffer pool
constantly; set it too large and you both duplicate the OS cache and stretch out checkpoint
flushes (more dirty buffers to write at once, which causes I/O spikes). The common ~25%-of-RAM
heuristic is a deliberate compromise: enough resident hot data to win, while leaving the OS cache
as a free second tier.

---

## 5. Experiments / Observations — `EXPLAIN ANALYZE` on a join

Take a schema of customers, their orders, and order line items. I asked for recent orders from one
city:

```sql
EXPLAIN ANALYZE
SELECT c.name, o.order_date, oi.quantity
FROM   customers   c
JOIN   orders      o  ON o.customer_id = c.id
JOIN   order_items oi ON oi.order_id   = o.id
WHERE  c.city = 'Berlin'
  AND  o.order_date >= DATE '2026-01-01';
```

A plausible plan:

```
Hash Join  (cost=312.50..2841.10 rows=980 width=44)
           (actual time=4.102..51.339 rows=3120 loops=1)
  Hash Cond: (oi.order_id = o.id)
  ->  Seq Scan on order_items oi
           (cost=0.00..1820.00 rows=120000 width=12)
           (actual time=0.011..18.402 rows=120000 loops=1)
  ->  Hash  (cost=300.20..300.20 rows=985 width=40)
            (actual time=4.061..4.062 rows=1040 loops=1)
        ->  Nested Loop  (cost=0.42..300.20 rows=985 width=40)
                         (actual time=0.038..3.401 rows=1040 loops=1)
              ->  Index Scan using customers_city_idx on customers c
                       (cost=0.42..18.30 rows=42 width=36)
                       (actual time=0.020..0.210 rows=40 loops=1)
                    Index Cond: (city = 'Berlin'::text)
              ->  Index Scan using orders_customer_id_idx on orders o
                       (cost=0.42..6.50 rows=23 width=12)
                       (actual time=0.005..0.060 rows=26 loops=40)
                    Index Cond: (customer_id = c.id)
                    Filter: (order_date >= '2026-01-01'::date)
Planning Time: 0.612 ms
Execution Time: 53.880 ms
```

### Reading the plan

The planner chose a nested loop to combine `customers` and `orders`, then a hash join to add
`order_items`. Why that shape?

- For `customers`, the predicate `city = 'Berlin'` is selective (42 estimated rows out of
  presumably many thousands), so an index scan on `customers_city_idx` beats a seq scan.
- For each matching customer the planner expects only ~23 orders, a small inner side, so a nested
  loop with an inner index scan on `orders_customer_id_idx` is cheaper than building hash tables.
  Notice `loops=1` on the outer side but `loops=40` on the inner index scan (once per outer row).
  The loop count is the giveaway that this is a nested loop.
- `order_items` is large (120,000 rows) with no selective predicate, so the planner reads it once
  with a seq scan and probes it with a hash join built on the small customer/order result. Hash
  the small side, stream the big one. That's the textbook move when one input is large and
  unfiltered.

### Estimated vs actual rows — estimation error

The interesting column is the gap between `rows=` (the estimate) and `actual ... rows=`:

| Node | Estimated | Actual | Error |
|---|---|---|---|
| Index Scan customers | 42 | 40 | tiny |
| Index Scan orders (per loop) | 23 | 26 | small |
| Final Hash Join | 980 | 3120 | ~3.2× under |

The leaf estimates are basically perfect. The final join is underestimated by more than 3×, and
that pattern is textbook. Per-table statistics are accurate, but the combination drifts because
the planner assumes column independence when it multiplies selectivities across the join. Berlin
customers happen to place disproportionately many line items, and that correlation isn't captured
by single-column stats, so the join cardinality comes out low.

### How the planner estimates — `pg_statistic` / `pg_stats`

The planner is cost-based. It enumerates candidate plans, assigns each an estimated cost
(`cost=startup..total`, in arbitrary page/CPU units), and picks the cheapest. Those costs ride on
row-count estimates, which come from statistics that `ANALYZE` samples and stores in
`pg_statistic` (readable through the `pg_stats` view):

- **`n_distinct`** — number of distinct values in a column. Drives equality selectivity:
  `customer_id = c.id` selectivity ≈ `1 / n_distinct`.
- **`most_common_vals` / `most_common_freqs`** — the MCV list and frequencies, for skewed columns.
  `city = 'Berlin'` is estimated straight from the MCV frequency if Berlin is common.
- **`histogram_bounds`** — equi-depth buckets used for range predicates like
  `order_date >= '2026-01-01'`.
- **`correlation`** — how closely physical row order tracks sorted column order. Near ±1, index
  scans get much cheaper (sequential heap fetches) and the planner leans toward them.

### When estimates go wrong, and the fix

When estimates are badly off, the planner picks the wrong join method or order. A classic case is
choosing a nested loop expecting 100 inner rows when the truth is 100,000, which turns an O(n)
probe into a runaway O(n²) disaster. Bad cardinalities are the single most common cause of bad
plans.

Remedies:
- **Run `ANALYZE`** (or let autovacuum's analyze run) so statistics match the current data. Stale
  stats after a bulk load are a frequent culprit.
- **Raise the statistics target** (`ALTER TABLE ... ALTER COLUMN ... SET STATISTICS n`, default
  100). More histogram buckets and a longer MCV list give finer, more accurate estimates for
  skewed or high-cardinality columns, at the cost of slower `ANALYZE` and slightly slower
  planning.
- **Extended statistics** (`CREATE STATISTICS`) to capture cross-column correlation, which
  directly addresses the independence-assumption error in our join.

---

## 6. Key Learnings

The throughline of PostgreSQL's design is a willingness to accept deferred or amortized cost in
exchange for a fast, scalable common case. Commits are fast because durability rides a sequential
WAL flush, not random data-page writes; the data pages catch up lazily through the background
writer and checkpointer. Reads scale because MVCC hands each transaction its own consistent
snapshot, so readers and writers step around each other instead of queueing on locks. Eviction is
fast because clock-sweep refuses to pay LRU's contention tax. Each time, an "optimal" but
contended or synchronous alternative was rejected for a slightly looser one that runs without
serialization.

That choice is never free, and the costs are themselves a coherent set. MVCC's no-overwrite model
demands `VACUUM` to reclaim dead versions, maintain the Free Space and Visibility maps, and freeze
xids against wraparound. WAL's fast commit demands write amplification and full-page writes. The
buffer pool's portability demands tolerating double buffering with the OS cache. Understanding
PostgreSQL means seeing these as matched pairs: every shortcut on the hot path is funded by
background work somewhere else.

The query-planning exercise drove home the most practical lesson: the engine is only as good as
its statistics. The execution machinery is excellent, but it acts on the planner's cardinality
estimates, and those come from sampled stats in `pg_statistic`. Per-column stats can be near
perfect while a multi-join estimate drifts badly because of the independence assumption. Knowing
that the fix is `ANALYZE`, a higher statistics target, or extended statistics, rather than blaming
the executor, is the difference between debugging a slow query in minutes versus hours.
Architecture and operations aren't separable here. The design decisions in §3 are the things a DBA
ends up tuning in production.

---

## References

1. **PostgreSQL Official Documentation** — especially the chapters on Concurrency Control (MVCC),
   Reliability and the Write-Ahead Log, Performance Tips / Using EXPLAIN, and the planner
   statistics chapters. <https://www.postgresql.org/docs/current/>
2. **Hironobu Suzuki, *The Internals of PostgreSQL*** — the definitive freely-available deep dive
   into the buffer manager, heap/MVCC, WAL, and the btree. <https://www.interdb.jp/pg/>
3. **`src/backend/storage/buffer/README`** — in-tree design notes on buffer descriptors, pinning,
   and the clock-sweep replacement strategy.
4. **`src/backend/access/nbtree/README`** — in-tree explanation of the Lehman-Yao implementation,
   high keys, right-links, and concurrent split handling.
5. **P. L. Lehman and S. B. Yao (1981), "Efficient Locking for Concurrent Operations on B-Trees,"**
   *ACM Transactions on Database Systems* 6(4):650–670 — the foundational paper for `nbtree`'s
   lock-light concurrent B+tree.
