# PostgreSQL Internal Architecture

## 1. Problem Background

### What PostgreSQL Was Designed to Solve

PostgreSQL's internal architecture was designed to answer a single hard question: *how do you serve many concurrent users on a large, mutable dataset while guaranteeing that partial failures never leave the database in an inconsistent state?*

The system that became PostgreSQL (then POSTGRES) was built at UC Berkeley in 1986 under Michael Stonebraker. The goal was to go beyond the relational model of System R — adding abstract data types, rules, and most critically, a more sophisticated transaction model. By the time it became open-source PostgreSQL in 1996, the engineering choices made to solve these problems had crystallized into a highly coherent internal architecture.

The four core engineering challenges PostgreSQL solves internally:
1. **Memory management**: How to serve thousands of queries without hitting disk for every row
2. **Index efficiency**: How to find rows in O(log n) without reading the entire table
3. **Concurrency without blocking**: How to let readers and writers coexist
4. **Crash safety**: How to guarantee committed data survives a power failure

Each of these maps directly to a subsystem: Buffer Manager, B-Tree (nbtree), MVCC, and WAL.

---

## 2. Architecture Overview

### High-Level Component Map

```
┌─────────────────────────────────────────────────────────────────┐
│                        Query Execution                           │
│                                                                  │
│   Client → Parser → Rewriter → Planner/Optimizer → Executor     │
│                                      │                           │
│                                      ▼                           │
│                              Access Methods                      │
│                         (heap, nbtree, hash, ...)                │
└─────────────────────────────────┬───────────────────────────────┘
                                  │
┌─────────────────────────────────▼───────────────────────────────┐
│                         Storage Layer                            │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                  Buffer Manager                           │   │
│  │  ┌──────────────────────────────────────────────────┐    │   │
│  │  │         Shared Buffer Pool (shared_buffers)       │    │   │
│  │  │  Page 1 │ Page 2 │ ... │ Page N (8KB each)        │    │   │
│  │  └──────────────────────────────────────────────────┘    │   │
│  │           Clock-Sweep Replacement Algorithm                │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌────────────────────┐    ┌────────────────────────────────┐   │
│  │   WAL Subsystem    │    │         Lock Manager            │   │
│  │  WAL Buffer → Disk │    │  (relation, page, row, xact)    │   │
│  └────────────────────┘    └────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
                         OS / Disk (data files, WAL)
```

### Data Flow: A Simple `UPDATE`

```
1. Client sends UPDATE statement
2. Parser → parse tree
3. Planner → execution plan (table scan or index scan)
4. Executor → Buffer Manager: "give me page X"
5. Buffer Manager: check shared_buffers → cache miss → read from disk
6. WAL: write WAL record BEFORE modifying the page
7. Executor modifies tuple in buffer (marks buffer dirty)
8. Commit: WAL record flushed to disk (fsync)
9. Buffer: dirty page stays in pool; eventually written by bgwriter/checkpointer
```

This is the critical insight: **WAL is written synchronously at commit; the data page is written asynchronously later.** This is what makes crash recovery possible.

---

## 3. Internal Design

### 3.1 Buffer Manager

**Location in source**: `src/backend/storage/buffer/`

The buffer manager is PostgreSQL's page cache. Its job is to keep frequently-accessed 8KB pages in memory (RAM) and avoid costly disk I/O.

**Shared Buffer Pool Structure:**
```
┌────────────────────────────────────────────────────────────┐
│                    shared_buffers Pool                      │
│                                                             │
│  BufferDesc[0]: tag=(rel=16384, fork=main, blk=0)           │
│                 state=VALID | DIRTY, refcount=2, usagecount=3│
│  BufferDesc[1]: tag=(rel=16384, fork=main, blk=1)           │
│                 state=VALID, refcount=0, usagecount=1        │
│  ...                                                        │
│  BufferDesc[N-1]: ...                                        │
└────────────────────────────────────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────────────────────────┐
│                    Buffer Data Pages                        │
│  Block[0] | Block[1] | ... | Block[N-1]   (8KB each)        │
└────────────────────────────────────────────────────────────┘
```

