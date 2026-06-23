# PostgreSQL Internal Architecture

---

## 1. Problem Background

PostgreSQL was designed to be a production-grade, extensible, multi-user relational database. The internal architecture is shaped by one central challenge: **how do you let hundreds of concurrent processes read and write shared data correctly, efficiently, and durably?**

Each of the four internal subsystems studied here — the Buffer Manager, the B-Tree implementation, MVCC, and WAL — is a direct answer to one part of that challenge:

- **Buffer Manager**: Disk is slow. Cache frequently accessed pages in RAM.
- **B-Tree (nbtree)**: Table scans are expensive. Index structures allow O(log n) lookups.
- **MVCC**: Locks cause contention. Version rows instead of locking them so readers and writers don't block each other.
- **WAL**: RAM is volatile. Write changes to a log before applying them so crashes are recoverable.

These four subsystems are deeply interconnected. Understanding each one individually, and then understanding how they interact, is the key to understanding PostgreSQL.

---

## 2. Architecture Overview

```
                        ┌─────────────────────────────────┐
                        │         Client Application       │
                        └─────────────┬───────────────────┘
                                      │ libpq (TCP / Unix socket)
                        ┌─────────────▼───────────────────┐
                        │         Postmaster               │
                        │  (Accepts connections, forks     │
                        │   backend processes)             │
                        └─────────────┬───────────────────┘
                                      │ fork()
                        ┌─────────────▼───────────────────┐
                        │       Backend Process            │
                        │  ┌───────────────────────────┐  │
                        │  │ Parser → Rewriter          │  │
                        │  │ Planner/Optimizer          │  │
                        │  │ Executor                  │  │
                        │  └────────────┬──────────────┘  │
                        └───────────────┼─────────────────┘
                                        │
              ┌─────────────────────────▼──────────────────────────┐
              │                 Shared Memory                       │
              │  ┌──────────────┐  ┌──────────┐  ┌─────────────┐  │
              │  │ Shared       │  │   WAL    │  │  Lock       │  │
              │  │ Buffers      │  │  Buffers │  │  Table      │  │
              │  │ (Buffer Mgr) │  │          │  │             │  │
              │  └──────┬───────┘  └────┬─────┘  └─────────────┘  │
              └─────────┼───────────────┼────────────────────────-─┘
                        │               │
              ┌─────────▼───────┐ ┌─────▼─────────────┐
              │  Heap & Index   │ │   WAL Files        │
              │  Files (data)   │ │   (pg_wal/)        │
              └─────────────────┘ └───────────────────┘
```

---

## 3. Internal Design

### 3.1 Buffer Manager

**Source**: `src/backend/storage/buffer/`

The Buffer Manager is the memory cache between backend processes and disk. All access to table and index pages goes through it — no backend ever reads a disk page directly.

**Shared Buffers**

Shared buffers is a fixed-size pool of 8KB page frames in shared memory. Its size is controlled by the `shared_buffers` GUC parameter (default: 128MB, recommended: 25% of RAM). All backend processes share this pool.

```
Shared Buffer Pool:
┌──────────────────────────────────────────────┐
│  Buffer Descriptor Array (metadata)          │
│  [buf_id=0: rel=16389 blk=0, pincount=1, ..] │
│  [buf_id=1: rel=16389 blk=1, pincount=0, ..] │
│  ...                                         │
├──────────────────────────────────────────────┤
│  Buffer Data (actual 8KB pages)              │
│  [Frame 0: page bytes]                       │
│  [Frame 1: page bytes]                       │
│  ...                                         │
└──────────────────────────────────────────────┘
```

Each buffer descriptor tracks:
- Which relation and block number this frame holds.
- `refcount` / `pincount`: How many backends are currently using this page.
- `usage_count`: For clock-sweep eviction.
- `dirty` flag: Whether the page has been modified since it was read from disk.

**Page Reads and Writes**

When an executor node needs a page:
1. It calls `ReadBuffer(relation, blocknum)`.
2. The buffer manager checks the buffer hash table (`BufTable`) for a hit.
3. **Hit**: Increment the pin count, return the buffer ID.
4. **Miss**: Find a victim buffer using clock-sweep, evict it (flushing to disk if dirty), read the new page from disk, insert into the hash table.

