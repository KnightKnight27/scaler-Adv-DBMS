# PostgreSQL Internal Architecture

PostgreSQL is often described as the most advanced open-source relational
database, and that reputation rests on a handful of internal subsystems that work
together quietly on every query: the buffer manager that decides what lives in
RAM, the B-tree that keeps lookups fast, MVCC that lets thousands of transactions
run without blocking each other, and the WAL that guarantees no committed data is
ever lost. This document walks through those four subsystems and then shows them in
action with real `EXPLAIN ANALYZE` output and system catalogs from a live
PostgreSQL 18.3 server.

Every plan, statistic, and buffer count below was captured from an actual running
instance.

---

## 1. Problem Background

PostgreSQL descends from the POSTGRES project led by Michael Stonebraker at UC
Berkeley starting in 1986. The research goal was to extend the relational model
with richer types, rules, and extensibility, but the engineering goal that has
defined it ever since is correctness under concurrency.

A serious database has to let many users read and write the same data at the same
time without (a) seeing each other's half-finished work, (b) blocking each other
needlessly, or (c) losing committed data when the machine crashes. Almost every
internal design decision in PostgreSQL serves one of those three guarantees:

- Isolation without locking reads is solved by MVCC.
- Fast access to unordered heap data is solved by B-tree indexes and the buffer
  manager.
- Durability across crashes is solved by the WAL and checkpoints.

The price PostgreSQL pays for its particular flavor of MVCC is that updates leave
behind dead row versions, which is why it needs a background garbage collector
called `VACUUM`. That theme recurs throughout this document.

---

## 2. Architecture Overview

PostgreSQL is a multi-process system. A supervisor process (the postmaster) forks
one backend process per client connection, and several background processes handle
durability and cleanup. They all communicate through a region of shared memory, the
heart of which is the buffer pool.

```
   client ──SQL──►  Backend process
                       │
        ┌──────────────┼─────────────────────────┐
        │  Parser → Rewriter → Planner → Executor │   per-query pipeline
        └──────────────┬─────────────────────────┘
                       ▼
        ┌─────────────────────────────────────────┐
        │            SHARED MEMORY                  │
        │   ┌───────────────┐   ┌───────────────┐   │
        │   │ shared_buffers │   │  WAL buffers  │   │
        │   │ (8KB pages)    │   │               │   │
        │   └───────┬───────┘   └───────┬───────┘   │
        └───────────┼───────────────────┼───────────┘
                    │ dirty pages        │ WAL records
   Background:      ▼                    ▼
   bgwriter /  ┌─────────┐          ┌─────────┐
   checkpointer│ heap +  │          │  pg_wal │   ← fsync'd before commit
   autovacuum  │ indexes │          │  (log)  │
               └─────────┘          └─────────┘
```

The life of a query:
1. Parse the SQL into a tree; the rewriter applies rules and views.
2. Plan. The cost-based optimizer enumerates strategies (scan types, join orders,
   join algorithms) and picks the cheapest using statistics.
3. Execute. The executor pulls rows through the plan tree, reading pages from
   `shared_buffers` and loading them from disk on a miss.
4. On a write, the change is recorded in the WAL and flushed to disk before commit
   returns; the modified data page is flushed lazily later.

---

## 3. Internal Design

### 3.1 Buffer Manager — what lives in RAM

PostgreSQL never reads or writes the heap directly. Every 8 KB page passes through
`shared_buffers`, a fixed-size array of buffers in shared memory. When a backend
needs a page that isn't there, the buffer manager evicts a victim using a
clock-sweep algorithm (an approximation of LRU that tracks a usage counter per
buffer), reads the page in, and pins it for the duration of access.

`EXPLAIN (ANALYZE, BUFFERS)` exposes this directly. Running a count twice on a
table that is already cached:

```
Seq Scan on orders  (actual rows=13200 loops=1)
  Buffers: shared hit=109
```

`shared hit=109` means all 109 pages were already resident, with zero disk reads.
We can even see which relations occupy the cache using the `pg_buffercache`
extension:

```sql
SELECT relname, count(*) AS buffers FROM pg_buffercache ... GROUP BY relname;
```
```
     relname     | buffers
-----------------+---------
 orders          |     112
 users           |      32
 idx_orders_user |       3
```

That is the buffer manager's job in one snapshot: the hot `orders` heap gets the
most buffers, and a small index needs only a few pages to stay entirely in memory.

### 3.2 B-Tree — how lookups stay fast