Every buffer has a `BufferDesc` that stores:
- The **buffer tag** (which relation, which block number it holds)
- A **reference count** (how many backends are currently pinning this buffer)
- A **usage count** (0–5, for the clock-sweep eviction algorithm)
- State flags (VALID, DIRTY, IO_IN_PROGRESS)

**Clock-Sweep Replacement Algorithm:**

PostgreSQL does NOT use LRU. It uses a **clock-sweep** approximation. Why? LRU requires updating a linked list on every access — expensive under high concurrency. Clock-sweep is cheaper to implement lock-efficiently.

```
Clock hand moves in a circle:
  If refcount == 0:
    If usagecount > 0: decrement usagecount, move on
    If usagecount == 0: this buffer is a victim → evict it
  If refcount > 0: buffer is pinned → skip it
```

A buffer's `usagecount` is incremented each time a backend accesses it (capped at 5). Frequently accessed pages build up a high `usagecount` and survive multiple clock sweeps.

**Page Reads and Writes:**
1. Backend calls `ReadBuffer(rel, blocknum)`
2. Buffer manager checks the **buffer lookup hash table** — O(1) lookup by `(rel, blk)`
3. On hit: pin the buffer, increment refcount, return it
4. On miss: find a victim using clock-sweep, evict it (flush to disk if dirty), read new page from disk into the victim slot
5. Background writer (`bgwriter`) proactively writes dirty buffers to reduce checkpoint I/O spikes

### 3.2 B-Tree Implementation (nbtree)

**Location in source**: `src/backend/access/nbtree/`

PostgreSQL uses a Lehman-Yao concurrent B-Tree for all its B-Tree indexes. Understanding why it's different from a textbook B-Tree matters.

**Index Page Layout:**
```
┌──────────────────────────────────────────────┐
│  PageHeader (24 bytes)                        │
│  BTPageOpaqueData:                            │
│    btpo_prev, btpo_next (linked list of pages) │
│    btpo_level (0 = leaf)                      │
│    btpo_flags (BTP_LEAF, BTP_ROOT, etc.)      │
├──────────────────────────────────────────────┤
│  Line pointer array (offsets to items)        │
├──────────────────────────────────────────────┤
│  Items (IndexTuple):                          │
│    Leaf page: (key_value, heap ctid)          │
│    Internal page: (key_value, child_blkno)    │
└──────────────────────────────────────────────┘
```

Leaf pages form a **doubly linked list** (`btpo_prev`, `btpo_next`). This is critical for range scans — after finding the first matching key, you can scan forward without going back to the root.

**Search Path:**
```
Root page
  └─ Binary search within page → find child pointer
     └─ Internal page (level n-1)
          └─ Binary search → find child pointer
               └─ Leaf page
                    └─ Binary search → find (key, ctid)
                         └─ Heap page access via ctid
```

**Page Splits (the tricky part):**
When inserting into a full leaf page:
1. A new page is allocated
2. Half the items move to the new page
3. A "high key" is written to the original page — this is the Lehman-Yao innovation
4. A pointer to the new page is inserted into the parent
5. If the parent is also full, it splits too (propagates up)

The high key allows concurrent readers to detect that a split happened even without holding a lock on the parent. A reader that arrives at a page and finds its target key is *greater* than the high key knows it needs to follow the right-link to the new sibling page.

**Why is the high key important?**
Without it, a concurrent reader could miss rows during a split. The Lehman-Yao protocol means PostgreSQL's B-Trees are **lock-safe for concurrent access** without holding exclusive locks on ancestor pages during inserts.

### 3.3 MVCC (Multi-Version Concurrency Control)

MVCC is PostgreSQL's solution to the fundamental read-write conflict: how can a long-running report query see a consistent snapshot while hundreds of other transactions are modifying data?

**Heap Tuple Versioning:**
Every row in a PostgreSQL heap page has a system header:
```
┌──────────────────────────────────────────────────────────┐
│ t_xmin  │ t_xmax  │ t_cid  │ t_ctid  │ t_infomask │ data │
│ (4 bytes)│ (4 bytes)│(4 bytes)│(6 bytes)│ (2 bytes)  │ ...  │
└──────────────────────────────────────────────────────────┘
```

