# Topic 2: PostgreSQL Internal Architecture

> **Course:** Advanced DBMS | **Roll Number:** 24BCS10355

---

## 1. Problem Background

PostgreSQL is not just a database — it is an engineering manifesto for how to build a *correct*, *extensible*, and *concurrent* relational system. Understanding its internals means understanding *why* each subsystem exists, what problem it was specifically designed to solve, and what it costs.

The four pillars of PostgreSQL's internal design:

1. **Buffer Manager** — How PostgreSQL moves data between disk and RAM without losing it
2. **B-Tree (nbtree)** — How PostgreSQL finds data in O(log n) even across billions of rows
3. **MVCC** — How PostgreSQL lets thousands of transactions run simultaneously without fighting over locks
4. **WAL** — How PostgreSQL guarantees that a committed transaction survives a power failure

These are not independent — they are deeply intertwined. WAL protects both heap changes *and* B-tree index changes. The buffer manager holds both data pages *and* index pages. MVCC writes tuple versions that WAL then records.

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        PostgreSQL Backend Process                           │
│                                                                             │
│  Client Query                                                               │
│       │                                                                     │
│  ┌────▼────┐   ┌──────────┐   ┌─────────────────┐   ┌───────────────────┐ │
│  │ Parser  │──▶│ Rewriter │──▶│  Planner/       │──▶│    Executor       │ │
│  │(gram.y) │   │ (rules)  │   │  Optimizer      │   │ (plan nodes)      │ │
│  └─────────┘   └──────────┘   │  ┌────────────┐ │   └────────┬──────────┘ │
│                               │  │pg_statistic│ │            │            │
│                               │  │(histograms)│ │            │            │
│                               │  └────────────┘ │            │            │
│                               └─────────────────┘            │            │
│                                                               │            │
│  ┌────────────────────────────────────────────────────────────▼──────────┐ │
│  │                         Buffer Manager                                │ │
│  │  ┌──────────────────────────────────────────────────────────────────┐ │ │
│  │  │  Shared Buffers (shared_buffers = 128MB default, 25% RAM ideal)  │ │ │
│  │  │  [Page][Page][Page]...[Page]  ← 8KB pages, pinned by backends   │ │ │
│  │  │  Clock-sweep replacement policy                                  │ │ │
│  │  └──────────────────────────────────────────────────────────────────┘ │ │
│  │  Buffer Table (hash: RelFileNode+BlockNum → buffer slot)              │ │
│  └───────────────────────────────────┬────────────────────────────────────┘ │
│                                      │ (dirty pages)                        │
│  ┌───────────────────────────────────▼────────────────────────────────────┐ │
│  │                         WAL (Write-Ahead Log)                          │ │
│  │  WAL Buffers → WAL Writer → pg_wal/ (16MB segments)                   │ │
│  │  LSN (Log Sequence Number) stamps every WAL record                    │ │
│  └────────────────────────────────────────────────────────────────────────┘ │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
              │                                         │
    ┌─────────▼──────────┐                   ┌─────────▼──────────┐
    │  Heap Files        │                   │  WAL Segments       │
    │  Index Files       │                   │  (pg_wal/)          │
    │  ($PGDATA/base/)   │                   └────────────────────┘
    └────────────────────┘
```

---

## 3. Internal Design

### 3.1 Buffer Manager

**Location in source:** `src/backend/storage/buffer/`

The buffer manager is PostgreSQL's central memory traffic controller. Every access to a data page — whether for reading a row or writing an update — goes through the buffer manager. It maintains the `shared_buffers` pool, a region of shared memory accessible by all backend processes simultaneously.

#### How a Page Read Works

```
Backend wants to read page (rel=orders, blk=42):

1. Compute buffer tag: { RelFileNode, ForkNumber, BlockNumber }
2. Look up buffer table (hash map: tag → buffer slot)
3a. Cache HIT  → increment pin count, return buffer slot
3b. Cache MISS → select victim buffer (clock-sweep)
      → if victim is dirty: write it to disk first (via bgwriter or inline)
      → read page 42 from disk into the victim slot
      → update buffer table
      → return buffer slot
