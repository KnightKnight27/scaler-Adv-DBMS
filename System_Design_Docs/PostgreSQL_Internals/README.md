# PostgreSQL Internal Architecture

> Advanced DBMS, System Design Discussion
> All experiments below are **real output** from a local **PostgreSQL 18.3** instance with a 3-table, ~550k-row dataset I built for this study.

---

## 1. Problem Background

PostgreSQL grew out of Berkeley's POSTGRES project (1986) and has become the reference open-source relational database for serious multi-user workloads. The hard problems it solves internally are the same ones every real database faces:

- How do you let many users read and write the same data at once without them blocking each other or seeing inconsistent state? The answer is **MVCC**.
- How do you make reads and writes fast when data lives on slow disks? The answer is the **buffer manager** (a shared page cache) plus **B-tree indexes**.
- How do you guarantee that a committed transaction survives a crash the instant after COMMIT returns? The answer is **Write-Ahead Logging (WAL)**.
- How do you choose a good way to execute a query out of millions of possibilities? The answer is a **cost-based planner** driven by collected **statistics**.

This document walks through those four subsystems and shows each one working on a live instance. The dataset:

```sql
customers (50,000 rows)   -- customer_id PK, name, country
products  (1,000 rows)    -- product_id PK, name, category, price
orders    (500,000 rows)  -- order_id PK, customer_id FK, product_id FK, quantity, order_date
-- + indexes on orders(customer_id), orders(product_id), customers(country)
```

---

## 2. Architecture Overview

```
            ┌──────────── one backend PROCESS per connection ────────────┐
 SQL  ───►  │  Parser  ─►  Planner/Optimizer  ─►  Executor                │
            └───────┬──────────────┬──────────────────┬──────────────────┘
                    │              │                   │
            uses statistics   picks lowest-      reads/writes 8KB pages
            (pg_statistic)     cost plan          through ↓
                                                ┌───────────────────────┐
                                                │   BUFFER MANAGER       │
                                                │  shared_buffers cache  │  ◄── shared memory
                                                │  (clock-sweep evict)   │
                                                └───────────┬───────────┘
                                                            │ misses / flushes
                       every change is logged first ──►  ┌──┴───────────┐
                       ┌──────────────┐                  │  Heap files  │  base/…
                       │   WAL  pg_wal│ ◄────────────────│  Index files │  (B-trees)
                       │  (durability)│   WAL-before-data │  FSM / VM    │
                       └──────────────┘                  └──────────────┘
        background helpers: bgwriter · checkpointer · walwriter · autovacuum
```

**Data flow of a query.** The backend parses the SQL, the planner uses table statistics to estimate the cheapest execution plan, and the executor runs it, reading 8 KB pages through the buffer manager. Any modification is first described in a WAL record (flushed to disk before the data page), so a crash can be recovered by replaying the log. Background processes asynchronously flush dirty pages (bgwriter and checkpointer) and reclaim dead row versions (autovacuum).

---

## 3. Internal Design

### 3.1 Buffer Manager (`src/backend/storage/buffer/`)

PostgreSQL never reads a table row straight from disk into your query. It always goes through `shared_buffers`, a fixed array of 8 KB page slots in shared memory (default 128 MB; my instance ran with 16384 × 8 KB = 128 MB). Because it is shared, every backend benefits from pages others have already cached.

Three structures cooperate:
- a **buffer table** (hash map): `BufferTag` `(db, relation, fork, block#)` to a buffer slot id;
- an array of **buffer descriptors** (metadata: dirty flag, pin/`refcount`, `usage_count`, content lock);
- the **buffer pool** itself (the 8 KB slots).

Reading a page works like this. Look it up in the buffer table. On a hit, pin it and read. On a miss, pick a victim slot, evict it (writing it out first if it is dirty), and load the page from disk into that slot.