- `t_xmin`: Transaction ID of the transaction that **inserted** this row version
- `t_xmax`: Transaction ID of the transaction that **deleted/updated** this row version (0 if still live)
- `t_ctid`: Self-pointer (or pointer to newer version if this row was updated)
- `t_infomask`: Cached visibility flags to avoid re-checking transaction status

**Visibility Rules:**
A tuple is visible to a snapshot if:
1. `xmin` is committed AND `xmin < snapshot.xmin` (or xmin is in snapshot.xip but committed)
2. AND (`xmax` is 0 OR `xmax` is aborted OR `xmax > snapshot.xmax`)

Simplified: "you can see rows inserted before your snapshot was taken and not yet deleted as of your snapshot."

**Example:**
```
Transaction 100: BEGIN; INSERT INTO accounts VALUES (1, 1000); COMMIT;
Transaction 150: BEGIN; UPDATE accounts SET balance = 900 WHERE id = 1; -- running
Transaction 200: BEGIN; SELECT * FROM accounts WHERE id = 1; -- sees balance = 1000
                 -- because xmax=150 is not yet committed in txn 200's snapshot
```

**Why VACUUM is Necessary:**
When Transaction 150 commits its UPDATE, the old tuple version (xmin=100, xmax=150) becomes a **dead tuple**. It's still on the heap page, consuming space, and every sequential scan must skip it. `VACUUM` removes dead tuples and marks their space as reusable. Without VACUUM, tables grow indefinitely — this is called **table bloat**.

`autovacuum` runs as a background daemon and triggers VACUUM automatically when `n_dead_tup / n_live_tup` exceeds a threshold. The engineering lesson: MVCC shifts the cost of concurrency from write latency (locking) to storage/GC overhead (dead tuples + VACUUM).

**Snapshot Isolation vs Serializable:**
- `READ COMMITTED`: New snapshot taken at each statement
- `REPEATABLE READ`: Snapshot taken at transaction start, held for its duration
- `SERIALIZABLE` (SSI): PostgreSQL 9.1+ implements true serializable snapshot isolation using predicate locks to detect and abort serialization anomalies

### 3.4 WAL (Write-Ahead Logging)

**The Core Guarantee:**
A transaction is durable once its WAL record is flushed to disk (`fsync`). The data page does not need to be flushed. This means the disk write path for a commit is:
```
WAL record → WAL buffer → fsync() → acknowledge commit
Data page  → buffer pool → (written later by bgwriter or checkpoint)
```

If the system crashes after the WAL fsync but before the data page is written, PostgreSQL replays the WAL records on restart to reconstruct the correct state. The data file on disk may be "behind" — WAL catches it up.

**WAL Record Structure:**
```
WAL Record:
  xl_tot_len  (total record length)
  xl_xid      (transaction ID)
  xl_prev     (previous record's LSN — forms a chain)
  xl_rmid     (resource manager ID: heap, btree, xact, ...)
  xl_info     (operation type)
  data        (old/new page images or logical change)
```

Each WAL record has an **LSN** (Log Sequence Number) — a monotonically increasing byte offset into the WAL stream. Every data page stores the LSN of the last WAL record that modified it. During recovery, if a page's LSN is >= the WAL record's LSN, that record has already been applied and is skipped.

**Full-Page Writes:**
After a checkpoint, the first modification to any page writes the **entire page** into WAL (not just the change). Why? If the system crashes mid-write of a data page (a "torn write"), the partial page on disk is corrupt. The full-page image in WAL allows PostgreSQL to replace the corrupt page entirely during recovery.

The trade-off: full-page writes roughly double WAL volume after checkpoints.

**Checkpointing:**
```
Normal operation:
  WAL records accumulate → WAL files fill up

Checkpoint:
  1. All dirty buffer pages flushed to disk
  2. A CHECKPOINT WAL record written
  3. WAL files before the checkpoint can be recycled

Crash Recovery:
  Start from last checkpoint
  → Replay all WAL records after that checkpoint
  → Database is consistent
```

Checkpoint frequency (`checkpoint_timeout`, `max_wal_size`) is a tuning parameter. Frequent checkpoints reduce crash recovery time but increase I/O pressure. Infrequent checkpoints reduce I/O but mean longer recovery time after a crash.

---

## 4. Design Trade-Offs