4. Backend reads page while holding pin (prevents eviction)
5. On done: unpin (decrement pin count)
```

#### Clock-Sweep Replacement

PostgreSQL uses a **clock-sweep** algorithm (a variant of LRU). Each buffer has a `usage_count` (0–5). When a buffer is accessed, its usage_count increments. When the clock hand sweeps past a buffer, it decrements usage_count. A buffer with usage_count=0 is a candidate for eviction.

This is cheaper than true LRU (no doubly-linked list movement on every access) and avoids LRU's susceptibility to sequential scans washing out the entire cache.

#### Buffer Replacement Problem: Sequential Scans

A large sequential scan (`SELECT * FROM orders` on a 5GB table) would evict the entire shared_buffers, thrashing the cache for all other queries. PostgreSQL detects this and uses a **small ring buffer** for sequential scans (a few MB), keeping the main buffer pool intact.

#### Dirty Page Management

- **bgwriter** process: proactively writes dirty buffers to disk during idle periods, so backends rarely have to do it inline
- **checkpointer** process: at each checkpoint, flushes all dirty buffers, then writes a checkpoint record to WAL. After a checkpoint, crash recovery only needs to replay WAL from that checkpoint forward.

```
Checkpoint sequence:
1. Write CHECKPOINT_START WAL record
2. Flush all dirty shared_buffers to disk
3. Write CHECKPOINT_END WAL record with redo pointer
4. Old WAL segments before the redo point can be recycled
```

---

### 3.2 B-Tree Implementation (nbtree)

**Location in source:** `src/backend/access/nbtree/`

PostgreSQL's B-tree is a B+tree variant (all data in leaf pages, interior pages only hold keys for navigation). It is used for `CREATE INDEX`, primary key constraints, and unique constraints.

#### B-Tree Page Layout

```
Leaf Page (8KB):
┌──────────────────────────────────────────┐
│  PageHeader (24 bytes)                   │
│  BTPageOpaqueData: left/right page links │
│  (enables in-order leaf traversal)       │
├──────────────────────────────────────────┤
│  High Key (maximum key on this page)     │
│  (used for page pruning during scans)    │
├──────────────────────────────────────────┤
│  Item 1: (key_value, heap_tid)           │
│  Item 2: (key_value, heap_tid)           │
│  ...sorted ascending...                 │
│  Item N: (key_value, heap_tid)           │
└──────────────────────────────────────────┘

heap_tid = (block_number, offset_in_page) → points to actual row in heap
```

Interior pages hold `(key, child_page_number)` pairs. The B-tree is always balanced — all leaf pages are at the same depth.

#### Search Path

```
Find all rows where email = 'alice@example.com':

1. Read root page → binary search for key ≥ 'alice@...'
2. Follow child pointer to interior page
3. Binary search again → follow child to leaf page
4. Binary search in leaf → find (key='alice@...', tid=(42, 7))
5. Read heap page 42, tuple slot 7 → return row data
```

Total I/Os: typically 3–4 (root + 1-2 interior + 1 leaf + 1 heap). For a billion-row table, the tree is only ~6-7 levels deep — O(log n) holds.

#### Page Splits

When inserting a key into a full leaf page:
1. Allocate a new page
2. Split the old page's items 50/50 between old and new page
3. Insert the split key into the parent interior page (which may itself split — propagates upward)
4. Update sibling left/right pointers
5. All of this is WAL-logged atomically — a crash mid-split leaves the tree in a recoverable state

#### Index-Only Scans and Visibility Map

PostgreSQL's **visibility map** tracks which heap pages have no dead tuples (all tuples on the page are visible to all transactions). For index-only scans, if the heap page is marked "all-visible", PostgreSQL doesn't need to visit the heap at all — it returns data directly from the index, drastically reducing I/O.

---

### 3.3 MVCC (Multi-Version Concurrency Control)

MVCC is the heart of PostgreSQL's concurrency model. The fundamental idea: instead of locking rows on read, PostgreSQL keeps multiple versions of each row and shows each transaction the version appropriate to its snapshot.

#### Heap Tuple Versioning

Every heap tuple has a header:

```c
struct HeapTupleHeaderData {
    TransactionId t_xmin;  // XID of inserting transaction
    TransactionId t_xmax;  // XID of deleting/updating transaction (0 = live)
    ItemPointerData t_ctid; // pointer to newest version (self if newest)
    uint16 t_infomask;     // flags: HEAP_XMIN_COMMITTED, HEAP_XMAX_INVALID, etc.
    ...
}
```

**INSERT:**
```
New tuple: t_xmin=500, t_xmax=0  → "inserted by txn 500, not yet deleted"
```

**UPDATE** (PostgreSQL never modifies in place):
```
Old tuple: t_xmin=500, t_xmax=600, t_ctid→(new_page, new_off)
New tuple: t_xmin=600, t_xmax=0                                 
```

**DELETE:**
```
Tuple: t_xmin=500, t_xmax=700   → "deleted by txn 700"
```

#### Visibility Rules

For a tuple to be visible to a transaction with snapshot S:

```
Tuple is VISIBLE if:
  t_xmin committed AND t_xmin < S.xmax   (inserter committed before snapshot)
  AND (
    t_xmax = 0                            (not deleted)
    OR t_xmax did not commit              (deleter aborted)
    OR t_xmax >= S.xmax                   (deleter started after snapshot)
    OR t_xmax is in S.xip (in-progress)   (deleter not yet committed)
  )