PostgreSQL's default index is a B-tree, specifically a Lehman & Yao
high-concurrency variant. It is a balanced tree of 8 KB pages: the root and
internal pages hold separator keys and child pointers, while leaf pages hold the
indexed values plus the `ctid` (physical row address) pointing back into the heap.
Because the heap itself is unordered, the index is what turns an O(n) scan into an
O(log n) descent.

A point lookup shows the index in use:

```sql
EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM orders WHERE user_id = 42;
```
```
Bitmap Heap Scan on orders  (actual rows=4 loops=1)
  Recheck Cond: (user_id = 42)
  Buffers: shared hit=4 read=2
  ->  Bitmap Index Scan on idx_orders_user  (actual rows=4 loops=1)
        Index Cond: (user_id = 42)
        Buffers: shared read=2
```

The planner descended the B-tree (`Bitmap Index Scan`, 2 index pages read) to find
the matching `ctid`s, then fetched 4 heap pages. Without the index this would have
scanned all ~109 pages. Inserts that fill a leaf page trigger a page split: the
page is divided in two and a separator is propagated up, which is how the tree
stays balanced as it grows.

### 3.3 MVCC — concurrency without read locks

This is PostgreSQL's signature design. Instead of locking rows for readers, it
keeps multiple versions of each row and shows each transaction the version
consistent with its snapshot. Every heap tuple carries two hidden columns:

- `xmin` — the transaction id that created this version
- `xmax` — the transaction id that deleted or superseded it (0 means still live)

A row is visible to a transaction if its `xmin` is committed and visible to the
snapshot, and its `xmax` is not. The important part is that an `UPDATE` does not
overwrite the old row. It writes a brand-new version and stamps the old one's
`xmax`. Watching it happen live:

```sql
SELECT ctid, xmin, xmax, city FROM users WHERE id = 1;
-- (0,1)  | 814 | 0 | LA        ← original version

UPDATE users SET city = 'BOS' WHERE id = 1;

SELECT ctid, xmin, xmax, city FROM users WHERE id = 1;
-- (28,67)| 819 | 0 | BOS       ← new version at a new physical location

SELECT n_dead_tup FROM pg_stat_user_tables WHERE relname='users';
-- n_dead_tup = 1               ← the old version is now garbage
```

The update produced a new tuple at `(28,67)` and left the original `(0,1)` as a
dead tuple. Older in-progress transactions can still read the old version from
their snapshot, which is how readers never block writers. The cost is the dead row
that now needs reclaiming.

### 3.4 VACUUM — paying the MVCC tax

Because every update and delete leaves dead tuples behind, a table would grow
without bound and its indexes would fill with pointers to dead rows. `VACUUM`
reclaims that space and marks pages reusable, and autovacuum runs it automatically
based on how many dead tuples accumulate. It also performs freezing to prevent
transaction-id wraparound. VACUUM isn't optional housekeeping; it is the
counterpart that makes the no-overwrite MVCC model sustainable.

### 3.5 WAL — durability and crash recovery

PostgreSQL guarantees durability with write-ahead logging. The rule is that a
change must be written to the log before the corresponding data page is written to
disk. On `COMMIT`, the relevant WAL records are flushed (`fsync`) to `pg_wal/`. So
even if the machine loses power a microsecond later, the committed change survives
in the log even though the heap page may still be dirty in memory.

Recovery has two parts:
- Checkpoint. Periodically, the checkpointer flushes all dirty buffers to the heap
  and records a known-good position in the WAL.
- Crash recovery. On restart, PostgreSQL replays WAL forward from the last
  checkpoint, re-applying any committed changes that hadn't reached the heap.

The same WAL stream is what powers streaming replication and point-in-time
recovery: a replica simply replays the primary's WAL.

### 3.6 The Planner and `pg_statistic`

PostgreSQL's optimizer is cost-based. It estimates the cost of each candidate plan
and picks the cheapest, and those estimates depend entirely on statistics that
`ANALYZE` collects into the `pg_statistic` catalog (readable via the `pg_stats`
view). For example, here is what the planner knows about our columns:

```
 attname | n_distinct | most_common_vals |   most_common_freqs
---------+------------+------------------+------------------------
 id      |         -1 |                  |                          ← -1 = unique
 city    |          3 | {LA,SF,NYC}      | {0.3334,0.3334,0.3332}   ← even 1/3 split
```

Knowing `city` has 3 evenly distributed values, the planner can accurately predict
that `WHERE city='NYC'` returns about a third of the table. In the join below its
estimate of `rows=6664` matched the actual `6664` exactly.

---

## 4. Design Trade-Offs

Strengths
- MVCC gives true snapshot isolation: readers never block writers and vice versa,
  which is ideal for mixed read/write workloads.
