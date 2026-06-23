# PostgreSQL Internal Architecture

## 1. Problem Background

PostgreSQL is one of the most architecturally sophisticated open-source databases in existence. Understanding its internals matters because the design choices made in the 1980s at UC Berkeley — and refined over four decades — are not arbitrary. Each internal component solves a specific hard problem in database engineering: how do you make concurrent access safe? How do you survive crashes without losing data? How do you make queries fast without knowing exactly what data looks like?

PostgreSQL's internal architecture answers these questions with four core mechanisms: the **Buffer Manager** (shared page cache), **B-Tree indexes** (fast data access), **MVCC** (safe concurrency), and **WAL** (crash recovery). These four systems interact deeply — understanding one requires understanding the others.

---

## 2. Architecture Overview

### Component Interaction Diagram

```
                         [Client Connection]
                                │
                     [Backend Process (fork'd)]
                                │
              ┌─────────────────▼──────────────────┐
              │           Query Processing          │
              │  Parser → Rewriter → Planner/       │
              │           Optimizer → Executor      │
              └─────────────────┬──────────────────┘
                                │
              ┌─────────────────▼──────────────────┐
              │         Storage Manager             │
              │                                    │
              │  ┌─────────────────────────────┐   │
              │  │      Buffer Manager          │   │
              │  │  (shared_buffers - 8KB pages)│   │
              │  │  [BufDesc array][Buffer pool] │   │
              │  └──────────┬──────────┬────────┘   │
              │             │          │             │
              │    ┌────────▼──┐  ┌───▼────────┐   │
              │    │ Heap Files│  │Index Files  │   │
              │    │(base/OID) │  │(btree, etc.)│   │
              │    └───────────┘  └────────────┘   │
              └─────────────────┬──────────────────┘
                                │
              ┌─────────────────▼──────────────────┐
              │              WAL                    │
              │   WAL Buffers → pg_wal/ directory   │
              │   (written before heap/index pages) │
              └─────────────────────────────────────┘

  Background Processes:
  ├── WAL Writer     (flushes WAL buffers)
  ├── Checkpointer   (periodic WAL checkpoint)
  ├── Autovacuum     (reclaims dead tuples)
  ├── BGWriter       (evicts dirty buffers)
  └── Stats Collector(updates pg_statistic)
```

---

## 3. Internal Design

### 3.1 Buffer Manager (`src/backend/storage/buffer/`)

The Buffer Manager is PostgreSQL's page cache. All reads and writes go through it — no backend process ever touches disk directly.

**Core Data Structures:**

```
Shared Memory Layout:
┌──────────────────────────────────────────────────┐
│  BufferDescriptors[NBuffers]                     │
│  (array of BufDescData — one per buffer slot)    │
│  Each BufDescData contains:                      │
│    - tag: {relforknum, blocknum} — identifies    │
│            which page this slot holds            │
│    - state: {ref count, usage count, dirty flag, │
│              valid, IO in progress flags}        │
│    - content_lock: RWLock for concurrent access  │
│    - io_in_progress_lock: spinlock               │
├──────────────────────────────────────────────────┤
│  BufferBlocks[NBuffers]                          │
│  (the actual 8KB page data — one per slot)       │
├──────────────────────────────────────────────────┤
│  BufferHashTable                                 │
│  (maps page tag → buffer slot index)             │
└──────────────────────────────────────────────────┘
```

**Page Read Path:**
```
ReadBuffer(rel, blockNum)
  │
  ├── Hash lookup: is page in BufferHashTable?
  │     YES → pin buffer (increment refcount), return
  │     NO  → need to load from disk
  │
  ├── Select victim buffer (clock sweep algorithm)
  │     Check usage_count: if > 0, decrement and skip
  │     If usage_count == 0 → candidate for eviction
  │     If dirty → write to disk first (or wait for BGWriter)
  │
  ├── Evict old page from slot
  ├── Update BufferHashTable: old tag → removed, new tag → this slot
  ├── Read 8KB page from disk into BufferBlocks[slot]
  └── Return pinned buffer
```