```

This means: two transactions updating the same row see different versions — no lock required, no blocking.

#### Snapshot Isolation

When a transaction begins (in READ COMMITTED, a new snapshot per query; in REPEATABLE READ/SERIALIZABLE, one snapshot for the whole transaction):

```
Snapshot = {
  xmin:  lowest XID still active
  xmax:  next XID to be assigned
  xip:   list of currently in-progress XIDs
}
```

A tuple is "in the past" (committed before snapshot) if its t_xmin < xmin. A tuple is "in the future" (committed after snapshot) if t_xmin >= xmax. Tuples with t_xmin in xip are in-progress — invisible.

#### The VACUUM Necessity

MVCC creates **dead tuples** — old row versions no longer visible to any active transaction. These accumulate on heap pages, wasting space and forcing table scans to read more pages.

**VACUUM** (specifically `autovacuum`) scans the heap and:
1. Marks dead tuples as free space (updates the Free Space Map)
2. Removes index entries pointing to dead tuples
3. Updates the visibility map for pages that become all-live

**VACUUM FULL** rewrites the entire table to reclaim space — it acquires an exclusive lock and is more disruptive.

**Transaction ID Wraparound** — XIDs are 32-bit integers. After ~2 billion transactions, XID wraps around. VACUUM also updates `pg_class.relfrozenxid` — "freezing" old tuples by replacing their t_xmin with a special `FrozenTransactionId` that is visible to all transactions. Without this, tables eventually become unreadable (wraparound catastrophe).

---

### 3.4 WAL (Write-Ahead Logging)

**Location in source:** `src/backend/access/transam/xlog.c`

WAL is PostgreSQL's durability guarantee. The rule: **a page modification must be recorded in WAL before the modified page is written to disk.** This ensures that even if PostgreSQL crashes with dirty pages in memory, it can replay WAL to reconstruct the correct state.

#### WAL Record Structure

```
WAL Record:
┌──────────────────────────────────────────────────────┐
│  xl_tot_len   (total record length)                  │
│  xl_xid       (transaction ID)                       │
│  xl_prev      (LSN of previous WAL record)           │
│  xl_info      (resource manager + record type)       │
│  xl_rmid      (resource manager: heap, btree, etc.)  │
├──────────────────────────────────────────────────────┤
│  Data block references (which pages this affects)    │
│  Full page image (FPI) if page was modified after    │
│  last checkpoint (protects against partial writes)   │
├──────────────────────────────────────────────────────┤
│  Record-specific data (e.g., new tuple data for      │
│  INSERT, old/new values for UPDATE)                  │
└──────────────────────────────────────────────────────┘
```

Every modification — heap INSERTs, UPDATEs, DELETEs, B-tree page splits, VACUUM operations — generates WAL records. Each record gets a **LSN (Log Sequence Number)** — a monotonically increasing byte offset into the WAL stream.

#### WAL Write Path

```
Transaction executes UPDATE:
1. Backend generates WAL record in WAL buffers (in shared memory)
2. Modified heap/index pages marked dirty in buffer manager (page LSN updated)
3. On COMMIT:
   a. COMMIT WAL record written to WAL buffers
   b. WAL buffers flushed to disk (fsync or fdatasync)
   c. Only now does the transaction return "success" to client