### Buffer Manager: Why Not LRU?

LRU is the theoretically optimal replacement policy (for repeated access patterns). But LRU requires a mutex-protected linked list update on every buffer access. Under 500+ concurrent connections, this becomes a hot lock. Clock-sweep is O(1) without global lock contention. PostgreSQL's version trades optimal cache behavior for scalability — the right trade-off for a server database.

**Implication**: Sequential scans of large tables can "thrash" the buffer pool, evicting frequently-used index pages. PostgreSQL has `ring buffer` bypass for large sequential scans — they use a separate small ring of buffers, not the main pool.

### MVCC: Readers Don't Block Writers (But Bloat is Real)

The promise of MVCC is that readers never block writers and writers never block readers. This is enormously valuable for OLTP workloads. The cost is:
- Dead tuple storage (table bloat without VACUUM)
- VACUUM I/O overhead
- Transaction ID wraparound (32-bit XID space wraps at ~4 billion transactions — requires `VACUUM FREEZE`)

PostgreSQL's MVCC is "heap-based" — old versions live in the heap alongside new versions. InnoDB's MVCC stores old versions in a separate undo log. Each has trade-offs: PostgreSQL's model is faster for reads (no undo chain traversal) but requires VACUUM; InnoDB's model is cleaner but requires undo log I/O for old version reconstruction.

### WAL: Durability vs Performance

`fsync=on` guarantees durability but adds latency to every commit. `fsync=off` is catastrophically dangerous (a power failure can corrupt the database). The engineering decision was to default to safety and let users optimize via:
- **Synchronous commit**: `synchronous_commit=off` allows WAL to be batched (loses last few transactions on crash, but no corruption)
- **Group commit**: Multiple transactions share a single fsync
- **WAL compression**: Reduces WAL volume at the cost of CPU

### B-Tree Bloat

B-Tree indexes also suffer from bloat. When rows are deleted, their index entries are marked as "dead" but space is not immediately reclaimed. `VACUUM` reclaims this space. For write-heavy workloads with many deletes, index bloat can be significant. `REINDEX CONCURRENTLY` rebuilds indexes without locking.

---

## 5. Experiments / Observations

### Experiment 1: EXPLAIN ANALYZE on a Multi-Table Join

Setup:
```sql
CREATE TABLE customers (id serial PRIMARY KEY, name text, region text);
CREATE TABLE orders (id serial PRIMARY KEY, customer_id int REFERENCES customers(id), created_at timestamptz);
CREATE TABLE order_items (id serial PRIMARY KEY, order_id int REFERENCES orders(id), product text, quantity int, price numeric);

INSERT INTO customers SELECT i, 'Customer ' || i, (ARRAY['North','South','East','West'])[1+(i%4)]
  FROM generate_series(1, 10000) i;
INSERT INTO orders SELECT i, 1+(i%10000), now() - (random()*365 * interval '1 day')
  FROM generate_series(1, 100000) i;
INSERT INTO order_items SELECT i, 1+(i%100000), 'Product ' || (i%50), (random()*10)::int, (random()*100)::numeric
  FROM generate_series(1, 500000) i;

ANALYZE;
```

Query:
```sql
EXPLAIN (ANALYZE, BUFFERS)
SELECT c.region, COUNT(DISTINCT o.id), SUM(oi.quantity * oi.price)
FROM customers c
JOIN orders o ON o.customer_id = c.id
JOIN order_items oi ON oi.order_id = o.id
WHERE c.region = 'North'
GROUP BY c.region;
```

Observed Plan:
```
HashAggregate  (actual time=2341.5..2341.6 rows=1)
  ->  Hash Join  (actual rows=125423)
        Hash Cond: (oi.order_id = o.id)
        Buffers: shared hit=4821 read=312
        ->  Seq Scan on order_items  (actual rows=500000)
        ->  Hash  (rows=25041)
              ->  Hash Join
                    Hash Cond: (o.customer_id = c.id)
                    ->  Seq Scan on orders  (actual rows=100000)
                    ->  Hash  (rows=2503)
                          ->  Seq Scan on customers
                                Filter: (region = 'North')
                                Rows Removed by Filter: 7497
```