Replacement uses a clock-sweep. A pointer (`nextVictimBuffer`) rotates over the descriptors. Each time it passes an unpinned buffer it decrements `usage_count`, and a buffer becomes the victim when it is unpinned and `usage_count == 0`. Every access bumps `usage_count` (capped at 5), so hot pages keep surviving while cold pages drift to zero and get evicted. This approximates LRU cheaply.

Pinning (`refcount > 0`) means "in use, do not evict." The WAL-before-data rule says that before a dirty page is written to disk, the WAL up to that page's LSN must already be flushed. The bgwriter proactively cleans dirty pages so backends usually find clean victims, and the checkpointer flushes everything at checkpoints.

You can watch the buffer manager work in the `BUFFERS` line of any plan. `shared hit=N` are cache hits, and `read=N` are misses that hit the OS or disk (see section 5).

### 3.2 B-Tree Implementation (`src/backend/access/nbtree/`)

PostgreSQL's default index is a B-tree, implemented as a faithful version of the **Lehman & Yao (1981)** high-concurrency B-tree.

```
                 [ meta page ]  (block 0, fixed → points to current root)
                       │
                  ┌────▼────┐
                  │  root   │
                  └──┬───┬──┘
            ┌────────┘   └────────┐
       ┌────▼────┐           ┌────▼────┐    internal pages: separator keys + downlinks
       │ internal│──right──► │ internal│
       └──┬───┬──┘           └────┬────┘
      ┌───▼─┐ ┌─▼───┐         ┌───▼───┐
      │leaf │→│leaf │→ ─ ─ ─ →│ leaf  │     leaves: (index key → heap TID), doubly linked
      └─────┘ └─────┘         └───────┘
```

Two ideas make it concurrent and crash-safe:
- **High key plus right-link.** Every page knows the largest key it is allowed to hold (its *high key*) and has a pointer to its right sibling. A descending search compares the search key to the high key, and if the key is higher than the high key, a concurrent split has moved the data, so the search just follows the right-link instead of re-locking from the parent. Readers proceed with minimal locking.
- **Splits stay correct mid-operation.** Inserting into a full leaf splits it: about half the entries move to a new right page, then a downlink and separator key are pushed into the parent (which may split in turn). Because the new page is linked via the right-link and high key first, concurrent readers stay correct even while the split is in progress.

Two useful optimizations: a *fastpath* for monotonically increasing keys (serial IDs, timestamps) caches the rightmost leaf to skip the full root-to-leaf descent, and *deduplication* (PG 13+) merges index entries that share the same key into a single posting list of heap TIDs, shrinking indexes on low-cardinality columns.

### 3.3 MVCC, Multi-Version Concurrency Control

PostgreSQL's headline feature is that readers never block writers and writers never block readers. It does this by keeping multiple physical versions of each row instead of updating in place.

Every heap tuple carries a header with (among other fields):
- `xmin`, the transaction id (XID) that created this version,
- `xmax`, the XID that deleted or expired it (0 if still live),
- `ctid`, a `(block, offset)` pointer used to chain an old version to its replacement,
- `t_infomask` hint bits that cache commit/abort status to avoid re-reading the commit log.

An `UPDATE` is really a delete plus an insert. The old version gets its `xmax` set (and its `ctid` pointed at the new version), and a brand-new tuple is written with a fresh `xmin`. A snapshot (oldest running XID `xmin`, next-unassigned XID `xmax`, and the in-progress list `xip`) decides which version each transaction sees. HOT (heap-only tuple) updates avoid touching indexes when no indexed column changed and the new version fits on the same page.

I demonstrated this end-to-end with `pageinspect`:

```sql
CREATE TABLE acct (id int PRIMARY KEY, balance int);
INSERT INTO acct VALUES (1, 100);
SELECT xmin, xmax, ctid, * FROM acct WHERE id=1;
  xmin | xmax | ctid  | id | balance
  -----+------+-------+----+--------
   808 |    0 | (0,1) |  1 |     100      <- created by txid 808, not deleted (xmax=0)

UPDATE acct SET balance = 200 WHERE id=1;
SELECT xmin, xmax, ctid, * FROM acct WHERE id=1;
  xmin | xmax | ctid  | id | balance
  -----+------+-------+----+--------
   809 |    0 | (0,2) |  1 |     200      <- a NEW version, created by txid 809

-- Both physical versions still live on the page (raw heap inspection):
SELECT lp, t_xmin, t_xmax, t_ctid FROM heap_page_items(get_raw_page('acct',0));
  lp | t_xmin | t_xmax | t_ctid
  ---+--------+--------+-------
   1 |    808 |    809 | (0,2)   <- OLD version: xmax=809, ctid points to the new one
   2 |    809 |      0 | (0,2)   <- NEW (live) version
```

The old row is not erased. It becomes a dead tuple. That is exactly why bloat happens and why VACUUM exists.

### 3.4 VACUUM, and why it is necessary

Because dead tuples accumulate, PostgreSQL needs VACUUM to:
1. reclaim dead tuples so the space is reusable (plain VACUUM marks space reusable within the table, while `VACUUM FULL` rewrites the table to return space to the OS but takes an exclusive lock);
2. prevent transaction-ID wraparound. XIDs are a 32-bit counter, so very old live rows must be frozen (marked visible to everyone) before their XID could appear to be "in the future";
3. update the Visibility Map (which lets future vacuums and index-only scans skip all-visible pages) and the Free Space Map.

Continuing the experiment, VACUUM reclaimed the dead version:

```sql
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname='acct';
  n_live_tup | n_dead_tup
  -----------+-----------
           1 |          1          <- one dead tuple waiting

VACUUM acct;
SELECT lp, lp_flags, t_xmin, t_xmax FROM heap_page_items(get_raw_page('acct',0));
  lp | lp_flags | t_xmin | t_xmax
  ---+----------+--------+-------
   1 |        2 |        |             <- lp_flags=2 (LP_DEAD/unused): slot reclaimed
   2 |        1 |    809 |      0       <- live version remains
```

Autovacuum does this automatically. By default it triggers a table once dead tuples exceed `50 + 0.2 × row_count` (the `0.2` is `autovacuum_vacuum_scale_factor`).

### 3.5 WAL: Write-Ahead Logging

The durability contract is simple: the WAL record describing a change is flushed to disk (`fsync`) before the modified data page is, and before COMMIT returns. This means we only have to force the small sequential log to disk on commit, not every scattered data page. Data pages can be written lazily later.

Key pieces:
- a WAL record is a header (length, prev pointer, XID, resource-manager id, CRC) plus the change payload, and records live in 16 MB segment files under `pg_wal/`;
- the LSN (Log Sequence Number) is a 64-bit byte offset into the WAL. Each page stores the LSN of the last change to it, which enforces WAL-before-data;
- full-page writes (on by default) means the first change to a page after a checkpoint logs the entire 8 KB page image, protecting against torn pages (a half-written page after an OS crash);
- a checkpoint flushes all dirty buffers and records a checkpoint LSN. It is triggered every `checkpoint_timeout` (default 5 min) or when `max_wal_size` (default 1 GB) is about to be exceeded;
- crash recovery (REDO): on restart PostgreSQL finds the last checkpoint and replays the WAL forward to bring data files back to a consistent state. There is no separate UNDO log, because aborted transactions just become invisible dead tuples thanks to MVCC.

Measured live, a 100k-row insert generated 15 MB of WAL:

```sql
SELECT pg_current_wal_lsn() AS before \gset
INSERT INTO wal_demo SELECT g, repeat('x',100) FROM generate_series(1,100000) g;
SELECT pg_size_pretty(pg_current_wal_lsn() - :'before') ;
  pg_size_pretty
  --------------
  15 MB                   <- WAL written by that one statement
```
```
wal_level=replica  checkpoint_timeout=300s  max_wal_size=1024MB  full_page_writes=on
```

### 3.6 Query Planner and Statistics