4. bgwriter/checkpointer later flushes dirty heap/index pages to disk
```

The key insight: **WAL is always written sequentially.** Sequential writes are far faster than random writes to heap/index pages. By batching random page modifications and writing them lazily, PostgreSQL achieves much higher write throughput than if it had to fsync every page immediately.

#### Crash Recovery

```
On restart after crash:
1. Read control file → find last checkpoint LSN
2. Open WAL at checkpoint LSN
3. Replay each WAL record:
   a. Read the target page into buffer manager
   b. If page LSN < WAL record LSN → apply the WAL change
   c. If page LSN >= WAL record LSN → page already has this change, skip
4. Recovery complete → database is in consistent state
```

#### Full Page Images (FPI)

When PostgreSQL first modifies a page after a checkpoint, it writes a **full page image** (entire 8KB page) into the WAL record. This protects against **torn page writes** — a scenario where a crash occurs mid-page-write to disk, leaving a partially written (corrupt) page. On recovery, the full page image is used to restore the page before replaying the incremental change.

#### WAL and Replication

WAL is also the foundation of **streaming replication**. A standby PostgreSQL server connects to the primary and receives WAL records in real-time, applying them to its own copy of the database. Since WAL is a complete record of all changes, the standby is an exact replica.

---

### 3.5 Query Planning and pg_statistic

The planner reads statistics from `pg_statistic` (populated by `ANALYZE`) to estimate row counts and choose optimal join strategies.

Key statistics maintained:
- `n_distinct`: estimated number of distinct values in a column
- `most_common_vals` / `most_common_freqs`: histogram of frequent values
- `histogram_bounds`: percentile distribution of values

```sql
-- View stats for a column:
SELECT attname, n_distinct, most_common_vals, histogram_bounds
FROM pg_stats
WHERE tablename = 'orders' AND attname = 'status';
```

---

## 4. Design Trade-Offs

### Buffer Manager

| Decision | Trade-off |
|---|---|
| Clock-sweep over LRU | Lower overhead but less optimal for mixed workloads |
| Ring buffer for seq scans | Protects cache but seq scans can't benefit from caching |
| Shared buffer size fixed at startup | Can't dynamically grow; requires restart to tune |

### MVCC

| Decision | Trade-off |
|---|---|
| Heap-based tuple versions (no undo log) | Simple read path, but dead tuples bloat tables |
| VACUUM for cleanup | Works asynchronously, but can't keep up under write-heavy load |
| Append-only updates | No in-place modification = write amplification for UPDATE-heavy workloads |
| XID as 32-bit integer | Eventually wraps around — requires periodic freezing (autovacuum critical path) |

### WAL

| Decision | Trade-off |
|---|---|
| Full page images after checkpoint | Protects against torn writes but increases WAL size by 2-3x |
| Sequential WAL writes | Enables fast writes but requires a dedicated WAL volume for best perf |
| WAL as replication stream | Zero extra infrastructure for replication, but standby is always behind by WAL lag |

### B-Tree

| Decision | Trade-off |
|---|---|
| 50/50 page splits | Simple but can cause poor fill factor; `fillfactor` setting helps |
| All data in leaf pages (B+tree) | Good range scans, but each index is a full copy of the indexed data |
| No per-index MVCC | Index entries remain after tuple deletion; HOT optimization helps for same-page updates |

---

## 5. Experiments / Observations

> **Environment:** PostgreSQL 17 | Database: `advdbms` | Schema: `users` (50K rows), `products` (10K), `orders` (500K+)

### Experiment 1: Buffer Manager — Cache Hit Ratio

**Query:**
```sql
SELECT 
  schemaname, relname, heap_blks_read, heap_blks_hit,
  ROUND(heap_blks_hit::numeric / NULLIF(heap_blks_read + heap_blks_hit, 0) * 100, 2) AS hit_ratio_pct
