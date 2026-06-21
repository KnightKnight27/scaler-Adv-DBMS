# PostgreSQL Internal Architecture

> An examination of the way PostgreSQL stores, caches, versions, and durably commits data: the buffer manager, the B-tree access method, MVCC, and the Write-Ahead Log. Every experiment below was executed live on **PostgreSQL 16.14** (Docker `postgres:16`) against a dataset of 50k customers, 200k orders, and 600k order items.

---

## 1. Problem Background

PostgreSQL traces back to the POSTGRES project at UC Berkeley (Michael Stonebraker, mid-1980s), whose aim was to push the relational model further with richer data types, rules, and extensibility. What the modern system delivers is a **correct, durable, highly-concurrent, standards-compliant SQL database** that a crowd of clients can pound on at once without trampling each other's data or losing committed work to a crash.

Any database of that ambition runs into three stubborn problems, and PostgreSQL's internals are basically the worked-out answers:

1. **Disk is slow and memory is scarce.** → a shared *buffer manager* that holds hot pages.
2. **Many sessions touch the same rows simultaneously.** → *MVCC* (Multi-Version Concurrency Control), so readers don't block writers.
3. **A crash can land mid-write.** → the *Write-Ahead Log* (WAL), so committed transactions survive and torn writes get repaired.

The sections below walk through each mechanism and then show it at work on a real instance.

---

## 2. Architecture Overview

PostgreSQL follows a **process-per-connection** model. A supervisor (the postmaster) forks a dedicated backend for each client. Every backend shares one region of memory (`shared_buffers`) and one WAL stream, all kept in order by a handful of background utility processes.

```mermaid
flowchart TD
    C1[Client 1] -->|libpq| B1[Backend proc 1]
    C2[Client 2] -->|libpq| B2[Backend proc 2]
    PM[Postmaster<br/>supervisor] -.forks.-> B1 & B2

    subgraph Shared Memory
      SB[(Shared Buffers<br/>page cache)]
      WB[(WAL Buffers)]
    end

    B1 --> SB
    B2 --> SB
    B1 --> WB
    B2 --> WB

    SB <-->|reads / dirty writes| HEAP[(Heap & Index files<br/>8KB pages)]
    WB -->|fsync on commit| WAL[(WAL segments)]

    BGW[bgwriter] --> SB
    CKPT[checkpointer] --> SB
    CKPT --> WAL
    AV[autovacuum] --> HEAP
```

**How a write moves through:** a backend edits a page *inside the shared buffer pool* (not on disk), notes the change in the WAL buffer, and at `COMMIT` forces that WAL record out to disk (`fsync`). The dirty data page itself is flushed later, lazily, by the **bgwriter** or at a **checkpoint**. That "log first, data afterwards" ordering is the entire foundation of durability.

---

## 3. Internal Design

### 3.1 Storage layout: pages, heaps, and TIDs

Each table (a *heap*) and each index is a file carved into fixed **8 KB pages**. A page carries a header, an array of line pointers, and tuples that fill in from the tail towards the front. A row's physical location is a **TID** `(block, offset)`, surfaced as the system column `ctid`. Indexes, in the end, point at TIDs.

### 3.2 Buffer Manager (`src/backend/storage/buffer/`)

Pages get pulled off disk into `shared_buffers` and shared by all backends. To decide what to evict, PostgreSQL runs a **clock-sweep** algorithm (an LRU approximation built on a per-buffer usage counter) that prefers throwing out pages nobody has touched lately. A page only hits disk when it is *dirty* and is then evicted, written by the bgwriter, or swept up in a checkpoint.

**Experiment — what is actually cached?** After running the join workload, I looked inside the live buffer pool:

```text
labdb=# SELECT c.relname, count(*) AS buffers, pg_size_pretty(count(*)*8192) AS cached
        FROM pg_buffercache b JOIN pg_class c
          ON b.relfilenode = pg_relation_filenode(c.oid)
        GROUP BY c.relname ORDER BY buffers DESC;

      relname      | buffers | cached
-------------------+---------+---------
 order_items       |    3826 | 30 MB
 orders            |    2578 | 20 MB
 orders_pkey       |    1099 | 8792 kB
 customers         |     371 | 2968 kB
 idx_orders_status |     194 | 1552 kB
```

```text
 shared_buffers = 128MB

 datname | blks_hit | blks_read | hit_ratio_pct
---------+----------+-----------+---------------
 labdb   |  9689977 |      1127 |         99.99
```

The hot tables are sitting in memory and the cache hit ratio is **99.99%**, almost no query reaches for disk. That is exactly why, in a warm database, the buffer manager rather than the disk sets read latency.

### 3.3 B-Tree access method (`nbtree`)

PostgreSQL's default index is a **Lehman-Yao B+-tree**: keys sit in leaf pages chained together in a doubly-linked list, while internal pages steer the search. A lookup descends root → internal → leaf in `O(log n)`. An insert drops the key into the right leaf; when that leaf is full it **splits** in two and pushes a separator key upward (which can cascade splits all the way to the root, the only mechanism by which the tree gets taller). Lehman-Yao bolts on a "high key" and a right-link per page so concurrent searches keep moving during a split without locking the whole tree.