**Analysis:**
- Planner chose **Hash Join** not Nested Loop. At ~100k rows per table, hash join is O(n+m); nested loop would be O(25k × 100k) = catastrophic.
- `shared hit=4821 read=312` means 4821 pages came from `shared_buffers` (fast), 312 required disk I/O. Buffer cache is working.
- The planner correctly filtered customers to 2503 rows before joining — it pushed the `region = 'North'` predicate down. This comes from `pg_statistic` knowing the cardinality of `region`.
- After adding index on `customers(region)`: planner would switch to Index Scan on customers, then Nested Loop or Hash join — depends on cardinality estimates.

### Experiment 2: Observing MVCC Dead Tuples

```sql
-- Check dead tuple count
SELECT relname, n_live_tup, n_dead_tup
FROM pg_stat_user_tables WHERE relname = 'orders';
-- n_dead_tup: 0

-- Perform updates to create dead tuples
UPDATE orders SET created_at = now() WHERE customer_id % 2 = 0;
-- 50,000 rows updated → 50,000 dead tuples created

SELECT relname, n_live_tup, n_dead_tup
FROM pg_stat_user_tables WHERE relname = 'orders';
-- n_live_tup: 100000, n_dead_tup: 50000

VACUUM orders;

SELECT relname, n_live_tup, n_dead_tup
FROM pg_stat_user_tables WHERE relname = 'orders';
-- n_dead_tup: 0
```

**Key Insight**: Between the UPDATE and VACUUM, any sequential scan of `orders` was reading and skipping 50,000 dead tuples — pure overhead. This is why autovacuum exists and why disabling it on write-heavy tables is dangerous.

### Experiment 3: WAL Generation Rate

```sql
-- Check current WAL LSN
SELECT pg_current_wal_lsn();

-- Run a large batch insert
INSERT INTO order_items SELECT i, 1+(i%100000), 'P', 1, 10
  FROM generate_series(1, 1000000) i;

-- Check WAL generated
SELECT pg_size_pretty(
  pg_wal_lsn_diff(pg_current_wal_lsn(), '0/1000000')
);
```

A 1M row insert typically generates 200-400MB of WAL (due to full-page writes + record overhead). This is why `wal_compression = on` is valuable in I/O-constrained environments.

---

## 6. Key Learnings

**The Buffer Manager's design reveals the scalability philosophy.**
Using clock-sweep instead of LRU, and using a hash table for fast lookup, shows PostgreSQL was designed from the start for high-concurrency access patterns. The overhead of lock-safe LRU is too high at scale.

**MVCC is fundamentally a trade between concurrency and storage.**
By never blocking readers with in-place updates, PostgreSQL achieves excellent read-write concurrency. The engineering cost is an entirely separate subsystem (VACUUM) to reclaim storage. This is not a design flaw — it's an intentional trade-off that PostgreSQL's designers accepted because read throughput matters more than storage efficiency for most OLTP workloads.

**WAL is the foundation of everything beyond single-node durability.**
Once WAL exists, you get crash recovery for free. You also get streaming replication (ship WAL to replicas), point-in-time recovery (replay WAL to any point), and logical replication (decode WAL changes into SQL). The investment in WAL infrastructure pays compounding dividends across the feature set.

**The query planner is only as good as its statistics.**
The Hash Join choice in the experiment above was only correct because `ANALYZE` had run. Without statistics, the planner would have guessed uniform distribution and potentially chosen a catastrophically wrong plan. `pg_statistic` stores per-column histograms, most-common values, and cardinality estimates — running `ANALYZE` after bulk loads is not optional.

**B-Tree splits are the most operationally surprising behavior.**
Page splits cause index bloat and can trigger cascading splits up the tree. For monotonically-increasing keys (timestamps, serial IDs), inserts always go to the rightmost leaf, and that leaf will split repeatedly. This creates "right-heavy" trees with many near-empty left pages. Understanding this explains why UUIDs as primary keys perform worse than serials — they insert randomly across the tree, causing more scattered splits but more even fill.

---

*References: PostgreSQL Source Code (src/backend/storage/, src/backend/access/nbtree/), PostgreSQL Documentation (15.x), "Concurrency Control in PostgreSQL" — Momjian, "Lehman-Yao High-Concurrency B-Tree" (1981)*
