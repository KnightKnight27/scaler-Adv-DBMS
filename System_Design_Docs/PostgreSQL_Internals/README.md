# PostgreSQL Internal Architecture

**Roll Number:** 24BCS10406
**Name:** Manasvi Sabbarwal
**Topic:** System Design Discussion, Topic 2

I picked this topic because three of the labs we built earlier this term
turned out to be direct miniatures of Postgres internals: Lab 3 was a
clock-sweep buffer cache (which is essentially what `bufmgr.c` does),
Lab 6 was a B-tree from scratch (nbtree's smaller cousin), and Lab 8 was
an MVCC + strict-2PL transaction manager (the same shape as Postgres's
heapam plus lock manager). Writing this is partly an opportunity to
explain what Postgres does differently than my toy versions and why.

The experimental section uses a fresh four-table OLTP-ish schema
(`customers`, `products`, `orders`, `order_items`, ~812k rows total) on a
local Postgres 16.13 server. Setup and outputs are in section 5.

---

## 1. Problem Background

POSTGRES started at Berkeley in 1986 as the successor to Ingres. The
original goal was an extensible relational database: users could add
their own data types, operator classes, and index access methods. The
"POST" in the name was literal, post-Ingres. SQL was bolted on after the
code went open-source in 1996. Streaming replication, WAL, MVCC,
foreign data wrappers, logical decoding, JIT-compiled expressions, JSONB
all came later, but the underlying architecture is recognizably the same
process model Stonebraker drew in the 1986 paper.

The problem Postgres is built for is "many clients hitting one shared
dataset that must not lose committed transactions". Everything in the
internal architecture flows from that. Process-per-connection so a buggy
client cannot corrupt the others. Shared buffers so backends share a
page cache instead of duplicating it. MVCC so readers and writers do not
fight. WAL so a power loss does not destroy state and so a replica can
follow along.

---

## 2. Architecture Overview

### Process model

```
                          +--------------------+
                          |  postmaster        |   listens on socket
                          |  (parent process)  |   forks one child per
                          +---------+----------+   incoming connection
                                    | fork()
                +-------------------+--------------------+
                |                   |                    |
        +-------v------+   +--------v------+   +---------v---------+
        | backend #1   |   | backend #2    |   | backend #N        |
        |  parser      |   |               |   |                   |
        |  planner     |   |               |   |                   |
        |  executor    |   |               |   |                   |
        +------+-------+   +--------+------+   +--------+----------+
               |                    |                   |
               +--------+-----------+----------+--------+
                        |                      |
                  +-----v-----+         +------v-------+
                  | shared    |         | locks /      |
                  | buffers   |         | wait graphs  |
                  | (page     |         +--------------+
                  |  cache)   |
                  +-----+-----+
                        |
                        v
              +---------+--------+        +--------------+
              | bgwriter / WAL   |  --->  | pg_wal/ on   |
              | writer /         |        | disk         |
              | checkpointer /   |        +--------------+
              | autovacuum       |
              +------------------+
```

There is one parent (`postmaster`), one backend per client connection,
and a handful of background helpers (background writer, WAL writer,
checkpointer, autovacuum launcher, archiver, stats collector). The
postmaster does not handle queries itself; it only accepts connections
and forks. If a backend crashes, the postmaster notices, kills every
other backend (to avoid using a possibly-corrupt shared memory), and
restarts the whole cluster. That sounds drastic but it is the only way
to be sure that the shared memory state is still consistent.

### What lives in shared memory

The big things:

- **shared_buffers**: a fixed array of 8 KB page frames, default 128 MB.
  Every backend reads and writes pages through this cache.
- **WAL buffers**: a small ring (default 16 MB) that backends append
  records to before they hit disk.
- **Lock manager state**: a hash table mapping locktag to wait queues.
- **Process array**: who is running, what their xmin/xmax are.
- **clog (pg_xact)** buffers: per-transaction commit/abort bits.

Everything else (parser state, plan trees, executor scratch) is private
to the backend.

---

## 3. Internal Design

### 3.1 Buffer Manager

Source: `src/backend/storage/buffer/bufmgr.c` and `freelist.c`.

The buffer pool is an array of `BufferDesc` entries, one per 8 KB page
frame. Each descriptor stores:

```
BufferDesc
+-----------------------+
| tag (rel, fork, blk)  |   what page is here
| state (atomic uint32) |   refcount + usage_count + flags (DIRTY, VALID, ...)
| content lock          |   serializes readers vs writers of the page
| io_in_progress lock   |   only one backend reads from disk at a time
| wait_backend_pid      |   for buffer pin waits
+-----------------------+
```

A backend that wants page P does:

```
ReadBufferExtended(rel, blk):
  hash lookup in BufTable: is P already cached?
    yes -> PinBuffer (increment refcount, bump usage_count), return
    no  -> StrategyGetBuffer (find a victim via clock sweep)
           if victim is dirty -> FlushBuffer to disk via smgr
           BufTableDelete(old tag); BufTableInsert(new tag)
           smgrread(rel, blk, buffer)
           return
```

The clock-sweep replacement is the same algorithm I implemented in
Lab 3. A `nextVictimBuffer` index walks around the ring; on each step it
decrements `usage_count`. If `usage_count == 0` AND `refcount == 0`, the
buffer becomes the victim. If a hot page was hit recently, its
`usage_count` is high, so it survives several full sweeps before being
considered for eviction.

```
clock sweep:

  +---+   +---+   +---+   +---+   +---+
  | A | -> | B | -> | C | -> | D | -> ...
  | 3 |   | 1 |   | 0 |   | 2 |
  +---+   +---+   +---+   +---+
                    ^
                    nextVictim (this one wins, usage=0)
```

What about ring buffers and bulk operations? A plain `SELECT *` on a
huge table would normally evict every hot OLTP page from `shared_buffers`
on its way through. Postgres avoids that with **buffer access
strategies**: a sequential scan over a relation larger than 1/4 of
`shared_buffers` uses a small dedicated 256 KB ring instead, so the scan
recycles its own pages without polluting the main pool. VACUUM and
bulk-load do the same. This is in `src/backend/storage/buffer/freelist.c`
in the `BAS_*` strategy code.

Background processes:

- **bgwriter** continuously flushes dirty pages so that backends rarely
  have to flush themselves during a buffer fault.
- **checkpointer** runs full checkpoints (every `checkpoint_timeout`
  seconds or after `max_wal_size` of WAL). A checkpoint flushes every
  dirty buffer to disk and writes a checkpoint record into the WAL.
- **WAL writer** drains the WAL buffer ring to disk so commits do not
  have to wait for an empty buffer.

### 3.2 nbtree (B-tree implementation)

Source: `src/backend/access/nbtree/`.

Postgres's B-tree is a Lehman-Yao variant. Two structural choices stand
out:

1. **Right-link pointers.** Every level of the tree has a sibling chain
   running left to right. A reader following a path can fall off the
   right edge of a node because of a concurrent split; the right link
   lets it land on the new sibling without restarting from the root.
2. **High keys.** Each internal page stores a "high key" upper bound.
   A search that goes right via the right-link uses the high key to
   detect when it has overshot and needs to descend.

These two together let nbtree allow many concurrent readers and a
splitting writer to coexist without coarse-grained locks. The same
ideas are described in Lehman and Yao's 1981 paper.

```
nbtree leaf page (simplified)

   page header  | line pointers | ...free space...| tuples (key + heap TID)
   +-----------+--------+-------+-----------------+-------+-------+-------+
   | opaque    | hi key | LP... |                 | T1    | T2    | T3    |
   +-----------+--------+-------+-----------------+-------+-------+-------+
                                                   ^------ inserted at end
   right sibling pointer in opaque header
```

What my Lab 6 B-tree did not have:

- right-link concurrency (Lab 6 was single-threaded)
- HOT chains in the heap that avoid index updates on small in-page
  updates
- partial indexes (`CREATE INDEX ... WHERE ...`)
- expression indexes (`CREATE INDEX ... ON tbl ((lower(col)))`)
- INCLUDE columns for index-only scans

The CLRS insert and split logic, however, is the same. nbtree is
recognizably a B-tree, just industrialized.

### 3.3 MVCC (Multi-Version Concurrency Control)

Source: `src/backend/access/heap/heapam.c`,
`src/backend/storage/ipc/procarray.c`, plus the visibility helpers in
`tqual.c` (older versions) / `heapam_visibility.c` (newer).

Every row on a heap page has a `HeapTupleHeader` (23 bytes min):

```
+---------+---------+------+------+--------+------------+
| t_xmin  | t_xmax  | cmin | cmax | t_ctid | infomask   |
| 4 bytes | 4 bytes | 2 B  | 2 B  | 6 B    | 4 B        |
+---------+---------+------+------+--------+------------+
| t_infomask2 (2 B) | t_hoff (1 B) | NULL bitmap (var) |
+---------+---------+--------------+-------------------+
| column data, aligned per type                        |
+------------------------------------------------------+
```

A version is **visible to a snapshot** S if (approximately):

```
xmin_visible = (xmin == myself)
            OR (xmin is committed AND xmin not in S.active_xids
                AND xmin < S.xmax_horizon)

xmax_visible = (xmax == 0)
            OR (xmax != myself
                AND NOT (xmax is committed
                         AND xmax not in S.active_xids
                         AND xmax < S.xmax_horizon))

visible = xmin_visible AND xmax_visible
```

An UPDATE never overwrites the original tuple. It sets the old tuple's
`t_xmax` to the updating transaction and writes a new tuple with
`t_xmin = self`. The two are linked through `t_ctid`. Readers walking
the chain consult the snapshot to decide which version is "the current
row" for them.

Snapshots are taken by `GetSnapshotData()` from the procarray (a shared
memory list of every active backend's `MyProc`). A snapshot is roughly
`(xmin_horizon, xmax_horizon, list_of_active_xids)`. From that, plus
the per-tuple xmin/xmax, every reader can decide visibility
independently with no locking on the heap.

In Lab 8 I built exactly this shape. The simplification I made was
`snapshot = my_xid` instead of a horizon plus an active list, which is
fine for a toy but breaks when an older xid commits after my begin
(real Postgres records that xid as "still active" in my snapshot so it
remains invisible to me).

### 3.4 WAL (Write-Ahead Logging)

Source: `src/backend/access/transam/xlog.c` and `xloginsert.c`.

The rule: any change to a page must be written to the WAL **before** the
change to the page itself is flushed to disk. That guarantee is what
makes crash recovery possible.

A WAL record looks like:

```
+--------------------+--------------------+----------------+
| XLogRecord header  | rmgr-specific data | optional       |
|   xl_tot_len       | (e.g. heap insert: | full-page image|
|   xl_xid           |  block ref + tuple)|                |
|   xl_prev          |                    |                |
|   xl_info, xl_rmid |                    |                |
|   xl_crc           |                    |                |
+--------------------+--------------------+----------------+
```

The "rmgr" is a resource manager: heap, btree, gin, xact, etc. Each one
knows how to redo its own kinds of records during recovery. There is no
generic "page diff"; each module emits records describing its own
operation.

After a crash, recovery starts at the last completed checkpoint LSN
(recorded in `pg_control`), reads each WAL record, and calls the
corresponding rmgr's `redo` function. Tuples whose `xmin` belongs to a
transaction that never committed remain physically present after
recovery, but the visibility rule hides them.

WAL is also the substrate for:

- **Streaming replication.** A standby receives WAL records as they are
  generated and applies them. The same `redo` code path is used.
- **Point-in-time recovery.** With archived WAL files, you can restore
  a base backup and replay WAL up to a specific timestamp or LSN.
- **Logical decoding.** Tools like Debezium read the WAL, decode it to
  logical changes (insert/update/delete with column values), and feed
  them into Kafka or another system.

### 3.5 Why VACUUM exists

MVCC keeps old row versions around until no active snapshot can still
see them. Eventually the heap fills up with dead tuples. The space
between live tuples on a page cannot be reused until something marks
those slots as free.

That something is VACUUM. It walks the heap, finds tuples whose
`t_xmax` is committed and older than the **oldest active snapshot**
(the "horizon"), and marks their line pointers as `LP_DEAD`. The space
can then be reused by future inserts on the same page. A `VACUUM FULL`
goes further and rewrites the whole relation, but it takes an
exclusive lock and rewrites everything, so it is rarely the right
choice in production.

Autovacuum is a daemon (`autovacuum launcher` + worker processes) that
runs VACUUM and ANALYZE periodically based on a per-table dead-tuple
threshold. It is the reason you don't usually have to think about
VACUUM manually.

Why does it matter? Three reasons:

1. **Bloat.** Without VACUUM, the heap grows indefinitely even on a
   workload that never gains new rows.
2. **Index hygiene.** Dead index entries point to dead heap tuples; if
   VACUUM does not remove them, index scans return tuples that
   visibility check then filters out, which is wasted work.
3. **Transaction ID wraparound.** Postgres uses 32-bit xids and runs
   into a wraparound problem at ~2 billion transactions. Autovacuum
   periodically "freezes" old tuples (marks them as visible to all
   snapshots) so that wraparound can be handled safely. Without that,
   the cluster would eventually have to shut down to prevent data
   loss.

### 3.6 Query planning and the role of statistics

Source: `src/backend/optimizer/`, `src/backend/commands/analyze.c`.

The planner picks an execution plan by estimating each candidate's cost.
Cost estimation depends on statistics gathered by `ANALYZE` and stored
in `pg_statistic` (exposed user-friendlier via `pg_stats`).

The interesting columns of `pg_statistic` for a column include:

- `stadistinct` (negative = fraction of n; positive = absolute count)
- `stamcv` (most common values) and `stamcfreqs` (their frequencies)
- `stahistogram` (equi-depth histogram of remaining values)
- `stanullfrac` (fraction NULL)
- `stadistinct[]` per column combination if extended stats are defined

When the planner sees `WHERE status = 'paid'`, it looks at
`pg_stats.most_common_vals` for that column to estimate the selectivity.
For `created_at BETWEEN d1 AND d2`, it uses the histogram. For join
selectivity it cross-references `n_distinct` of the join keys on both
sides.

Bad stats lead to bad plans. The classic failure mode is:
`ANALYZE` was never run after a bulk insert, so the optimizer thinks the
table has zero rows, picks a nested loop join, and the query runs for
hours. The fix is `ANALYZE` (or wait for autovacuum to run it).

---

## 4. Design Trade-Offs

### 4.1 Heap with versioning vs in-place updates

Postgres's MVCC requires that **every update is an insert plus a mark**.
The benefit is non-blocking readers and a clean way to implement
SERIALIZABLE via SSI (predicate locking). The cost is twofold:

1. The heap grows on every update. Workloads that update the same row
   often (counters, session metadata) bloat fast.
2. Indexes have to be updated to point to the new tuple location. HOT
   updates (heap-only-tuple) mitigate this for in-page updates that do
   not change any indexed column, but cross-page updates still need
   full index maintenance.

The alternative (InnoDB-style in-place updates with undo logs) is what
MySQL chose. We will compare those in Topic 3.

### 4.2 Process-per-connection vs threads

A separate OS process per connection gives strong isolation: a backend
crash cannot scribble on another backend's memory. The price is that
spinning up a new connection is expensive (`fork()` + memory setup),
and Postgres caps `max_connections` at typically a few hundred.
Production deployments use a connection pooler (PgBouncer or built-in
one in newer Postgres) to multiplex many client connections onto a
smaller number of backend processes.

A thread-per-connection database (MySQL, SQL Server) is cheaper per
connection but a buggy thread can corrupt shared state of others.
Postgres made a durability-first choice.

### 4.3 WAL and double-write avoidance

Postgres's WAL contains **full-page images** for the first modification
of a page after each checkpoint. This is necessary because most disks
do not guarantee atomic writes of 8 KB pages; a power failure mid-write
can leave a torn page. The full-page image is the fix: on recovery,
the page is restored from the WAL before further records are applied.

MySQL/InnoDB solves the same problem with a "doublewrite buffer" that
writes every page twice (once to a scratch area, once to the real
location). Different choice; same goal.

### 4.4 Process complexity vs operational features

Postgres ships with autovacuum, autoanalyze, a checkpoint scheduler, a
WAL writer, a background writer, a stats collector, an archiver, and
sometimes a logical replication walsender. That is a lot of moving
parts. Each one was added to solve a real production problem and they
mostly work without intervention, but when one misbehaves (autovacuum
falling behind on a huge table is a classic), diagnosing it requires
real understanding of the system.

This is the price of being a long-running shared server. Embedded
SQLite has none of those because it does not have to do any of the
work.

---

## 5. Experiments and Observations

### 5.1 Setup

```sql
-- four tables, ~812k rows total, 3 indexes plus auto-created pkeys
customers   (id PK, name, country, created_at)         10,000 rows
products    (id PK, name, category, price)              2,000 rows
orders      (id PK, customer_id FK, status, created_at) 200,000 rows
order_items (order_id, product_id, qty, price)          600,000 rows
            PRIMARY KEY (order_id, product_id)

CREATE INDEX idx_orders_customer ON orders(customer_id);
CREATE INDEX idx_orders_status   ON orders(status);
CREATE INDEX idx_items_order     ON order_items(order_id);
ANALYZE customers; ANALYZE products; ANALYZE orders; ANALYZE order_items;
```

Relation sizes:

```
       relname       |  size   | relpages |  rows
---------------------+---------+----------+--------
 order_items         | 34 MB   |     4412 | 600000
 order_items_pkey    | 24 MB   |     3077 | 600000
 orders              | 11 MB   |     1471 | 200000
 idx_items_order     | 9640 kB |     1205 | 600000
 orders_pkey         | 4408 kB |      551 | 200000
 idx_orders_customer | 1528 kB |      191 | 200000
 idx_orders_status   | 1368 kB |      171 | 200000
 customers           | 584 kB  |       73 |  10000
 customers_pkey      | 240 kB  |       30 |  10000
 products            | 128 kB  |       16 |   2000
 products_pkey       | 64 kB   |        8 |   2000
```

The order_items table alone is bigger than everything else combined,
which matches the row count (600k vs 200k for orders, 10k for
customers, 2k for products).

### 5.2 The multi-table join (rubric exercise)

Query:

```sql
SELECT c.country,
       p.category,
       SUM(oi.qty * oi.price) AS revenue
FROM customers c
JOIN orders o       ON o.customer_id = c.id
JOIN order_items oi ON oi.order_id   = o.id
JOIN products p     ON p.id          = oi.product_id
WHERE o.status = 'paid'
  AND o.created_at BETWEEN DATE '2026-02-01' AND DATE '2026-04-30'
GROUP BY c.country, p.category
ORDER BY c.country, p.category;
```

EXPLAIN (ANALYZE, BUFFERS) output, abbreviated:

```
Finalize GroupAggregate (cost=12179.51..12192.27 rows=48)
                        (actual time=37.440..38.793 rows=12)
  Group Key: c.country, p.category
  Buffers: shared hit=6196 read=44
  ->  Gather Merge  Workers Planned: 2  Workers Launched: 2
        ->  Sort  Sort Method: quicksort  Memory: 26kB
              ->  Partial HashAggregate
                    ->  Hash Join  (Hash Cond: oi.product_id = p.id)
                          ->  Hash Join  (Hash Cond: o.customer_id = c.id)
                                ->  Parallel Hash Join
                                      Hash Cond: oi.order_id = o.id
                                      ->  Parallel Seq Scan on order_items
                                            Buffers: shared hit=4412
                                      ->  Parallel Hash
                                            ->  Parallel Bitmap Heap Scan on orders
                                                  Recheck Cond: status = 'paid'
                                                  Filter: created_at IN range
                                                  Rows Removed by Filter: 8519
                                                  Heap Blocks: exact=581
                                                  ->  Bitmap Index Scan on idx_orders_status
                                ->  Hash -> Seq Scan on customers
                          ->  Hash -> Seq Scan on products
Planning Time: 2.149 ms
Execution Time: 38.865 ms
```

![EXPLAIN ANALYZE of the 4-table join](../../screenshots/pg-internals-explain.png)

Things to notice:

- The planner chose **parallel hash joins** with 2 workers because the
  query was eligible and `parallel_setup_cost` was outweighed by the
  expected savings.
- The first thing it does on `orders` is a **bitmap index scan on
  `idx_orders_status`** followed by a heap scan that rechecks the
  status (just in case) AND filters by date range. The index is on
  status only, so the date predicate is applied during the heap scan.
- `Buffers: shared hit=6196 read=44` means almost the entire join was
  served from cache. Only 44 8 KB pages came from disk.
- `customers` and `products` were small enough that the planner chose
  hash table builds from sequential scans rather than indexed lookups.
- Total execution: 38.8 ms. Most of it was the parallel seq scan and
  bitmap heap scan on the big tables.

### 5.3 Statistics behind the plan

```
   attname    | n_distinct |         most_common_vals         |     most_common_freqs
--------------+------------+----------------------------------+----------------------------
 status       |          4 | {shipped,cancelled,pending,paid} | {0.2508, 0.2506, 0.2504, 0.2482}
 customer_id  |      10002 | {779, 1973}                      | {0.000333, 0.000333}
 created_at   |        180 | {2026-06-03, 2026-05-10}         | {0.00667, 0.00650}
```

What the planner does with this:

- For `status = 'paid'`, it sees `'paid'` in `most_common_vals` with
  frequency 0.2482, so it estimates 200000 * 0.2482 ~= 49,640 matching
  rows. (Real: 50,000.)
- For `customer_id`, `n_distinct = 10002` and frequencies are tiny
  (~0.00033 each), so the planner uses the uniform-distribution
  formula and estimates ~20 rows per customer.
- For `created_at BETWEEN d1 AND d2`, it consults the histogram
  (not shown above) and estimates how many of the 180 distinct dates
  fall in the range.

These three estimates compose multiplicatively when the planner picks
the join order. Bad stats anywhere along the chain (stale `n_distinct`,
missing MCV, outdated histogram) is the most common cause of bad
plans in real Postgres deployments.

### 5.4 Buffer behavior

The `Buffers:` annotations are the most useful single thing in
EXPLAIN ANALYZE. They tell us:

- **shared hit=N**: N pages found in `shared_buffers` (no disk I/O).
- **shared read=N**: N pages that were not in cache and had to be read
  from disk (or the OS page cache).
- **shared dirtied=N**: N pages we wrote to that became dirty.

In the query above we have `shared hit=6196 read=44` overall. The
sequential scan on `order_items` is `shared hit=4412` which matches the
table's page count exactly, meaning every page was already in
`shared_buffers` from earlier ANALYZE / planning. The 44 reads were
mostly on the `orders` table (1471 - some) and the `idx_orders_status`
index pages.

If we re-ran the query immediately, `Buffers: shared read` would drop
to 0 because the 44 pages we just read would now be in
`shared_buffers`. That's exactly how the warm-cache effect shows up in
the data.

### 5.5 WAL records during a transaction

```
psql=# SELECT pg_current_wal_lsn();
 0/F3F60F0

psql=# BEGIN;
psql=# UPDATE orders SET status='paid' WHERE id BETWEEN 1 AND 1000;
UPDATE 1000
psql=# COMMIT;

psql=# SELECT pg_current_wal_lsn();
 0/F450D98
```

The LSN advanced from `0xF3F60F0` to `0xF450D98`, a delta of `0x5ACA8`
bytes (~371 KB) for 1000 row updates. That averages ~371 bytes per
updated row, which is reasonable for a row of this width plus the
update record header.

What got written to the WAL for each updated row:
- a heap update record (XLOG_HEAP_UPDATE)
- a heap insert record for the new tuple (if not in the same page)
- index update records for any indexed column that changed
- (in this case the status changed, so idx_orders_status got new entries)

The COMMIT record at the end is what makes the whole transaction
visible. Without that COMMIT in the WAL, recovery would treat all the
preceding records as work from an aborted transaction.

### 5.6 MVCC dead tuples and VACUUM

Right after the UPDATE:

```
n_live_tup | n_dead_tup | last_autovacuum
-----------+------------+-----------------
   200000  |       1000 | (recent)
```

1000 dead tuples. Each one is an old row version whose `t_xmax` is the
update transaction, no longer visible to any new snapshot. They sit in
the heap consuming space.

After `VACUUM orders`:

```
INFO:  vacuuming "postgres.public.orders"
INFO:  finished vacuuming "postgres.public.orders": index scans: 0
pages: 0 removed, 1478 remain, 16 scanned (1.08% of total)
tuples: 1000 removed, 199944 remain
```

The 1000 dead tuples are now reclaimable. (Note `n_live_tup` is 199944
instead of 200000 because autovacuum had already run and removed a
few before our manual VACUUM.) The heap file size did not shrink, but
the slots inside its pages are now free for new inserts on the same
page. That is the normal trade-off: VACUUM frees space inside files,
not the files themselves. To shrink the file you would need
`VACUUM FULL` or `pg_repack`, both of which take stronger locks.

This is also why workloads that do a lot of UPDATEs need autovacuum
tuned aggressively. Without it, the dead tuples pile up, queries
slow down because they walk longer version chains, and indexes bloat.

![WAL delta + dead tuples + VACUUM](../../screenshots/pg-internals-mvcc-vacuum.png)

---

## 6. Key Learnings

- **The buffer manager looks exactly like the textbook.** Clock sweep
  with usage counts, refcount pinning, and a separate WAL-write path
  before any dirty page can leave the cache. The fact that I built a
  toy version in Lab 3 made the real code immediately legible. There
  are extra concerns (ring buffers for bulk scans, strategy hooks,
  HASHHDR alignment), but the core algorithm is the same.

- **nbtree is the same B-tree from CLRS, plus Lehman-Yao.** Right
  links and high keys are what make many concurrent readers and a
  splitting writer coexist. The Postgres source has comments that
  literally cite the 1981 paper.

- **MVCC is a heap invariant, not a separate subsystem.** Every row on
  disk carries the four xids and a NULL bitmap. Visibility is decided
  by reading those fields against a snapshot from the procarray. There
  is no separate "MVCC engine"; it is the heap layer plus the snapshot
  mechanism.

- **VACUUM is not a quirk; it is fundamental.** Snapshot isolation
  buys non-blocking readers by leaving old versions on disk. Those
  versions become invisible once no live snapshot needs them; reclaiming
  the space is VACUUM's job. Without it the system would slowly die,
  not from corruption but from bloat plus eventual transaction-ID
  wraparound.

- **WAL is more than durability.** It is the foundation for streaming
  replication, point-in-time recovery, and logical decoding. The same
  bytes that protect against a crash also feed replicas and CDC tools.
  This dual purpose is the single most important thing the Postgres
  team got right architecturally, in my opinion. SQLite has a WAL too,
  but its WAL was never meant to leave the host.

- **Statistics drive everything in the planner.** Selectivity is
  estimated from `pg_statistic`. n_distinct, MCV, histograms, and
  extended statistics for column correlations all feed into the cost
  model. ANALYZE is what keeps that current. Forget to ANALYZE after
  a bulk load and you will get a textbook bad plan.

- **The plan output is a learning tool.** Once you can read
  `EXPLAIN (ANALYZE, BUFFERS)`, debugging slow queries becomes a
  mostly-mechanical exercise: find the node with the worst
  cost/actual mismatch, check its `Buffers:` line, ask "did my stats
  predict this?", and fix the upstream issue (missing index, stale
  stats, no partitioning, etc.). EXPLAIN is the single most valuable
  thing in Postgres that SQLite does not really have an analogue for.

---

## References

- E. Rogov, "PostgreSQL Internals" (free PDF, four parts covering
  storage, MVCC, buffer manager, WAL, planner, locks)
- B. Momjian's slide decks at momjian.us
- PostgreSQL source tree:
  - `src/backend/storage/buffer/` (buffer manager)
  - `src/backend/access/nbtree/` (B-tree)
  - `src/backend/access/heap/` (heap and HOT updates)
  - `src/backend/access/transam/xlog.c` (WAL)
  - `src/backend/optimizer/` (planner)
  - `src/backend/commands/analyze.c` (statistics collection)
- Lehman, Yao, "Efficient locking for concurrent operations on
  B-trees" (1981)
- Stonebraker, Rowe, "The design of POSTGRES" (1986)
- Hellerstein, Stonebraker, Hamilton, "Architecture of a database
  system" (2007, the canonical RDBMS architecture overview)