PostgreSQL is cost-based. For a query it enumerates plans (scan methods, join methods, join orders) and picks the one with the lowest estimated cost. Costs are computed from statistics that `ANALYZE` collects into `pg_statistic` (readable via the `pg_stats` view): `n_distinct`, `null_frac`, `most_common_vals`/`most_common_freqs` (for skew), and a `histogram_bounds` equi-depth histogram (for ranges). Costs use tunable constants, such as `seq_page_cost` = 1.0 and `random_page_cost` = 4.0 (random I/O is treated as "4 times a sequential read"), plus small CPU-per-tuple costs.

I dumped the actual stats the planner used:

```sql
SELECT attname, n_distinct, most_common_vals, most_common_freqs
FROM pg_stats WHERE tablename='customers';

attname     | n_distinct | most_common_vals                | most_common_freqs
------------+------------+---------------------------------+----------------------------------
 customer_id|         -1 |                                 |          -- -1 = unique (a key)
 country    |          5 | {UK,Germany,USA,Japan,India}    | {0.203,0.202,0.201,0.198,0.195}
```

`n_distinct = -1` tells the planner that `customer_id` is unique. `country` has 5 roughly-equal values, so a `country = 'India'` predicate is about 20% selective, and that directly drives the plan choices below.

---

## 4. Design Trade-Offs

- **MVCC vs in-place updates.** PostgreSQL's append-style MVCC gives lock-free reads and trivially-correct rollback (just do not make the row visible), but it creates dead tuples and bloat, which is the price paid through VACUUM. MySQL/InnoDB makes the opposite choice with in-place updates plus undo logs.
- **Buffer cache in shared memory.** Sharing one cache across all backends maximizes hit rate, but the fixed array and partitioned locks (`shared_buffers`, BufMapping partitions) mean you cannot just set it to "all RAM." Beyond about 25% of RAM, the OS page cache and double-buffering effects take over.
- **WAL.** It turns expensive random data-page writes into one cheap sequential log flush per commit, and it gives replication and point-in-time recovery for free. The costs are extra write volume (full-page writes can dominate just after a checkpoint) and recovery time proportional to the WAL since the last checkpoint, which is the `checkpoint_timeout` vs recovery-time trade-off.
- **Cost-based planning.** It adapts to the data shape and is usually excellent, but it is only as good as its statistics. Stale stats lead to bad estimates and therefore bad plans, which is why autovacuum also runs ANALYZE.

---

## 5. Experiments / Observations

### EXPLAIN (ANALYZE, BUFFERS) on a 3-table join

```sql
EXPLAIN (ANALYZE, BUFFERS)
SELECT c.country, p.category, count(*) n_orders, sum(o.quantity*p.price) revenue
FROM orders o
JOIN customers c ON c.customer_id = o.customer_id
JOIN products  p ON p.product_id  = o.product_id
WHERE c.country = 'India' AND o.order_date >= DATE '2025-01-01'
GROUP BY c.country, p.category
ORDER BY revenue DESC;
```

Real output (abridged):

```
 Sort  (cost=8256.13..8256.15 rows=5) (actual time=40.2..42.8 rows=5 loops=1)
   Buffers: shared hit=4435 read=10
   -> Finalize GroupAggregate
      -> Gather Merge   Workers Planned: 2  Workers Launched: 2
         -> Partial HashAggregate
            -> Hash Join  (cost=780.87..6955.35 rows=23920) (actual rows=19579 loops=3)
                 Hash Cond: (o.product_id = p.product_id)
               -> Hash Join  (actual rows=19579 loops=3)
                    Hash Cond: (o.customer_id = c.customer_id)
                  -> Parallel Seq Scan on orders o
                       Filter: (order_date >= '2025-01-01')
                       Rows Removed by Filter: 67673
                  -> Hash
                     -> Bitmap Heap Scan on customers c
                          Recheck Cond: (country = 'India')
                        -> Bitmap Index Scan on idx_customers_country (actual rows=9880)
               -> Hash -> Seq Scan on products p (rows=1000)
 Planning Time: 1.523 ms
 Execution Time: 43.042 ms
```

