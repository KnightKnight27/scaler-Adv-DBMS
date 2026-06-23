# PostgreSQL Internals — Buffers, B-Trees, MVCC, and WAL

PostgreSQL's reputation for correctness comes from four subsystems working together: the
**buffer manager** decides what's in memory, **nbtree** is how you find rows quickly, **MVCC**
lets everyone read and write at once without lying to each other, and **WAL** makes sure none
of it is lost in a crash. This document walks through each and ties them to behavior you can
actually observe with `EXPLAIN ANALYZE`.

---

## 1. Problem Background

PostgreSQL grew out of Berkeley's POSTGRES (1986), built to be a *serious* multi-user database
— many concurrent transactions, strong durability, and rich extensibility. Two design choices
defined its internals: it stores rows in an unordered **heap** with versioning rather than
updating in place, and it logs every change to a **write-ahead log** before touching data
files. The buffer manager and B-tree are fairly conventional; MVCC and WAL are where the
character of the system shows.

---

## 2. Architecture Overview

```
        SQL query
           |
   parser -> rewriter -> planner/optimizer -> executor
                              |                   |
                       uses pg_statistic     reads/writes pages
                              |                   |
                              v                   v
                       +--------------------------------------+
                       |   Buffer manager (shared_buffers)    |
                       |   8 KB pages, clock-sweep eviction    |
                       +--------------------------------------+
                          |            ^              |
                     page miss      pin/unpin     dirty page
                          |            |              |
                          v            |              v
                     heap files,   nbtree index    WAL  --fsync on commit-->
                     (data on disk)  files          |
                                                checkpointer flushes dirty
                                                pages, sets redo point
```

---

## 3. Internal Design

### Buffer Manager — `src/backend/storage/buffer/`