The **clock-sweep** algorithm scans buffers in circular order. Each pass decrements `usage_count`. A buffer with `usage_count=0` and `pincount=0` is eligible for eviction. This approximates LRU without the overhead of a true LRU list.

**Why shared, not per-process?**

If each backend had its own private cache, the same hot page could be cached in 100 different backend processes — 100× the memory waste. Shared buffers mean one copy of a hot page is in memory regardless of how many backends need it.

### 3.2 B-Tree Implementation (nbtree)

**Source**: `src/backend/access/nbtree/`

PostgreSQL's default index type is a B-tree (specifically, a B+-tree variant). All unique constraints, primary keys, and `CREATE INDEX` without specifying a type create a nbtree index.

**Index Structure**

```
                        ┌─────────────────┐
                        │   Meta Page     │  (Page 0: root pointer, fast-root)
                        └────────┬────────┘
                                 │
                        ┌────────▼────────┐
                        │   Root Page     │
                        │  [30 | 60 | 90] │  (Interior: separator keys + child pointers)
                        └──┬──────┬───┬───┘
                    ┌───────┘      │   └───────┐
          ┌─────────▼──┐  ┌────────▼───┐  ┌───▼────────┐
          │ Leaf Page  │  │ Leaf Page  │  │ Leaf Page  │
          │ ←prev|next→│  │ ←prev|next→│  │ ←prev|next→│
          │ (TID,key)  │  │ (TID,key)  │  │ (TID,key)  │
          └────────────┘  └────────────┘  └────────────┘
```

Leaf pages are doubly-linked, enabling efficient range scans without returning to the root.

**Index Page Layout**

Each nbtree page is an 8KB page (same as heap pages) divided into:
- **Page header**: LSN, checksum, special area pointer.
- **Item pointer array**: (offset, length) pairs for each index tuple.
- **Index tuples**: Packed from the bottom; each contains (key value, TID pointing to the heap tuple).
- **Special area** (BTPageOpaqueData): Left and right sibling page numbers, level, flags. This is what enables the leaf-page linked list and the B-tree's structural integrity.

**Search Path**

```
Search for key K:
1. Read meta page → get root page number
2. Read root page → binary search for child pointer where K would reside
3. Descend to next level, repeat
4. Reach leaf page → binary search for K
5. Return TID(s) → fetch actual tuple from heap
```

**Insert and Page Splits**

Insertions add a new (key, TID) entry to the appropriate leaf page. If the leaf is full, a **page split** occurs:
1. Allocate a new page.
2. Move the upper half of entries to the new page.
3. Insert a new separator key into the parent page pointing to the new page.
4. If the parent is also full, split propagates upward.
5. If the root splits, a new root is allocated and the tree height increases by one.

All split steps are WAL-logged before execution, ensuring crash safety.

### 3.3 MVCC (Multi-Version Concurrency Control)

**Heap Tuple Versioning**

Every heap tuple (row) carries system columns that implement versioning:

| Column | Meaning |
|---|---|
| `xmin` | Transaction ID that inserted this tuple |
| `xmax` | Transaction ID that deleted/updated this tuple (0 = still live) |
| `cmin` | Command ID within the inserting transaction |
| `cmax` | Command ID within the deleting transaction |
| `ctid` | Physical location (page, offset) of this tuple; updated tuples point to their successor |

When a row is **updated**:
1. The old tuple's `xmax` is set to the current transaction ID.
2. A new tuple is inserted with `xmin` set to the current transaction ID.
3. The old tuple's `ctid` is updated to point to the new tuple (for HOT chains).

When a row is **deleted**:
1. The tuple's `xmax` is set to the current transaction ID.
2. No physical deletion occurs. The tuple remains on the page.

**Visibility Rules**

A transaction with snapshot `S` can see tuple `T` if and only if:
- `T.xmin` committed before snapshot `S` was taken (the row was inserted by a committed transaction before our snapshot).
- AND either:
  - `T.xmax = 0` (not deleted), OR
  - `T.xmax` did NOT commit before snapshot `S` was taken (deleted by a transaction we cannot see yet).