FROM pg_statio_user_tables
ORDER BY heap_blks_hit + heap_blks_read DESC LIMIT 5;
```

**Actual output (executed on advdbms):**
```
 schemaname |  relname   | heap_blks_read | heap_blks_hit | hit_ratio_pct
------------+------------+----------------+---------------+---------------
 public     | orders     |              0 |       2200635 |        100.00
 public     | stale_test |              0 |        103690 |        100.00
 public     | users      |              0 |         60000 |        100.00
 public     | products   |              0 |         60000 |        100.00
 public     | iso_test   |              0 |             8 |        100.00
(5 rows)
```

**Interpretation:** All tables show 100% buffer cache hit ratio — every page read was served from `shared_buffers`, zero disk reads. This is expected for a warm, in-memory dataset. In production with large tables exceeding `shared_buffers`, hit ratios typically range 95–99%. A ratio below 95% is the primary signal to increase `shared_buffers`.

---

### Experiment 2: EXPLAIN ANALYZE — Multi-Join Query

**Query:**
```sql
EXPLAIN (ANALYZE, BUFFERS)
SELECT u.name, COUNT(o.id) AS order_count, SUM(o.total_amount) AS revenue
FROM users u
JOIN orders o ON u.id = o.user_id
JOIN products p ON o.product_id = p.id
WHERE o.created_at >= NOW() - INTERVAL '30 days'
GROUP BY u.id, u.name
ORDER BY revenue DESC LIMIT 10;
```

**Actual output:**
```
 Limit  (cost=19017.82..19017.84 rows=10 width=54) (actual time=114.570..114.577 rows=10 loops=1)
   Buffers: shared hit=5174, temp read=134 written=297
   ->  Sort  (cost=19017.82..19142.82 rows=50000 width=54) (actual time=114.568..114.573 rows=10 loops=1)
         Sort Key: (sum(o.total_amount)) DESC
         Sort Method: top-N heapsort  Memory: 26kB
         Buffers: shared hit=5174, temp read=134 written=297
         ->  HashAggregate  (cost=16572.20..17937.33 rows=50000 width=54) (actual time=87.285..107.960 rows=35040 loops=1)
               Group Key: u.id
               Planned Partitions: 4  Batches: 5  Memory Usage: 8241kB  Disk Usage: 1616kB
               ->  Hash Join  (cost=3771.90..11874.82 rows=63158 width=24) (actual time=19.173..62.099 rows=60671 loops=1)
                     Hash Cond: (o.product_id = p.id)
                     ->  Hash Join  (cost=3468.90..11405.97 rows=63158 width=28) (actual time=17.360..49.113 rows=60671 loops=1)
                           Hash Cond: (o.user_id = u.id)
                           ->  Bitmap Heap Scan on orders o  (actual time=4.363..19.315 rows=60671 loops=1)
                                 Recheck Cond: (created_at >= (now() - '30 days'::interval))
                                 Heap Blocks: exact=4338
                                 ->  Bitmap Index Scan on idx_orders_created_at
                                       Index Cond: (created_at >= ...)
                                       Buffers: shared hit=245
                           ->  Hash  (actual time=12.794..12.795 rows=50000 loops=1)
                                 Buckets: 65536  Memory Usage: 2856kB
                                 ->  Seq Scan on users u  (actual time=0.005..4.299 rows=50000 loops=1)
                     ->  Hash  (actual time=1.758..1.759 rows=10000 loops=1)
                           ->  Seq Scan on products p  (actual time=0.006..0.747 rows=10000 loops=1)
 Planning Time: 1.104 ms
 Execution Time: 115.339 ms
```

**Key observations:**
- Planner chose **Hash Join** (not Nested Loop) for both joins — correct choice given large table sizes
- `Bitmap Index Scan on idx_orders_created_at` used to filter 30-day window: 60,671 rows from 510K+
- HashAggregate spilled to disk (Disk Usage: 1616kB) when grouping 50K users — work_mem tuning opportunity
- Total: **5,174 shared buffer hits, 0 disk reads** — fully in-memory execution in 115ms

---

### Experiment 3: MVCC Dead Tuples — Before/After VACUUM

**Setup:** Used `pgstattuple` extension to measure dead tuples precisely.

```sql
CREATE EXTENSION IF NOT EXISTS pgstattuple;