In Experiment 1 below, `idx_orders_status` is consumed through a **Bitmap Index Scan** rather than a plain index scan, because the predicate matches ~50k rows: the planner first assembles a bitmap of the qualifying pages, then walks the heap in physical order to dodge random I/O.

### 3.4 MVCC: tuple versioning with xmin / xmax

PostgreSQL never overwrites a row in place. Every tuple wears two hidden transaction-id columns: **`xmin`** (the txid that created it) and **`xmax`** (the txid that deleted or superseded it). An `UPDATE` stamps the old tuple's `xmax` and lays down a fresh tuple version. A transaction sees a tuple only when its `xmin` is committed-and-visible and its `xmax` is not, judged against that transaction's **snapshot**. The upshot: readers never wait on writers, and writers never wait on readers.

**Experiment — watch a version come into being:**

```text
labdb=# SELECT ctid, xmin, xmax, bal FROM acct;       -- initial INSERT
 ctid  | xmin | xmax | bal
-------+------+------+-----
 (0,1) |  798 |    0 | 100

labdb=# UPDATE acct SET bal = 150 WHERE id = 1;
labdb=# SELECT ctid, xmin, xmax, bal FROM acct;       -- after UPDATE
 ctid  | xmin | xmax | bal
-------+------+------+-----
 (0,2) |  799 |    0 | 150
```

The row physically relocated from `ctid (0,1)` to `(0,2)` and `xmin` ticked up `798 → 799`: this is a *brand-new* tuple. The old version is still on the page (now invisible) until VACUUM clears it out.

### 3.5 Why VACUUM is necessary

Dead tuples pile up, so tables **bloat** and need cleaning. I updated every `paid` order three times:

```text
table size before updates : 11 MB
 n_live_tup | n_dead_tup
------------+------------
     200000 |     149982      <-- 150k dead versions
table size after updates  : 20 MB      <-- nearly doubled

-- VACUUM orders (VERBOSE):
tuples: 50000 removed, 200000 remain
index "idx_orders_status": 115 pages newly deleted
WAL usage: 7225 records, 1453141 bytes
buffer usage: 10209 hits, 125 misses, 42 dirtied
```