- The cost-based planner adapts join strategy to the data. Given the same query it
  may pick nested-loop, hash, or merge joins depending on statistics.
- WAL delivers strong durability and doubles as the replication and PITR mechanism,
  so one subsystem serves several purposes.

Costs
- Write amplification from MVCC. Every update writes a full new tuple, and updates
  to an indexed column must update the index too, so heavily updated tables bloat
  quickly.
- VACUUM is mandatory. Under-tuned autovacuum on a high-churn table leads to bloat
  and even transaction-id wraparound risk.
- Per-connection processes. Each backend is an OS process, so thousands of
  connections are expensive, and production deployments usually front PostgreSQL
  with a pooler like PgBouncer.

In one line: PostgreSQL trades extra write work and a background garbage collector
for lock-free reads and clean snapshot semantics, which is a good bargain for the
read-heavy, correctness-sensitive workloads it targets.

---

## 5. Experiments / Observations

Dataset: `users` (5,000 rows) and `orders` (20,000 rows), index on
`orders.user_id`, on PostgreSQL 18.3.

### 5.1 A multi-table join, fully instrumented
```sql
EXPLAIN (ANALYZE, BUFFERS)
SELECT u.city, COUNT(*), SUM(o.amount)
FROM users u JOIN orders o ON o.user_id = u.id
WHERE u.city = 'NYC' GROUP BY u.city;
```
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
The planner chose a hash join: it built a hash table of the 1,666 NYC users
(`Memory Usage: 82kB`, a single batch) and streamed all 20,000 orders through it.
The estimate `rows=6664` exactly matched reality, a direct payoff from
`pg_statistic`. `Buffers: shared hit=138` shows every page came from cache, and the
filter discarded 3,334 non-NYC users (`Rows Removed by Filter`).

### 5.2 Index vs. sequential scan
A selective predicate (`user_id = 42`, about 4 rows) produced a bitmap index scan
that read just 2 index pages and 4 heap pages, while an unselective predicate
(`amount > 50`, about 13,200 rows) produced a sequential scan of all 109 pages. The
planner correctly switches strategy based on estimated selectivity: an index for a
needle, a sequential scan for a haystack.

### 5.3 Buffer cache contents
`pg_buffercache` confirmed the working set lives in RAM: 112 buffers for the
`orders` heap, 32 for `users`, and 3 for the index. That is exactly what you'd
expect for a small, hot dataset, and it is why repeated queries report `shared hit`
with no `read`.

---

## 6. Key Learnings

MVCC is the center of gravity here. Once you accept the rule "never overwrite a
row, just add a version," everything else follows: the hidden `xmin`/`xmax`
columns, snapshot visibility, dead tuples, and the need for VACUUM. I watched a
single `UPDATE` create a new tuple and orphan the old one, and that one statement
contains the whole model.

The planner is only as good as its statistics. The estimate matching the actual row
count exactly (6664 = 6664) wasn't luck; it came from `ANALYZE` recording the even
distribution of `city` into `pg_statistic`. Stale statistics are the most common
cause of bad plans in practice, and seeing the prediction land on the nose made
that concrete.

A few smaller observations worth holding onto:
- The buffer manager makes RAM the real database. The `BUFFERS` output turns an
  abstract idea into a number, and `shared hit` versus `read` is the difference
  between a memory access and a disk seek. It is the single biggest lever on query
  speed.
- WAL is durability and replication in one mechanism. The same log that lets
  PostgreSQL recover from a crash by replaying forward from a checkpoint is what it
  streams to replicas.
- Every guarantee has a maintenance cost. Lock-free reads are wonderful, but they
  are paid for with write amplification and mandatory vacuuming. PostgreSQL doesn't
  make the cost disappear; it moves it into the background where it's tunable, and
  understanding where the cost went is the key to operating it well.

---

### References
- PostgreSQL Documentation — *Internals*: Database Physical Storage, MVCC,
  Write-Ahead Logging, Routine Vacuuming, How the Planner Uses Statistics:
  <https://www.postgresql.org/docs/current/internals.html>
- *The Internals of PostgreSQL*, Hironobu Suzuki: <https://www.interdb.jp/pg/>
- P. L. Lehman & S. B. Yao, *"Efficient Locking for Concurrent Operations on
  B-Trees"*, ACM TODS 1981.
- M. Stonebraker & L. Rowe, *"The Design of POSTGRES"*, SIGMOD 1986.

*Experiments performed locally on PostgreSQL 18.3; all plans and catalog output are
copied from actual runs.*