-- Initial state after 50K rows already updated
SELECT table_len, tuple_count, dead_tuple_count, dead_tuple_percent, free_space
FROM pgstattuple('orders');
```

**Before additional updates:**
```
 table_len | tuple_count | dead_tuple_count | dead_tuple_percent | free_space
-----------+-------------+------------------+--------------------+------------
  37552128 |      500000 |            50000 |               8.52 |      23776
```

```sql
-- Three rounds of updates on 100K rows
UPDATE orders SET status = 'shipped'   WHERE id BETWEEN 1 AND 100000;
UPDATE orders SET status = 'delivered' WHERE id BETWEEN 1 AND 100000;
UPDATE orders SET status = 'returned'  WHERE id BETWEEN 1 AND 50000;

SELECT table_len, tuple_count, dead_tuple_count, dead_tuple_percent, free_space
FROM pgstattuple('orders');
```

**After updates (dead tuples accumulated):**
```
 table_len | tuple_count | dead_tuple_count | dead_tuple_percent | free_space
-----------+-------------+------------------+--------------------+------------
  54607872 |      500000 |           100000 |              11.72 |   12821224
```

```sql
VACUUM orders;

SELECT table_len, tuple_count, dead_tuple_count, dead_tuple_percent, free_space
FROM pgstattuple('orders');
```

**After VACUUM:**
```
 table_len | tuple_count | dead_tuple_count | dead_tuple_percent | free_space
-----------+-------------+------------------+--------------------+------------
  54607872 |      500000 |                0 |                  0 |   20409004
```

**Interpretation:** 100K dead tuples (11.72% bloat) accumulated from repeated UPDATEs. VACUUM zeroed the dead tuple count and reclaimed ~20MB as free space. Table file size stays the same (VACUUM FULL needed to shrink the file) but future inserts can reuse the freed slots without extending the file.

---

### Experiment 4: WAL Size Measurement

```sql
-- Capture LSN before 10K insert
SELECT pg_current_wal_lsn() AS lsn_start;
-- Result: 0/104F9140

INSERT INTO orders (user_id, product_id, total_amount, status)
SELECT (random()*49999+1)::int, (random()*9999+1)::int,
       (random()*999+1)::numeric(10,2), 'PENDING'
FROM generate_series(1, 10000);

SELECT pg_size_pretty(
  pg_wal_lsn_diff(pg_current_wal_lsn(), '0/104F9140'::pg_lsn)
) AS wal_for_10k_insert;
```

**Actual output:**
```
 wal_for_10k_insert
--------------------
 3723 kB
```

```sql
-- Total WAL files on disk
SELECT count(*) AS wal_file_count, pg_size_pretty(sum(size)) AS total_wal_size
FROM pg_ls_waldir();
```

```
 wal_file_count | total_wal_size
----------------+----------------
             16 | 256 MB
```

**Interpretation:** A single bulk insert of 10K rows generated **3.7 MB of WAL**. The full WAL directory holds 16 segments × 16MB = 256MB. Each segment is recycled after the next checkpoint advances past it. WAL generation grows significantly if `full_page_writes=on` (default) is active — the first modification of each page after a checkpoint embeds the full 8KB page image in WAL, protecting against torn writes.

---

### Experiment 5: Stale Statistics — Planner Row Count Error

```sql
-- Create table, insert 1000 rows, analyze
DROP TABLE IF EXISTS stale_test;
CREATE TABLE stale_test (id SERIAL PRIMARY KEY, val INT);
INSERT INTO stale_test(val) SELECT generate_series(1,1000);
ANALYZE stale_test;

EXPLAIN SELECT * FROM stale_test WHERE val < 500;
```

**With fresh stats (1000 rows):**
```
                         QUERY PLAN
-------------------------------------------------------------
 Seq Scan on stale_test  (cost=0.00..17.50 rows=499 width=8)
   Filter: (val < 500)
```

```sql
-- Insert 100K more rows WITHOUT running ANALYZE
INSERT INTO stale_test(val) SELECT generate_series(1001,101000);

EXPLAIN SELECT * FROM stale_test WHERE val < 500;
```

**With stale stats (table has 101K rows, planner still thinks 1K):**
```
                           QUERY PLAN