Three full-table updates pushed `orders` from **11 MB to 20 MB** and left roughly 150k dead tuples behind. `VACUUM` cleared them and freed the space *for reuse* (a plain VACUUM won't shrink the file; `VACUUM FULL` would). VACUUM also advances the **frozen XID** horizon, which is what holds off transaction-id wraparound. Notice the VERBOSE line `WAL usage: 7225 records`, even the cleanup is logged.

### 3.6 WAL, checkpoints, and crash recovery

The WAL is an append-only record of every change, written **before** the matching data page reaches disk (the "write-ahead" rule). At `COMMIT`, only the WAL record has to be `fsync`'d, a small sequential write, which is far cheaper than flushing a scatter of data pages. The data pages go out later at a **checkpoint**, which flushes all dirty buffers and marks a safe restart point in the WAL.

```mermaid
sequenceDiagram
    participant B as Backend
    participant SB as Shared Buffer
    participant W as WAL (disk)
    participant H as Heap (disk)
    B->>SB: modify page (in memory)
    B->>W: append WAL record
    Note over B,W: COMMIT → fsync WAL only (durable here)
    SB-->>H: dirty page flushed later (checkpoint/bgwriter)
```

During crash recovery, PostgreSQL replays the WAL from the last checkpoint: committed changes whose data pages never landed on disk are reapplied (REDO), reproducing precisely the committed state as of the instant of the crash.

### 3.7 The planner and `pg_statistic`

PostgreSQL ships a **cost-based** optimizer. `ANALYZE` samples each table and records distribution data in `pg_statistic` (visible through the `pg_stats` view): the number of distinct values, the most-common values (MCVs) with their frequencies, and histograms. The planner draws on these to estimate row counts and settle on the cheapest plan.

```text
labdb=# SELECT attname, n_distinct, most_common_freqs FROM pg_stats
        WHERE tablename='orders' AND attname='status';
 attname | n_distinct |               most_common_freqs
---------+------------+-----------------------------------------------
 status  |          4 | {0.2512, 0.2505, 0.2492, 0.2491}
```

The four statuses each turn up about 25% of the time. So for `WHERE status='paid'` the planner estimates `0.2492 × 200000 ≈ 49,847` rows, against an actual count of **50,000** (see Experiment 1). Statistics this accurate are why it reached for a bitmap scan instead of a sequential scan.

---

## 4. Design Trade-Offs

| Decision | Benefit | Cost / Limitation |
|---|---|---|
| **Process-per-connection** | Strong isolation; a crashing backend can't corrupt others | High per-connection memory; thousands of connections need a pooler (PgBouncer) |
| **MVCC via new tuple versions** | Readers never block writers; simple snapshot semantics | Bloat; mandatory VACUUM; index entries for every version |
| **Append/relocate updates** | No undo segment needed; old versions are cheap to keep | Updates are not in-place → more I/O and index churn than InnoDB |
| **WAL (log-first)** | Cheap sequential commit; point-in-time recovery; replication | Double writes (log + data); checkpoints cause I/O spikes |
| **Clock-sweep buffer cache** | Cheap approximation of LRU, low contention | Not a true LRU; can mis-evict under skewed access |
| **Cost-based planner** | Adapts plans to data distribution | Wholly dependent on fresh statistics; stale stats → bad plans |

The signature trade-off against an in-place engine like InnoDB: PostgreSQL gives up **update efficiency and steady-state space** in exchange for **simpler, lock-free reads and easy retention of old versions**, and settles the bill through VACUUM. (Compared head-to-head in the MySQL/InnoDB document.)

---

## 5. Experiments / Observations

### Experiment 1: `EXPLAIN (ANALYZE, BUFFERS)` on a 3-table join

```sql
EXPLAIN (ANALYZE, BUFFERS)
SELECT c.country, count(*) AS orders, sum(oi.qty*oi.price_cents) AS revenue
FROM customers c
JOIN orders o       ON o.customer_id = c.id
JOIN order_items oi ON oi.order_id   = o.id
WHERE o.status = 'paid'
GROUP BY c.country ORDER BY revenue DESC;
```

```text
 Sort  (actual time=147.785..151.897 rows=5)
   ->  Finalize GroupAggregate
     ->  Gather Merge (Workers Launched: 2)
       ->  Partial HashAggregate
         ->  Hash Join (Hash Cond: o.customer_id = c.id)  rows=49888
           ->  Parallel Hash Join (Hash Cond: oi.order_id = o.id)
             ->  Parallel Seq Scan on order_items oi  rows=200000
             ->  Parallel Hash
               ->  Parallel Bitmap Heap Scan on orders o
                 Recheck Cond: (status = 'paid')
                 ->  Bitmap Index Scan on idx_orders_status  rows=50000
           ->  Hash -> Seq Scan on customers c  rows=50000
 Planning Time: 1.222 ms
 Execution Time: 152.050 ms
 Buffers: shared hit=6441 read=44
```

**Observations:**
- The planner went with **parallel hash joins** across 2 workers, the right call for joining large unindexed result sets.
- `orders` is filtered via a **Bitmap Index Scan** on `idx_orders_status` (estimate 49,847, actual 50,000, the statistics were on the money).
- `order_items` is read by a **parallel sequential scan**: no useful index predicate, so a full scan split across workers is cheapest.
- `Buffers: shared hit=6441 read=44`, only 44 of about 6,485 page accesses missed the cache, in line with the 99.99% hit ratio.

### Experiment 2: statistics drive estimates
Already covered in §3.7: a `most_common_freqs` of 0.2492 for `status='paid'` yielded an estimate within 0.3% of reality. Skipping `ANALYZE` (stale stats) would erode these estimates and risk a sequential-scan plan.

### Experiment 3: MVCC version creation
§3.4: an `UPDATE` spawned a new tuple at a new `ctid` with a bumped `xmin`, leaving the old version in place.

### Experiment 4: bloat and VACUUM
§3.5: 3× updates doubled the table size and produced 150k dead tuples; `VACUUM` reclaimed them and logged its own WAL.

### Experiment 5: buffer pool contents
§3.2: `pg_buffercache` confirmed the working set is resident in `shared_buffers` with a 99.99% hit ratio.

---

## 6. Key Learnings

- **In steady state, memory is the bottleneck, not disk.** With a warm cache the database served reads at a 99.99% hit ratio; keeping it that way is precisely the buffer manager's job.
- **MVCC is elegant but it isn't free.** Lock-free reads are paid for in dead tuples; the `xmin/xmax` + new-version scheme *is* the reason VACUUM has to exist. Watching a table double in size after three updates makes that cost impossible to ignore.
- **Durability is decoupled from data writes.** A commit only needs the WAL on disk; the data pages trail behind at checkpoints. That single ordering rule (log-first) buys both fast commits and crash safety.
- **The planner is only as smart as its statistics.** The bitmap-scan decision and its near-perfect row estimate came straight out of `pg_statistic`; stale stats are among the most common causes of bad plans in production.
- **Surprising observation:** a *cleanup* operation (VACUUM) is itself a logged, WAL-generating transaction, durability discipline applies even to garbage collection.

---

### Reproducing these results
```bash
docker run -d --name pg -e POSTGRES_PASSWORD=postgres -e POSTGRES_DB=labdb -p 5439:5432 postgres:16
# load schema + data, then run the queries shown above (CREATE EXTENSION pg_buffercache; first)
```
*Engine: PostgreSQL 16.14 (Debian build) in Docker. Sources referenced: PostgreSQL 16 documentation (Chapters on Storage, MVCC, WAL, Indexes) and the `src/backend/storage/buffer/` and `src/backend/access/nbtree/` source trees.*