**Buffer Replacement — Clock Sweep:**
PostgreSQL uses a clock sweep algorithm (approximation of LRU). Each buffer has a `usage_count` (0–5). On access, count is incremented. The clock hand sweeps through buffers; if usage_count > 0, it decrements and moves on; if 0, the buffer is evicted. This is O(1) and avoids the overhead of a true LRU linked list. It's a deliberate trade-off: slightly less optimal replacement in exchange for much simpler locking.

**Why This Matters Operationally:**
`shared_buffers` is the most important PostgreSQL tuning parameter. Too small → constant disk I/O. Too large → OS page cache is starved. The conventional wisdom (set to 25% of RAM) exists because PostgreSQL also relies on the OS page cache as a second-level cache. The buffer manager and OS cache work together, not in competition.

---

### 3.2 B-Tree Implementation (`src/backend/access/nbtree/`)

PostgreSQL implements B+ trees (all data at leaves, internal nodes only hold keys for navigation).

**Index Page Layout:**

```
B-Tree Page (8KB):
┌─────────────────────────────────────────────────────┐
│ PageHeaderData (24 bytes)                           │
│  - lsn: WAL position of last change                 │
│  - flags: leaf/internal, has high key, etc.         │
├─────────────────────────────────────────────────────┤
│ BTPageOpaqueData (special space, 16 bytes)          │
│  - btpo_prev, btpo_next: sibling page links         │
│  - btpo_level: 0 = leaf                             │
│  - btpo_flags: BTP_LEAF, BTP_ROOT, etc.             │
├─────────────────────────────────────────────────────┤
│ ItemIdData array (line pointers)                    │
│  - Position 1: "high key" (max key on this page)    │
│  - Positions 2..N: index tuples                     │
├─────────────────────────────────────────────────────┤
│ Index Tuples (IndexTupleData):                      │
│  - t_tid: ctid pointing to heap tuple               │
│  - t_info: size and flags                           │
│  - key data (the indexed column value)              │
└─────────────────────────────────────────────────────┘
```

**Search Path:**
```
_bt_search(key)
  Start at root page (cached in metapage)
  Loop:
    Binary search on page's line pointer array
    Find correct downlink (internal) or item (leaf)
    If internal: follow downlink to child page
    If leaf: return matching item(s)
  
  Result: heap ctid → fetch actual tuple via heap_fetch()
```

**Insert and Page Splits:**
When a leaf page is full, PostgreSQL splits it:
1. Allocate a new page
2. Move the upper half of entries to the new page
3. Insert a new downlink into the parent page (which may also split, propagating up)
4. Update sibling links (doubly-linked leaf list)

The critical property: **B-tree splits are logged in WAL atomically**. A crash mid-split leaves a "half-dead" page, which the next access cleans up. This is the `nbtree` "vacuum" cycle.

**Why B-Trees?**
B-trees give O(log N) search, insert, and delete. For a table with 100 million rows and 8KB pages holding ~200 index entries each, the tree is at most 4-5 levels deep. Most production trees are 3-4 levels. PostgreSQL caches the root page, so most lookups are 2-3 page reads regardless of table size.

---

### 3.3 MVCC (Multi-Version Concurrency Control)

MVCC is PostgreSQL's solution to the concurrency problem: how do you let readers and writers operate simultaneously without blocking each other?

**Tuple Header Fields:**

```
HeapTupleHeader (every row in the heap):
┌─────────────────────────────────────────────┐
│ t_xmin  (uint32): txn ID that inserted row  │
│ t_xmax  (uint32): txn ID that deleted row   │
│          (0 if row is still "alive")         │
│ t_ctid  (ItemPointer): self or updated vers │
│ t_infomask: flags (XMIN_COMMITTED,          │
│             XMAX_COMMITTED, etc.)            │
│ t_hoff: offset to actual tuple data         │
└─────────────────────────────────────────────┘
```

**Visibility Rule (simplified):**