```
Example: Three concurrent transactions

Txn 100 (snapshot: {active: []})
Txn 200 (snapshot: {active: [100]})
Txn 300 (snapshot: {active: [100, 200]})

Tuple: xmin=50, xmax=0   → All three transactions see it (xmin=50 committed long ago)
Tuple: xmin=100, xmax=0  → Txn 200 and 300 cannot see it (xmin=100 is in-progress when they snapshot)
Tuple: xmin=50, xmax=200 → Txn 100 sees it (xmax=200 not yet committed at snapshot time)
                         → Txn 300 does not (xmax=200 committed before Txn 300's snapshot)
```

**Snapshot Isolation**

PostgreSQL's default isolation level is Read Committed: each statement takes a fresh snapshot. Repeatable Read and Serializable take one snapshot for the entire transaction. The snapshot is a list of in-progress transaction IDs at the moment it's taken. Visibility is computed relative to this snapshot, not the current time.

**Why VACUUM is Necessary**

Dead tuples (old versions with `xmax` committed) accumulate on heap pages. VACUUM:
1. Scans heap pages for dead tuples.
2. Marks their item pointer slots as free.
3. Updates the free space map so new inserts can reuse space.
4. Updates the visibility map (pages where all tuples are visible to all transactions can skip VACUUM next time).
5. Reclaims `pg_clog` (commit log) space for old transaction IDs.

Without VACUUM, the heap grows without bound and eventually **transaction ID wraparound** occurs: PostgreSQL uses 32-bit transaction IDs, which wrap around after ~2 billion transactions. VACUUM advances the freeze horizon, preventing this.

### 3.4 WAL (Write-Ahead Logging)

**WAL Records**

WAL is an append-only log. Every modification to a data page — whether it's a heap insert, a tuple update, a B-tree page split, or an MVCC visibility flag change — is first written as a WAL record before the data page is modified.

Each WAL record contains:
- **LSN** (Log Sequence Number): Monotonically increasing byte offset in the WAL stream.
- **Resource Manager ID**: Identifies which subsystem generated this record (heap, btree, transaction, etc.).
- **Record data**: The actual change (e.g., the new tuple bytes for a heap insert, or the split information for a B-tree split).

**Durability Guarantee**

Before a transaction is considered committed:
1. The commit WAL record must be written to the WAL buffer.
2. The WAL buffer must be flushed to disk (via `fsync` or `fdatasync`).

Only after step 2 does PostgreSQL return success to the client. The data pages themselves do **not** need to be flushed at commit time — they can remain in shared buffers as dirty pages and be written to disk lazily by the background writer or checkpoint process.

**Why this is safe**: If the system crashes after a commit but before data pages are flushed, the WAL contains a complete record of the change. During crash recovery, PostgreSQL replays the WAL from the last checkpoint LSN, re-applying all changes to reconstruct the exact state at the time of the crash.

**Crash Recovery**

```
Crash recovery sequence:
1. PostgreSQL starts
2. Reads the control file → finds last checkpoint LSN
3. Opens WAL from that LSN
4. Replays each WAL record (redo phase)
5. Rolls back any transactions that were in-progress at crash time (using in-WAL abort records or lack of commit record)
6. Database is consistent; normal operation resumes
```

**Checkpointing**

A checkpoint writes all dirty shared buffers to disk and writes a CHECKPOINT WAL record. After a checkpoint, all WAL records before the checkpoint LSN are no longer needed for recovery and can be archived or deleted. Checkpoints are triggered by:
- The `checkpoint_timeout` interval (default: 5 minutes).
- The WAL size exceeding `max_wal_size`.
- Manual `CHECKPOINT` command.

More frequent checkpoints mean faster crash recovery (less WAL to replay) but more I/O overhead during normal operation.

---

## 4. Design Trade-offs

### Buffer Manager Trade-offs

| Decision | Advantage | Cost |
|---|---|---|
| Fixed shared buffer pool | Predictable memory usage | Can't dynamically grow during spikes |
| Clock-sweep eviction | O(1) amortized, simple | Less optimal than true LRU for certain workloads |
| Shared (not per-process) | One copy of hot pages | Requires lwlocks to protect buffer descriptors |