Reading the plan:
- `cost=START..TOTAL` are the planner's estimates (arbitrary cost units), while `actual time=START..TOTAL` are measured milliseconds. Comparing estimated `rows` to `actual rows` is how you spot bad statistics. Here `rows=23920` vs `actual 19579` is a good estimate.
- The planner chose two parallel workers and hash joins because the join touches a large fraction of `orders`.
- For `customers` it used a Bitmap Index Scan on `idx_customers_country`, because the `country='India'` predicate is selective enough (about 20%, matching the `most_common_freqs` from section 3.6).
- `products` (only 1000 rows) is read by a plain Seq Scan, because for a tiny table scanning is cheaper than indexing.
- `Buffers: shared hit=4435 read=10` means almost everything came from `shared_buffers` (the buffer manager in action), and only 10 pages missed to disk.
- `loops=3`: the `actual rows` and `actual time` on the parallel nodes are per-loop (3 = leader plus 2 workers), which is a classic gotcha.

### Selectivity decides the access method

Same column, two predicates, and the planner flips strategy based on statistics:

```sql
-- Highly selective (one customer): uses the index
EXPLAIN ANALYZE SELECT * FROM orders WHERE customer_id = 12345;
  Bitmap Heap Scan on orders (actual rows=12)
    -> Bitmap Index Scan on idx_orders_customer    -- 0.2 ms total

-- Matches ~everything: ignores indexes, parallel-scans the whole table
EXPLAIN ANALYZE SELECT count(*) FROM orders WHERE quantity >= 1;
  Parallel Seq Scan on orders (actual rows=166667 loops=3)   -- 33 ms
```

Observation: an index is not always a win. The planner correctly used the index for the 12-row result, but it chose a full parallel scan when the predicate matched all 500k rows. That is exactly what the cost model (`random_page_cost=4` vs `seq_page_cost=1`) predicts.

---

## 6. Key Learnings

1. **Pages move through one shared cache.** Nothing is read from a table without going through `shared_buffers`. Clock-sweep plus `usage_count` approximate LRU, and the `BUFFERS` counters let you literally see hit vs read.
2. **MVCC means a new version per change, not an in-place edit.** I watched `xmin`, `xmax`, and `ctid` produce a second physical row on an `UPDATE`, and saw it become a dead tuple. That is the concrete reason readers and writers do not block each other.
3. **VACUUM is not optional housekeeping, it is load-bearing.** Without it you get bloat and eventually a transaction-ID wraparound shutdown. I saw a dead tuple's slot get reclaimed (`lp_flags`) after VACUUM.
4. **WAL buys durability cheaply.** One sequential log flush per commit protects scattered random data pages, and recovery just replays forward from the last checkpoint. A 100k-row insert produced a measurable 15 MB of WAL.
5. **Good plans depend entirely on good statistics.** The same query chose parallel hash joins, a bitmap index scan, and a seq scan in one plan, and each choice traces back to `pg_stats` (`n_distinct`, `most_common_freqs`) and the cost constants. Estimated-vs-actual rows is the first thing to check when a query is slow.

---

## References

- The Internals of PostgreSQL: [Buffer Manager (ch. 8)](https://www.interdb.jp/pg/pgsql08.html)
- PostgreSQL source: [`nbtree/README`](https://github.com/postgres/postgres/blob/master/src/backend/access/nbtree/README)
- PostgreSQL docs: [Page Layout](https://www.postgresql.org/docs/current/storage-page-layout.html), [Routine Vacuuming](https://www.postgresql.org/docs/current/routine-vacuuming.html), [WAL Internals](https://www.postgresql.org/docs/current/wal-internals.html) and [Configuration](https://www.postgresql.org/docs/current/wal-configuration.html), [Using EXPLAIN](https://www.postgresql.org/docs/current/using-explain.html), [Planner Statistics](https://www.postgresql.org/docs/current/planner-stats.html), [Query Planning costs](https://www.postgresql.org/docs/current/runtime-config-query.html)
- All experiment output captured live on PostgreSQL 18.3.