-----------------------------------------------------------------
 Seq Scan on stale_test  (cost=0.00..1564.50 rows=44699 width=8)
   Filter: (val < 500)
```

```sql
-- Run ANALYZE, then check again
ANALYZE stale_test;
EXPLAIN SELECT * FROM stale_test WHERE val < 500;
```

**After ANALYZE (corrected):**
```
                          QUERY PLAN
---------------------------------------------------------------
 Seq Scan on stale_test  (cost=0.00..1709.50 rows=484 width=8)
   Filter: (val < 500)
```

**Interpretation:** Without ANALYZE, the planner wildly overestimated rows (`rows=44699` vs actual ~484). This matters critically for joins — a planner that thinks a table has 45K matching rows instead of 484 may choose Nested Loop over Hash Join, causing 100x slower queries. `autovacuum` runs ANALYZE automatically when ≥20% of a table changes, but it can lag behind bulk loads. Manual `ANALYZE` after large imports is always good practice.

---

### Experiment 6: MVCC Snapshot Isolation

```sql
BEGIN ISOLATION LEVEL REPEATABLE READ;

-- First read: balance = 1000
SELECT id, balance FROM iso_test WHERE id = 1;
-- id | balance → 1 | 1000

SAVEPOINT before_update;
UPDATE iso_test SET balance = balance - 200 WHERE id = 1;

-- Sees own change: balance = 800
SELECT id, balance FROM iso_test WHERE id = 1;
-- id | balance → 1 | 800

-- Roll back to savepoint — snapshot reverts to pre-update view
ROLLBACK TO SAVEPOINT before_update;
SELECT id, balance FROM iso_test WHERE id = 1;
-- id | balance → 1 | 1000  (snapshot consistent despite update attempt)

COMMIT;
```

**Output:**
```
 id | balance      id | balance      id | balance
----+---------  → ----+---------  → ----+---------
  1 |    1000       1 |     800       1 |    1000
```

**Interpretation:** REPEATABLE READ isolation takes a snapshot at `BEGIN`. Within the transaction, the `ROLLBACK TO SAVEPOINT` reverts changes but the consistent read view is preserved. Concurrent transactions updating the same row do not block this read — MVCC provides non-blocking reads by serving each transaction its own version of the data.

---

## 6. Key Learnings

1. **The buffer manager is the performance bottleneck** — almost every optimization in PostgreSQL (indexes, clustering, partitioning) ultimately aims to reduce the number of 8KB pages the buffer manager has to read from disk. `shared_buffers` tuning is the highest-leverage configuration change.

2. **MVCC's hidden cost is VACUUM** — the elegance of non-blocking reads comes with the obligation of periodic dead tuple cleanup. In systems with heavy UPDATE/DELETE workloads and poor autovacuum tuning, tables silently balloon in size, slowing every query.

3. **WAL is write-ahead, not write-behind** — the constraint is that WAL must reach disk *before* a commit is acknowledged. This is why `synchronous_commit=off` is a real performance optimization (but risks losing the last few transactions on crash — acceptable for some workloads).

4. **The query planner is a probabilistic engine** — it makes cost estimates based on statistical approximations. The planner can only be as good as the statistics you give it. Understanding `pg_statistic` and knowing when to manually `ANALYZE` is essential for production PostgreSQL.

5. **B-tree page splits are expensive but rare** — the 50/50 split strategy means pages are always ~50% full after a split. Setting a lower `fillfactor` (e.g., 70) leaves room for in-place updates, reducing future splits at the cost of larger index size.

6. **Everything in PostgreSQL is WAL** — heap changes, index changes, VACUUM operations, sequence increments. This unified WAL stream is why streaming replication works so cleanly: the standby literally re-executes every operation the primary did.

---

*References: PostgreSQL 16 source code (src/backend/storage/buffer/, src/backend/access/nbtree/, src/backend/access/transam/); PostgreSQL Documentation Chapters 14, 28, 30, 73; "The Internals of PostgreSQL" by Hironobu Suzuki (interdb.jp); EXPLAIN ANALYZE guide (thoughtbot.com); "Inside PostgreSQL: MVCC Internals" (Medium)*
