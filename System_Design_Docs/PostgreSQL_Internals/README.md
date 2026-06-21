# PostgreSQL Internal Architecture

I studied four core parts of PostgreSQL: the buffer manager, the B-tree index
(nbtree), MVCC, and WAL. My focus is why each part exists and what it trades.

## 1. Problem Background

PostgreSQL must serve many users at once and never lose committed data. To do
that it solves four problems:

- Disk is slow -> cache hot pages in memory (buffer manager).
- Finding rows fast -> keep sorted indexes (B-tree / nbtree).
- Many users at once -> let them work without blocking each other (MVCC).
- Crashes happen -> make committed data survive (WAL + checkpoints + recovery).

## 2. Architecture Overview

```
clients -> postmaster -> forks one backend process per client
                              |
   Parser -> Rewriter -> Planner -> Executor
                              |  reads/writes pages
   SHARED MEMORY: shared_buffers (page cache) + WAL buffers + locks
        |  flush pages                 |  flush log
        v                              v
   data files (8 KB pages)         WAL files (pg_wal/)
helpers: WAL writer, checkpointer, background writer, autovacuum
```

Flow: a backend parses the SQL, the planner picks a plan from statistics, the
executor asks the buffer manager for pages, changes go to WAL first then to
pages in memory, and COMMIT flushes WAL to disk so it is durable.

## 3. Internal Design

- **Buffer manager** (`src/backend/storage/buffer/`). `shared_buffers` is one
  array of 8 KB page slots shared by all backends. A hash table maps
  (table, block) -> slot. On a miss, PostgreSQL reads the page from disk and
  may evict a victim chosen by *clock-sweep* (cheap approximate LRU using a
  usage count). A page in use is *pinned* so it can't be evicted. Dirty pages
  are written back later because the change is already safe in WAL.
- **B-tree (nbtree).** The default index. Leaves hold the key plus a TID
  (physical row location) and are linked left-right for range scans. Search:
  root -> binary-search keys -> follow child -> repeat to a leaf -> follow the
  TID to the heap. A full leaf *splits* and pushes a separator key up; a
  right-link pointer lets other backends keep searching during a split.
- **MVCC.** Each row version stores hidden xmin (creator txn) and xmax
  (deleter txn). UPDATE never overwrites - it writes a new version and marks the
  old one expired. A transaction sees versions valid for its *snapshot*, so
  readers don't block writers. This is why VACUUM is needed: it reclaims dead
  versions, updates the visibility map (for index-only scans), and prevents
  transaction-id wraparound.
- **WAL.** Rule: write the log record of a change before the data page. On
  COMMIT, WAL is flushed (`fsync`) so the commit survives a crash even if data
  pages are still in memory. A checkpoint writes dirty pages to disk and marks a
  point; recovery replays WAL forward from the last checkpoint. WAL also powers
  replication.
- **Planning.** The planner estimates costs from statistics in `pg_statistic`
  (row counts, common values, histograms) to choose scans and joins. Stale
  stats give bad plans, so I'd run ANALYZE.

## 4. Design Trade-Offs

- Shared buffer cache: great hit rates, but needs locking and a cheap eviction
  policy (clock-sweep).
- MVCC: excellent concurrency, but causes bloat and forces VACUUM. InnoDB keeps
  old versions in undo (table stays compact); PostgreSQL keeps them in the
  table - simpler, but more cleanup.
- Heap + separate indexes: cheap updates, but lookups usually do a second hop
  to the heap.
- WAL: turns many random page writes into one sequential log write, at the cost
  of writing data twice and needing checkpoints.
- Process per connection: stable and isolated, but high connection counts need
  a pooler (PgBouncer).

## 5. Experiments / Observations

> `psql` was not installed here, so this `EXPLAIN ANALYZE` output is a
> representative example (illustrative, not run live).

```
HashAggregate (rows=812)
  -> Hash Join  (cost=120..2200 rows=24000) (actual rows=23950)
       Hash Cond: (o.customer_id = c.id)
       -> Seq Scan on orders o  Filter: created_at >= '2026-01-01'
                                 Rows Removed by Filter: 11050
       -> Hash -> Seq Scan on customers c
Planning Time: 0.40 ms   Execution Time: 19.6 ms
```

How I read it:
- `cost` is the planner's *estimate*; `actual` is what really happened. A big
  gap between estimated and actual rows usually means stale stats -> run ANALYZE.
- It chose a Hash Join (build a hash on small `customers`, probe with `orders`)
  and a Seq Scan on `orders` because the date filter still matches most rows.

Cross-check I actually ran: in the PostgreSQL_vs_SQLite topic I watched a real
plan flip from `SCAN` to `SEARCH USING INDEX` after creating an index - same
idea, smaller engine.

## 6. Key Learnings

- Pages move through the buffer manager by hash lookup -> pin -> use, with
  clock-sweep picking victims; delaying dirty writes is safe because WAL has it.
- MVCC means versions, not locks (xmin/xmax + snapshots) - and the leftover dead
  versions are exactly why VACUUM exists.
- WAL gives durability with one rule (log before data) and recovers by replaying
  from the last checkpoint.
- Plans are only as good as the statistics; EXPLAIN ANALYZE compares estimated
  vs actual rows.
- The recurring theme: trade memory and background cleanup for concurrency and
  durability.

### References
PostgreSQL source `storage/buffer/` and `access/nbtree/`; PostgreSQL docs (MVCC,
WAL, Vacuuming, Planner Statistics, EXPLAIN); *The Internals of PostgreSQL*.