PostgreSQL caches 8 KB pages in a fixed-size shared-memory pool sized by `shared_buffers`.
Each slot has a `BufferDesc` holding its page tag, a **pin count**, a dirty flag, and a
**usage count**. To read a page, a backend pins it (so it can't be evicted), uses it, then
unpins.

Eviction is **not** plain LRU — tracking exact access order across many backends is too much
contention. Instead it's a **clock-sweep**: a pointer cycles through buffers, decrementing
each usage count; the first buffer it finds at zero (and unpinned) gets evicted. If that
buffer is dirty, it's written out first. Frequently touched pages keep bumping their usage
count back up and survive. The `bgwriter` trickles dirty pages out ahead of time so backends
rarely have to write during eviction.

### B-Tree — `nbtree`

PostgreSQL's default index is a **Lehman & Yao B-link tree**. Two clever ideas make it
concurrency-friendly: every page stores a **high key** (an upper bound on its keys) and a
**right-link** pointer to its right sibling. A searcher descends the tree holding only one
page lock at a time; if a concurrent split moved the target rightward, the searcher notices
its key exceeds the high key and simply follows the right-link instead of restarting. That's
why index reads don't need to lock the whole path.

**Insert** walks to the correct leaf and adds the entry in key order. If the leaf is full it
**splits**: roughly half the entries move to a new page, a separator key is pushed up to the
parent (which may split too, recursively up to the root). Leaf entries point at heap tuples
by `ctid` — `(block, offset)`.

### MVCC — Multi-Version Concurrency Control

Every heap tuple carries two system columns: **`xmin`** (the transaction that created it) and
**`xmax`** (the transaction that deleted/superseded it, or 0). A row version is visible to you
if its `xmin` committed before your snapshot and its `xmax` either hasn't happened or isn't
visible to you.

```
   UPDATE accounts SET bal = 90 WHERE id = 1;

   before:  (id=1, bal=100)  xmin=50  xmax=0      <- old version, now stamped xmax=72
   after:   (id=1, bal=90 )  xmin=72  xmax=0      <- new version

   A transaction with an older snapshot still SEES bal=100.
   A transaction starting after 72 commits SEES bal=90.
```

So an UPDATE doesn't overwrite — it writes a **new tuple** and marks the old one dead. Readers
never block writers and writers never block readers, because they're literally looking at
different physical versions.

**Why VACUUM is necessary.** Dead tuples (old versions, deleted rows) pile up and waste space
— "bloat." `VACUUM` reclaims them for reuse, updates the visibility map, and crucially
advances the **frozen** transaction-id horizon to prevent transaction-ID wraparound. Autovacuum
does this in the background. The price of cheap, non-blocking updates is that someone has to
clean up afterward.

### WAL — Write-Ahead Logging

The rule: **the WAL record describing a change must be on durable storage before the changed
data page is.** Each record has a Log Sequence Number (LSN), and a page header records the LSN
of the last change that touched it. On `COMMIT`, the WAL up to that point is fsync'd — *that's*
the durability guarantee, even though the dirty data page may still be sitting in
`shared_buffers`.

A **checkpoint** periodically flushes all dirty buffers to the data files and writes a
checkpoint record marking a **redo point**. After a crash, recovery starts at the last redo
point and **replays** WAL forward, reapplying any changes that hadn't reached the data files.
Anything not committed is simply never made visible. Checkpoints bound recovery time but cause
an I/O spike, so they're spread out (`checkpoint_completion_target`).

---

## 4. Design Trade-Offs

- **MVCC's bargain:** glorious read/write concurrency, paid for with bloat and the constant
  need for VACUUM. Update-heavy tables that aren't vacuumed enough get slow and fat.
- **Clock-sweep over LRU:** gives up perfect recency accuracy to avoid lock contention across
  dozens of backends — a very deliberate "good enough, and scalable" choice.
- **WAL:** turns commit durability into a (mostly) **sequential** write to one log, instead of
  random fsyncs scattered across data files. Faster commits, plus replication and PITR fall
  out of having the log. The cost is write amplification — every change is written twice (WAL,
  then the data file at checkpoint).

---

## 5. Experiments / Observations — `EXPLAIN ANALYZE` on a join

Take a two-table join and ask the planner to show its work:

```sql
EXPLAIN (ANALYZE, BUFFERS)
SELECT c.name, o.total
FROM customers c
JOIN orders o ON o.customer_id = c.id
WHERE c.country = 'IN';
```

A representative plan:

```
Hash Join  (cost=38.50..512.30 rows=820 width=40)
           (actual time=0.42..3.11 rows=794 loops=1)
  Hash Cond: (o.customer_id = c.id)
  ->  Seq Scan on orders o  (cost=0..312 rows=10000 ...)
  ->  Hash  (cost=..)
        ->  Index Scan using customers_country_idx on customers c
              (cost=.. rows=210 ...) (actual .. rows=205 ...)
              Index Cond: (country = 'IN')
Planning Time: 0.30 ms
Execution Time: 3.34 ms
```

What to read out of it:

- **Estimated vs actual rows.** `rows=820` (estimate) vs `rows=794` (actual). The planner's
  estimate comes from `pg_statistic`, populated by `ANALYZE` — histograms, most-common-values,
  and `n_distinct` per column. When estimates are wildly off, that's usually stale statistics,
  and the planner then picks a bad join strategy.
- **Join method choice.** It chose a **hash join** (build a hash table on the smaller filtered
  side, probe with the larger). If one side were tiny it might pick a **nested loop**; if both
  were pre-sorted on the key, a **merge join**. The optimizer costs all three and picks the
  cheapest under its cost model.
- **Scan choice.** It used an index scan for the selective `country = 'IN'` filter but a seq
  scan on `orders` — because reading most of a table is cheaper sequentially than via random
  index lookups. Flip the selectivity and the choice flips.
- **`BUFFERS`** reveals `shared hit` vs `read` — i.e. how much came from the buffer cache
  versus disk. Run the same query twice and watch reads turn into hits as pages warm up.

---

## 6. Key Learnings

- A page's journey: planner asks the executor for a row → buffer manager pins the page (cache
  hit, or read from disk evicting a zero-usage buffer) → executor reads it → modifications mark
  the buffer dirty and emit a WAL record → commit fsyncs WAL → checkpoint later flushes the
  page. Durability is owed to the **WAL**, not to the data file being written.
- MVCC is the reason Postgres feels so smooth under concurrency, and **VACUUM is the unavoidable
  other half of that deal** — not a bug, but the cleanup cost of never overwriting in place.
- The planner is only as smart as its statistics. `EXPLAIN ANALYZE`'s estimate-vs-actual gap is
  the single most useful signal when a query goes wrong, and the fix is often just `ANALYZE`.
- The recurring theme: PostgreSQL repeatedly trades a bit of extra background work (vacuuming,
  checkpointing, double-writing to WAL) for strong online guarantees — concurrency and
  durability you don't have to think about while serving traffic.