A tuple is visible to a transaction with snapshot `snap` if:
```
xmin is committed AND xmin < snap.xmax
AND
(xmax is NOT committed OR xmax >= snap.xmax OR xmax is in snap.xip[])
```

In plain English: the row was inserted by a committed transaction before our snapshot, and either hasn't been deleted yet, or was deleted by a transaction we can't see yet.

**Update Lifecycle:**

```sql
-- Original: xmin=100, xmax=0, data='Alice'
UPDATE employees SET name='Alice Smith' WHERE id=1;
-- Transaction 200 runs this
```

Result in heap:
```
Tuple v1: xmin=100, xmax=200, ctid=(0,1), name='Alice'   ← dead to txn ≥ 200
Tuple v2: xmin=200, xmax=0,   ctid=(0,2), name='Alice Smith' ← live
```

Transaction 150 (started before 200 committed) still sees v1. Transaction 201 sees v2. Neither transaction blocks the other.

**Why VACUUM is Necessary:**

The dead tuple (v1) is taking up space on the heap page. It will never be visible to any future transaction, but PostgreSQL doesn't reclaim it automatically during normal operation. VACUUM scans the heap, identifies dead tuples (checking `pg_xact` to see if `xmax` is committed), and marks their space as reusable. Without VACUUM, tables grow unboundedly under high UPDATE/DELETE workloads. This is called "table bloat."

**Transaction ID Wraparound:**

`xmin`/`xmax` are 32-bit integers (~4 billion values). PostgreSQL uses modular arithmetic to determine "before" vs "after." After ~2 billion transactions, IDs start to wrap. Aggressive VACUUMING is required to freeze old tuples (set `xmin = 2 = FrozenTransactionId`) before wraparound occurs. This is the "transaction ID wraparound problem" — a famous operational concern for busy PostgreSQL databases.

---

### 3.4 WAL (Write-Ahead Logging)

WAL is the mechanism that makes PostgreSQL crash-safe.

**The Rule:** Before any heap or index page is written to disk, the corresponding WAL record describing the change must be flushed to disk.

**Why This Guarantees Durability:**

If PostgreSQL crashes mid-write:
- If the WAL record was flushed: replay it on recovery to redo the change
- If the WAL record was NOT flushed: the change never happened (the page is in its old state)

This means the database on disk is always consistent: either you have the WAL record and can redo the change, or you don't and the original page is still there.

**WAL Record Structure:**

```
WAL Record:
┌──────────────────────────────────────────────┐
│ XLogRecord header:                           │
│   - xl_tot_len: total length                 │
│   - xl_xid: transaction ID                  │
│   - xl_prev: LSN of previous record          │
│   - xl_info: operation type                  │
│   - xl_rmid: resource manager (heap, btree, │
│              clog, xact, etc.)               │
├──────────────────────────────────────────────┤
│ Block reference(s):                          │
│   - file/relation/block identifiers          │
│   - "full page image" if needed (FPI)        │
├──────────────────────────────────────────────┤
│ Operation-specific data:                     │
│   - For INSERT: the new tuple                │
│   - For UPDATE: old/new tuple diff           │
│   - For DELETE: ctid of deleted tuple        │
└──────────────────────────────────────────────┘
```

**Full Page Images (FPI):** After a checkpoint, the first write to any page includes the entire 8KB page in the WAL record. This handles "torn page" scenarios (crash mid-write of an 8KB page to 512-byte sector storage). FPIs make WAL larger but recovery simpler.

**Checkpointing:**

The checkpointer process periodically writes all dirty buffers to disk and writes a checkpoint WAL record. On recovery, PostgreSQL only replays WAL from the last checkpoint. Without checkpoints, WAL replay would have to start from the beginning of time.

```
Timeline:
  ...─── [Checkpoint A] ─── changes ─── [Checkpoint B] ─── crash
                                                              │
                       Recovery starts here ─────────────────┘
                       (replays WAL from Checkpoint B)
```