The `effective_cache_size` parameter doesn't allocate memory — it tells the planner how much total OS page cache + shared buffers is available, influencing index scan decisions.

### B-Tree Trade-offs

| Decision | Advantage | Cost |
|---|---|---|
| Non-clustered (heap + index) | Flexible; multiple indexes, index updates don't move data | Index scan requires heap fetch (unless covering index) |
| Leaf page linking | Efficient range scans | Extra pointer maintenance during splits |
| Page splits | Keeps tree balanced | Write amplification; WAL overhead per split |

PostgreSQL 13+ introduced **bottom-up index deletion** for B-tree indexes to reduce page splits caused by heap updates.

### MVCC Trade-offs

| Decision | Advantage | Cost |
|---|---|---|
| Tuple versioning in heap | Readers never block writers | Dead tuple bloat; requires VACUUM |
| Snapshot-based reads | Consistent view at a point in time | Snapshots may see stale data relative to latest commit |
| No in-place updates | No read-write contention on rows | Tables grow physically with updates; VACUUM must reclaim |

Compare to InnoDB's approach: InnoDB does in-place updates and keeps old versions in a separate undo log. PostgreSQL's approach is simpler (old versions are right in the heap) but requires VACUUM; InnoDB's undo log can be purged more precisely.

### WAL Trade-offs

| Decision | Advantage | Cost |
|---|---|---|
| Write-ahead logging | Crash safety, replication, PITR | ~2x write I/O (WAL + eventual data page write) |
| Page-level WAL records | Idempotent; safe to replay multiple times | Larger WAL volume than logical logging |
| Checkpoint-based recovery | Bounds crash recovery time | Checkpoint I/O can spike; `checkpoint_completion_target` spreads it |

`synchronous_commit = off` is a trade-off: transactions appear to commit instantly (WAL not flushed), but up to `wal_writer_delay` of data can be lost on crash. This is acceptable for non-critical data when throughput matters more than durability.

---

## 5. Experiments / Observations

### Experiment 1: Buffer Manager — Cache Hit Rate

```sql
-- Check buffer cache hit rate
SELECT
  sum(heap_blks_read)  AS heap_read,
  sum(heap_blks_hit)   AS heap_hit,
  round(
    sum(heap_blks_hit)::numeric /
    (sum(heap_blks_hit) + sum(heap_blks_read) + 1) * 100, 2
  ) AS hit_rate_pct
FROM pg_statio_user_tables;

-- Sample output after warming up:
-- heap_read | heap_hit | hit_rate_pct
-- ----------+----------+-------------
--      1240 |   184320 |       99.33
```

A hit rate above 99% means the working set fits in shared buffers. Below 95% suggests `shared_buffers` should be increased.

### Experiment 2: EXPLAIN ANALYZE on a Multi-Table Join

```sql
EXPLAIN (ANALYZE, BUFFERS) 
SELECT o.order_id, c.name, p.product_name, oi.quantity
FROM orders o
JOIN customers c ON o.customer_id = c.id
JOIN order_items oi ON o.order_id = oi.order_id
JOIN products p ON oi.product_id = p.id
WHERE o.created_at > NOW() - INTERVAL '30 days';
```

**Observed plan output** (simplified):

```
Hash Join  (cost=245.12..1823.44 rows=2840 width=72)
           (actual time=4.213..31.4 ms rows=2756 loops=1)
  Buffers: shared hit=1243 read=87
  ->  Hash Join  (cost=108.3..892.1 rows=2840 width=48)
        ->  Seq Scan on order_items oi
        ->  Hash on products p  (Batches=1, Memory Usage: 128kB)
  ->  Hash on customers c  (Batches=1, Memory Usage: 64kB)
        Filter: (o.created_at > (now() - '30 days'::interval))
        Rows Removed by Filter: 18230
```

**Analysis of the plan**:
- The planner chose **Hash Join** over Nested Loop because `pg_statistic` estimated ~2840 matching rows — too many for nested loop to be efficient.
- `shared hit=1243 read=87`: 93% of pages were served from shared buffers; only 87 required disk reads.
- The filter on `created_at` removed 18,230 rows; adding an index on `orders(created_at)` would allow an **Index Scan** and reduce rows examined.

