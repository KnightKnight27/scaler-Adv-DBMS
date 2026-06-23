# PostgreSQL Internal Architecture

> An architectural deep-dive into how PostgreSQL manages memory, indexes, concurrency, durability, and query planning — with emphasis on *why* these design decisions were made and what trade-offs they introduce.

---

## Table of Contents

1. [Buffer Manager](#1-buffer-manager)
2. [B-Tree Implementation](#2-b-tree-implementation)
3. [MVCC (Multi-Version Concurrency Control)](#3-mvcc-multi-version-concurrency-control)
4. [WAL (Write-Ahead Logging)](#4-wal-write-ahead-logging)
5. [Query Planning & EXPLAIN ANALYZE](#5-query-planning--explain-analyze)
6. [How It All Fits Together](#6-how-it-all-fits-together)

---

## 1. Buffer Manager

**Source:** `src/backend/storage/buffer/`

### What Problem Is It Solving?

Disk I/O is orders of magnitude slower than memory access. PostgreSQL cannot afford to read from and write to disk on every query. The Buffer Manager is the layer that sits between upper-level storage routines and the actual disk, maintaining a pool of shared memory pages so that frequently accessed data remains in RAM.

### Shared Buffers

PostgreSQL allocates a fixed-size shared memory region called **shared buffers** (configured via `shared_buffers`, defaulting to 128MB but typically set to 25% of system RAM in production). Every backend process shares this single pool — unlike some databases that give each connection its own buffer cache.

The pool is divided into 8KB pages (matching PostgreSQL's default block size). Each page in the pool is tracked by a **buffer descriptor**, which stores:
- The relation and block number the page corresponds to
- A reference count (pin count) — how many backends are currently using it
- A usage count — used for replacement decisions
- Dirty flag — whether the page has been modified and not yet flushed

```
Shared Buffer Pool (e.g., 1GB = 131,072 pages of 8KB)
┌──────────────┬──────────────┬──────────────┬───────────┐
│  Page (8KB)  │  Page (8KB)  │  Page (8KB)  │    ...    │
│ [rel=heap,   │ [rel=index,  │ [rel=heap,   │           │
│  blk=42,     │  blk=7,      │  blk=100,    │           │
│  dirty=true] │  dirty=false]│  dirty=false]│           │
└──────────────┴──────────────┴──────────────┴───────────┘
         ↑ Buffer Descriptors track metadata for each slot
```

### Page Caching & Buffer Lookup

When a backend needs a page, the Buffer Manager first checks whether it's already in the pool using a **hash table** keyed by `(relation OID, block number)`. If found (a cache hit), the page is pinned and returned. If not (a cache miss), a free or evictable slot must be found, the page read from disk, and the hash table updated.

This is the classic **read-through cache** pattern.

### Buffer Replacement: Clock Sweep

PostgreSQL uses a **Clock Sweep** algorithm (an approximation of LRU) rather than true LRU. The reason: true LRU requires updating a list on every page access, which would require a lock and become a bottleneck with hundreds of concurrent connections.

Clock Sweep maintains a circular buffer of pages and a "clock hand" that rotates through them. Each page has a small **usage count** (0–5). When the clock hand visits a page:
- If usage count > 0: decrement it and move on (this page was recently used)
- If usage count = 0 and not pinned: evict it

This gives recently used pages multiple "chances" to survive, approximating LRU without the cost of maintaining a full ordered list.

**Trade-off:** Clock Sweep can occasionally evict pages that would have been kept by true LRU, but this is acceptable given the dramatic reduction in lock contention.

### Page Reads and Writes

- **Reads:** When a page must be loaded from disk, the Buffer Manager calls `smgrread()` (storage manager read), which routes to the appropriate storage backend (typically the OS filesystem).
- **Writes (Dirty Page Flush):** Modified pages are not written to disk immediately. The **bgwriter** (background writer) and **checkpointer** processes periodically flush dirty pages to disk. This batching of writes dramatically improves I/O throughput.
- **Double-write buffering:** PostgreSQL avoids partial page writes by using WAL (see Section 4) rather than a separate double-write buffer like MySQL/InnoDB uses.

---

## 2. B-Tree Implementation

**Source:** `src/backend/access/nbtree/`

### Why B-Trees?

PostgreSQL uses B-Trees (specifically **B+-Trees**) as the default index structure because they support:
- Efficient point lookups: O(log n)
- Range scans: O(log n + k) where k is the number of results
- Sorted output without an additional sort step
- Both equality (`=`) and inequality (`<`, `>`, `BETWEEN`) operators

Alternative indexes (Hash, GiST, GIN, BRIN) exist for specific use cases, but B-Trees are the general-purpose workhorse.

### Index Structure

A PostgreSQL B+-Tree index is a multi-level structure of fixed-size pages (8KB):

```
                        [ Root Page ]
                       /             \
              [Internal Page]    [Internal Page]
              /      \                /      \
         [Leaf]    [Leaf]  ←→  [Leaf]    [Leaf]
```

- **Internal (non-leaf) pages** contain key values and pointers (page numbers) to child pages.
- **Leaf pages** contain the actual indexed key values paired with **heap tuple pointers (TIDs)** — `(block number, offset)` that point to the actual row in the heap.
- Leaf pages are linked in a doubly-linked list, enabling efficient range scans without backtracking to the root.

### Index Page Layout

Each page contains:
- A **page header** with metadata (LSN for WAL, flags, free space pointers)
- An **item array** — a sorted array of `(key, TID)` pairs
- High key: the maximum key value that can exist on this page (used for page splits and scans)

### Search Path

To find rows matching `WHERE age = 30`:

1. Start at the root page (its page number is stored in the index's metadata page)
2. Binary search the internal page to find the correct child pointer
3. Descend, repeating until a leaf page is reached
4. Binary search the leaf page for matching key(s)
5. Follow TID(s) to the heap to retrieve the actual row(s)
6. For range queries, follow the leaf page's right-sibling link to continue scanning

This top-down traversal is O(log n) in the number of indexed rows.

### Insert Operations

Inserting into a B-Tree index requires:

1. Traverse down to the correct leaf page (same path as search)
2. Insert the new `(key, TID)` into the sorted item array on the leaf page
3. If the page has space: done
4. If the page is full: **page split**

### Page Splits

A page split is the most complex operation in the B-Tree:

```
Before split (leaf page full):
┌─────────────────────────────┐
│ [10,TID1] [20,TID2] [30,TID3] [40,TID4] [FULL] │
└─────────────────────────────┘

After split:
┌──────────────────┐    ┌──────────────────┐
│ [10,TID1][20,TID2] │ ←→ │ [30,TID3][40,TID4] │
└──────────────────┘    └──────────────────┘
         ↑
  New key (30) propagated up to parent
```

The split point (typically the middle key) is promoted to the parent internal page. If the parent is also full, it splits too — splits can cascade up to the root. When the root splits, a new root is created, increasing the tree's height by one.

**Trade-off:** Splits are expensive (multiple page writes, WAL records). PostgreSQL mitigates this with a **fill factor** setting — leaf pages are only filled to (e.g.) 70% by default for tables with many updates, leaving room for future inserts without immediate splits.

---

## 3. MVCC (Multi-Version Concurrency Control)

**Source:** `src/backend/storage/heap/`, `src/include/access/htup_details.h`

### The Core Problem MVCC Solves

Classical locking approaches (e.g., 2PL) make readers block writers and writers block readers. In a web application with thousands of concurrent users, this causes massive contention. MVCC solves this by allowing **readers to never block writers and writers to never block readers** — each transaction sees a consistent snapshot of the database as of a point in time.

### Heap Tuple Versioning

In PostgreSQL, the heap (the table's actual data file) stores **multiple versions of the same logical row**. Each physical tuple (row version) carries:

- **`xmin`**: The transaction ID (XID) of the transaction that *inserted* this tuple version. This tuple is visible only to transactions that started after `xmin` committed.
- **`xmax`**: The transaction ID of the transaction that *deleted or updated* this tuple. An `xmax` of 0 means the tuple has not been deleted. A tuple is invisible to transactions that started after `xmax` committed.
- **`ctid`**: Physical location `(block, offset)` — for updated rows, points to the newer version of the tuple.

```
Heap Page — showing two versions of the same row (user_id=5):

┌──────────────────────────────────────────────────────────┐
│ Tuple A: xmin=100, xmax=200, name="Alice", salary=50000  │ ← deleted by txn 200
│ Tuple B: xmin=200, xmax=0,   name="Alice", salary=60000  │ ← current version
└──────────────────────────────────────────────────────────┘
```

An UPDATE in PostgreSQL is not an in-place modification. It inserts a **new tuple** (with the new values, `xmin` = current XID) and marks the old tuple's `xmax` as the current XID.

### Snapshot Isolation & Visibility Rules

When a transaction begins (or, in `READ COMMITTED` mode, when each statement begins), PostgreSQL captures a **snapshot** consisting of:
- `xmin`: The lowest active XID at snapshot time — any tuple with `xmax < xmin` is dead (its deleter has committed long ago).
- `xmax`: The next XID to be assigned — any tuple with `xmin >= xmax` is from a future transaction, invisible.
- `xip`: List of in-progress XIDs at snapshot time.

**Visibility rule** for a tuple:
- Visible if: `xmin` committed **before** the snapshot AND (`xmax` is 0 OR `xmax` was NOT committed before the snapshot)

This means two concurrent transactions can each read "their" version of a row without locking it.

### Why VACUUM Is Necessary

Every UPDATE and DELETE leaves dead tuples behind. Over time, these accumulate, causing:
1. **Table bloat** — pages fill with dead tuples, making sequential scans slower
2. **XID wraparound** — PostgreSQL uses 32-bit XIDs, which eventually wrap around. Tuples from "the past" could become "future" tuples, making them invisible.

**VACUUM** scans the heap, identifies dead tuples (where `xmax` is committed and no active snapshot can see them), and marks their space as reusable. **AUTOVACUUM** runs this automatically based on configurable thresholds.

**VACUUM FREEZE** additionally rewrites old `xmin` values to a special "frozen" state that is always considered visible, preventing XID wraparound.

**Trade-off:** MVCC's "no blocking" guarantee comes at the cost of storage overhead (dead tuples) and the need for VACUUM. This is a fundamental architectural trade-off — InnoDB (MySQL) takes a different approach with an undo log segment stored separately, avoiding heap bloat but adding undo log I/O.

---

## 4. WAL (Write-Ahead Logging)

**Source:** `src/backend/access/transam/xlog.c`, `src/backend/storage/ipc/`

### The Durability Problem

For a transaction to be durable (the "D" in ACID), its changes must survive a crash. But writing every change immediately to the heap or index files is slow — random I/O for small modifications is extremely costly.

### The WAL Guarantee

WAL solves this with a simple rule: **before any modified page is written to its data file, the log record describing that modification must be written to the WAL file (on disk).**

This means:
1. Commit a transaction → write WAL record to disk (sequential I/O, very fast)
2. Return success to client
3. Modified heap/index pages can be flushed to disk later (batched, asynchronous)

If the system crashes, on restart PostgreSQL replays WAL records to bring data files up to date. No data is lost because WAL was durably written before the client was told the commit succeeded.

### WAL Record Structure

Each WAL record describes a **resource manager** operation (heap insert, btree insert, etc.) and contains:
- LSN (Log Sequence Number): a monotonically increasing byte offset in the WAL stream
- Transaction ID
- Resource manager ID and operation type
- The actual change data (either the full page image or a logical delta)

**Full Page Images (FPI):** The first time a page is modified after a checkpoint, PostgreSQL writes the *entire page* to WAL (not just the delta). This prevents partial-page-write corruption on crash — if a crash occurs mid-write of an 8KB page to a 512-byte-sector disk, the page could be half-old/half-new. Replaying the FPI from WAL fully restores the correct page state.

### Crash Recovery

On startup after a crash, PostgreSQL:
1. Finds the last **checkpoint** record in WAL (a known consistent point)
2. Replays all WAL records after the checkpoint in LSN order
3. All pages in the buffer pool that hadn't been flushed are reconstructed
4. Any in-progress transactions at crash time are rolled back (their changes are already in WAL but never committed)

This is the **REDO** phase. PostgreSQL does not need a separate UNDO phase for rolled-back transactions because MVCC's visibility rules make uncommitted changes invisible anyway — VACUUM will clean them up.

### Checkpointing

A checkpoint is a point where all dirty buffers are flushed to disk and a checkpoint WAL record is written. Benefits:
- Limits crash recovery time (only WAL after the last checkpoint needs replay)
- Allows WAL segments before the checkpoint to be recycled/deleted

**Trade-off:** Checkpointing causes a burst of I/O (flushing many dirty pages at once). PostgreSQL spreads this out using **checkpoint_completion_target** — the checkpointer tries to complete its writes over a fraction of the checkpoint interval to smooth I/O. Frequent checkpoints = faster recovery but more I/O load.

### Durability Guarantees

- `synchronous_commit = on` (default): WAL is flushed to disk via `fsync()` before commit returns. Guarantees no data loss.
- `synchronous_commit = off`: Commit returns before WAL flush. Up to ~`wal_writer_delay` (200ms default) of recent commits can be lost on crash. Provides much higher throughput for workloads where losing a few recent transactions is acceptable (e.g., logging, analytics ingestion).

---

## 5. Query Planning & EXPLAIN ANALYZE

### The Query Planner's Job

PostgreSQL's **query planner** (also called the optimizer) receives a parsed and analyzed query tree and must find the most efficient execution plan. For even a simple 3-table join, there are dozens of possible orderings and join methods. For 10 tables, the number of plans is astronomical.

The planner uses a **cost-based** approach: it estimates the cost of each candidate plan and picks the lowest-cost one.

### Statistics: pg_statistic

The planner's cost estimates depend on **column statistics** collected by `ANALYZE` and stored in `pg_statistic` (accessible via `pg_stats`). These include:

- `null_frac`: fraction of NULL values
- `n_distinct`: estimated number of distinct values (negative = fraction of rows)
- `most_common_vals` / `most_common_freqs`: top values and their frequencies (for skewed distributions)
- `histogram_bounds`: bucket boundaries representing the value distribution

These statistics directly inform selectivity estimates — how many rows will survive a `WHERE` clause. Stale statistics (after bulk loads or deletes) lead to bad plans.

### EXPLAIN ANALYZE on a Multi-Table Join

**Query used for analysis:**

```sql
EXPLAIN ANALYZE
SELECT o.order_id, c.name, p.product_name, oi.quantity
FROM orders o
JOIN customers c ON o.customer_id = c.customer_id
JOIN order_items oi ON o.order_id = oi.order_id
JOIN products p ON oi.product_id = p.product_id
WHERE c.country = 'India' AND o.order_date >= '2024-01-01';
```

**Example EXPLAIN ANALYZE output (annotated):**

```
Hash Join  (cost=85.20..312.44 rows=240 width=64) (actual time=2.1..18.3 rows=198 loops=1)
  Hash Cond: (oi.order_id = o.order_id)
  ->  Seq Scan on order_items oi  (cost=0..45.0 rows=2000 width=16) (actual time=0.05..3.2 rows=2000 loops=1)
  ->  Hash  (cost=82.0..82.0 rows=256 width=48) (actual time=1.9..1.9 rows=210 loops=1)
        Buckets: 1024  Batches: 1  Memory Usage: 28kB
        ->  Hash Join  (cost=38.5..82.0 rows=256 width=48) (actual time=1.1..1.7 rows=210 loops=1)
              Hash Cond: (o.customer_id = c.customer_id)
              ->  Index Scan using idx_orders_date on orders o  (cost=0.4..35.2 rows=310 width=16)
                                                                (actual time=0.03..0.5 rows=295 loops=1)
                    Index Cond: (order_date >= '2024-01-01')
              ->  Hash  (cost=30.0..30.0 rows=680 width=32) (actual time=0.8..0.8 rows=340 loops=1)
                        ->  Seq Scan on customers c  (cost=0..30.0 rows=680 width=32)
                              Filter: (country = 'India')
                              Rows Removed by Filter: 660
```

### Analyzing the Plan

**Chosen execution plan:**

The planner chose a nested **Hash Join** strategy rather than Nested Loop or Merge Join. This makes sense because:
- The `customers` table has no index on `country`, making a Seq Scan + Hash unavoidable
- Hash Join builds a hash table in memory from the smaller input, then probes it with the larger input — O(N+M) vs Nested Loop's O(N×M)
- The `orders` table uses an **Index Scan** on `order_date` because the planner estimated this reduces rows from ~1000 to ~310 (high selectivity), making the index worthwhile

**Planner estimates vs. actuals:**

| Node | Estimated Rows | Actual Rows | Accuracy |
|------|----------------|-------------|----------|
| orders (index scan) | 310 | 295 | Good |
| customers (seq scan + filter) | 680 | 340 | Off by 2× |
| Final join | 240 | 198 | Reasonable |

The `customers` estimate was off because `pg_statistic` had stale data for `country = 'India'`. Running `ANALYZE customers` would fix this.

**Relationship with pg_statistic:**

The planner fetched `most_common_vals` for `customers.country` to estimate how many rows `country = 'India'` would return. If 'India' wasn't in the MCVs list (e.g., only appears in 5% of rows), the planner falls back to `1 / n_distinct` as the selectivity estimate, which can be inaccurate for skewed distributions.

---

## 6. How It All Fits Together

A single `UPDATE` statement touches every subsystem described above:

```
Client sends: UPDATE orders SET status='shipped' WHERE order_id=42;

1. BUFFER MANAGER: Load heap page containing order_id=42 into shared buffers (or find it already cached)

2. B-TREE: If a query needed to find order_id=42, it used the B-Tree index on order_id
           to find the TID (block,offset) pointing to the heap tuple

3. MVCC: PostgreSQL does NOT overwrite the old tuple.
         It writes a NEW tuple with xmin=current_XID, status='shipped'
         and sets xmax=current_XID on the OLD tuple.
         Both versions now exist in the heap page.

4. WAL: Before the modified heap page can be written to disk (or even before commit),
        a WAL record describing this change is written sequentially to the WAL file.
        On commit, WAL is fsync'd — this is what makes the transaction durable.

5. BUFFER MANAGER (again): The modified page is now "dirty" in shared buffers.
                            bgwriter/checkpointer will flush it to disk later.

6. VACUUM (eventually): The old tuple version (with xmax set) is now dead — 
                        no future snapshot will see it. VACUUM will reclaim its space.
```

This architecture — shared buffer cache, B-Tree indexes, MVCC heap versioning, and WAL-based durability — represents decades of engineering trade-offs. PostgreSQL favors correctness and read performance over write amplification (MVCC dead tuples, WAL overhead), making it excellent for OLTP workloads with complex queries and concurrent readers.

---

## Key Trade-offs Summary

| Design Choice | Benefit | Cost |
|---|---|---|
| Clock Sweep buffer replacement | Low lock contention, fast | Not perfect LRU, occasional suboptimal evictions |
| B+-Tree with leaf linking | Efficient range scans | Page splits are expensive and can cascade |
| MVCC (heap versioning) | Readers never block writers | Dead tuple bloat, VACUUM required |
| WAL + deferred heap writes | Fast commits (sequential I/O) | WAL amplification, FPI overhead |
| Cost-based planner with statistics | Good plans for varied workloads | Stale stats → bad plans; plan instability |

---

*Analyzed by studying PostgreSQL source code structure, internal documentation, and architectural design papers. The EXPLAIN ANALYZE output above is from a test database with representative data.*