**WAL-Enabled Features:**
- **PITR**: Archive WAL segments → restore to any point in time
- **Streaming replication**: Ship WAL to standby servers in real-time
- **Logical replication**: Decode WAL into logical changes (INSERT/UPDATE/DELETE) for selective replication

---

### 3.5 Query Planning and pg_statistic

The query planner chooses the best execution plan by estimating the cost of alternatives. Its inputs are statistics about the data, stored in `pg_statistic`.

**Statistics Collected by ANALYZE:**

```sql
SELECT * FROM pg_stats WHERE tablename = 'orders' AND attname = 'status';
-- Shows:
--   null_frac: fraction of nulls
--   n_distinct: estimated distinct values (-1 = all distinct)
--   most_common_vals: array of most frequent values
--   most_common_freqs: their frequencies
--   histogram_bounds: bucket boundaries for distribution
--   correlation: physical vs logical sort order (affects index scan cost)
```

**How the Planner Uses This:**

```sql
EXPLAIN ANALYZE SELECT * FROM orders o
JOIN customers c ON o.customer_id = c.id
WHERE c.country = 'IN' AND o.status = 'pending';
```

1. Planner estimates rows matching `c.country = 'IN'` using `most_common_freqs` for 'IN'
2. Estimates rows matching `o.status = 'pending'` similarly
3. Estimates join output using both selectivities
4. Computes cost of: nested loop, hash join, merge join
5. Picks lowest-cost plan