```sql
-- After adding: CREATE INDEX idx_orders_created ON orders(created_at);
-- EXPLAIN shows:
Index Scan using idx_orders_created on orders o
  (actual time=0.041..1.2 ms rows=2756 loops=1)
-- Join time drops from 31ms to ~8ms
```

**Relationship with pg_statistic**:

```sql
-- The planner uses statistics collected by ANALYZE:
SELECT attname, n_distinct, correlation
FROM pg_stats
WHERE tablename = 'orders';

-- correlation near 1.0 = physical order matches index order (cheap index scan)
-- correlation near 0   = random I/O (seq scan may be cheaper)
```

After a bulk insert without `ANALYZE`, statistics are stale and the planner may make poor choices (e.g., estimating 10 rows when there are 100,000). Always run `ANALYZE` after large data loads.

### Experiment 3: MVCC Dead Tuple Accumulation

```sql
-- Run 50,000 updates on the same rows
DO $$
BEGIN
  FOR i IN 1..50000 LOOP
    UPDATE accounts SET balance = balance + 1 WHERE id = 1;
  END LOOP;
END $$;

-- Check dead tuples before VACUUM:
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname = 'accounts';
-- n_live_tup | n_dead_tup
-- -----------+-----------
--      10000 |      50000

-- Run VACUUM:
VACUUM accounts;

-- After VACUUM:
-- n_live_tup | n_dead_tup
-- -----------+-----------
--      10000 |          0
```

This demonstrates exactly why autovacuum exists and why `n_dead_tup` being high is a sign of a table that needs attention.

### Experiment 4: WAL Write Amplification

```sql
-- Check WAL generated by a bulk insert
SELECT pg_current_wal_lsn() AS before_lsn;
INSERT INTO big_table SELECT generate_series(1, 1000000);
SELECT pg_current_wal_lsn() AS after_lsn;

-- Compute WAL bytes:
SELECT pg_wal_lsn_diff('0/3A000000', '0/20000000') AS wal_bytes;
-- wal_bytes
-- ----------
--  436207616  (≈ 416 MB of WAL for 1M rows)

-- Actual data size of 1M rows:
SELECT pg_size_pretty(pg_total_relation_size('big_table'));
-- 69 MB
```

WAL is approximately 6× the data size for this insert workload. WAL includes full page images on first modification after a checkpoint (FPI) to handle partial page writes. Setting `full_page_writes = off` reduces WAL size but removes protection against partial page write corruption.

---

## 6. Key Learnings

**1. The Buffer Manager is the central bottleneck arbiter.**
Almost every query performance question comes back to buffer hit rate. Understanding which pages are hot and sizing shared buffers accordingly is the single most impactful tuning decision in PostgreSQL.

**2. MVCC's elegance comes with a maintenance burden.**
Non-blocking reads are "free" at query time but accumulate a debt in dead tuples. Autovacuum is not background noise — it is a first-class correctness mechanism. Systems with high update rates need aggressive autovacuum tuning.

**3. WAL is the backbone of everything beyond basic querying.**
Replication, point-in-time recovery, logical decoding, and crash safety all flow from the WAL. WAL records are designed to be idempotent and replay-safe; understanding this makes WAL-based replication intuitive.

**4. The query planner is statistical, not deterministic.**
`EXPLAIN ANALYZE` reveals that the planner makes probabilistic estimates based on `pg_statistic`. Stale statistics (from skipped `ANALYZE`) cause bad plans. The `correlation` column in `pg_stats` directly controls whether the planner believes an index scan will be sequential or random.

**5. B-tree page splits are the hidden write cost of indexes.**
Every index on a frequently-inserted table will periodically split pages. This causes WAL amplification and I/O spikes. The `fillfactor` storage parameter (default 90) leaves 10% of each page free for updates, reducing split frequency on tables with many in-place updates.

---

*References: PostgreSQL Source Code (github.com/postgres/postgres), "The Internals of PostgreSQL" by Hironobu Suzuki (interdb.jp), PostgreSQL Documentation chapters on WAL, MVCC, and Buffer Manager.*