**The relationship with reality:** If statistics are stale (ANALYZE hasn't run), `n_distinct` and histogram bounds are wrong. The planner may drastically underestimate rows, choose a nested loop instead of a hash join, and query performance collapses. This is why autovacuum runs ANALYZE automatically, and why sudden query slowdowns after large data loads are often fixed by a manual `ANALYZE tablename`.

---

## 4. Design Trade-offs

### Buffer Manager

**Clock sweep vs LRU:** LRU would give optimal cache hit rates but requires a doubly-linked list with lock contention on every access. Clock sweep is ~5% less optimal in hit rate but has dramatically lower lock contention. At high concurrency (hundreds of backends), this matters enormously. PostgreSQL chose throughput over optimal replacement.

**Shared buffer size:** A larger shared_buffers reduces disk I/O but consumes RAM. The optimal size depends on working set size (the set of hot pages). If working set fits in shared_buffers, cache hit rates approach 99%+. If not, the buffer manager becomes a bottleneck.

### MVCC

**Append-only vs undo log:** PostgreSQL's append-only model (never overwrite tuples) avoids the complexity of an undo log (which InnoDB uses). Undo logs allow in-place updates, which is faster for single-row point updates. PostgreSQL's model is simpler but produces dead tuples that VACUUM must reclaim. The design choice trades operational complexity (VACUUM tuning) for implementation simplicity.

**Snapshot overhead:** Taking a snapshot requires reading the list of currently active transaction IDs (`ProcArray`). Under very high connection counts, this becomes a scalability bottleneck. This is one reason why PostgreSQL doesn't scale linearly with connection count — connection poolers help by reducing the number of actual backend processes.

### B-Tree

**Page splits cause WAL amplification:** A leaf split propagates upward potentially causing parent splits. Each split generates WAL records. Under heavy random insert workloads, split cascades can make insert throughput spiky. This is why bulk loads use `COPY` + index rebuild (CREATE INDEX) rather than incremental inserts.

### WAL

**fsync overhead:** Every transaction commit requires an fsync (or fdatasync) to guarantee the WAL is durable. This serializes commits through the I/O subsystem. Commit group batching (`synchronous_commit = off` or async standby) trades durability for throughput. The correct setting depends on business requirements.

**FPI bloat:** Full page images make WAL segments larger, increasing disk usage and replication bandwidth. Setting `wal_compression = on` compresses FPIs and reduces this overhead with modest CPU cost.

---

## 5. Experiments / Observations

### EXPLAIN ANALYZE on a Multi-Table Join

```sql
EXPLAIN ANALYZE
SELECT c.name, COUNT(o.id) AS order_count, SUM(o.amount) AS total
FROM customers c
JOIN orders o ON o.customer_id = c.id
WHERE c.country = 'IN'
GROUP BY c.name
ORDER BY total DESC
LIMIT 10;
```

**Sample Output Analysis:**
```
Limit  (cost=4521.33..4521.35 rows=10) (actual time=87.2..87.3 rows=10)
  -> Sort  (cost=4521.33..4541.33 rows=8000)
      -> HashAggregate (groups=8000)
          -> Hash Join  (cost=234.00..3890.00 rows=24000)
                Hash Cond: (o.customer_id = c.id)
                -> Seq Scan on orders  (rows=800000)
                -> Hash  (actual rows=8000)
                    -> Seq Scan on customers
                         Filter: (country = 'IN')
                         Rows Removed by Filter: 92000
```

**Insights:**
- Planner chose **Hash Join** because the filtered customers set (~8000 rows) fits in `work_mem` as a hash table
- Sequential scan on `customers` is chosen over an index on `country` because 8% selectivity doesn't justify index overhead for this table size
- **Planner estimate vs actual rows** divergence indicates stale statistics — run `ANALYZE customers` to fix

### Buffer Manager Observation

```sql
SELECT buffers_clean, buffers_alloc, maxwritten_clean 
FROM pg_stat_bgwriter;
```
`maxwritten_clean > 0` indicates the BGWriter is hitting its `bgwriter_lru_maxpages` limit, meaning backends are doing their own eviction (slower path). Increase `bgwriter_lru_maxpages` or `bgwriter_delay` to reduce backend stalls.

### MVCC Dead Tuple Observation

```sql
-- After many updates:
SELECT n_dead_tup, n_live_tup, last_autovacuum 
FROM pg_stat_user_tables 
WHERE relname = 'accounts';
-- n_dead_tup >> n_live_tup indicates bloat
-- Run: VACUUM ANALYZE accounts;
```

### WAL Generation Rate

```sql
SELECT pg_size_pretty(pg_wal_lsn_diff(pg_current_wal_lsn(), '0/0'));
-- Monitor WAL generation rate under load
-- High WAL generation → more I/O, more replication lag
```

---

## 6. Key Learnings

**Every component is interconnected.** The Buffer Manager, MVCC, B-Tree, and WAL are not independent. WAL records reference buffer pages. MVCC depends on the heap structure that the Buffer Manager caches. B-Tree page splits generate WAL. VACUUM reads heap pages through the Buffer Manager to find dead MVCC tuples. Pulling on any one thread eventually touches all the others.

**MVCC is the most architecturally distinctive choice.** The decision to never overwrite tuples in-place defines everything downstream: tuple headers need xmin/xmax, VACUUM exists, the visibility map exists, hint bits exist. InnoDB made a different choice (undo logs + in-place updates). Both work, but they produce very different operational profiles.

**Statistics are not just metadata.** The quality of query plans is entirely dependent on pg_statistic. A database with great schema design but stale statistics will have terrible query performance. ANALYZE is not optional housekeeping — it's a core part of the database's correctness for query optimization.

**WAL is more than crash recovery.** The fact that WAL is a complete, ordered log of all database changes makes it the foundation for replication, PITR, and logical decoding. This was likely not fully anticipated when WAL was first designed for crash recovery alone, but it has become one of PostgreSQL's most powerful architectural properties.

**Tuning is architectural.** Parameters like `shared_buffers`, `work_mem`, `max_connections`, and `checkpoint_timeout` are not cosmetic — they directly control how the Buffer Manager, sort operations, connection overhead, and WAL checkpoint frequency work. Mistuning any of them creates real architectural bottlenecks.

---

*References: PostgreSQL source code (github.com/postgres/postgres), PostgreSQL documentation, "The Internals of PostgreSQL" by Hironobu Suzuki (interdb.jp), src/backend/storage/buffer/README, src/backend/access/nbtree/README*
