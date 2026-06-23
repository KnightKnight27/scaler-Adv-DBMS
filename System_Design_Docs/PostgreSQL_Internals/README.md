# Topic 2: PostgreSQL Internal Architecture

> **Author:** Akshansh Sinha | Advanced DBMS — System Design Discussion

---

## 1. Problem Background

### Why Study PostgreSQL Internals?

PostgreSQL is not just a database — it is a reference implementation of nearly every major idea in relational database systems: MVCC, WAL-based durability, cost-based query planning, buffer management, and extensible index types. Understanding its internals is essentially understanding the engineering principles that underpin all modern RDBMS systems.

PostgreSQL began as POSTGRES at UC Berkeley under Michael Stonebraker in 1986. The design philosophy was: **do not make decisions that force future extensibility to be broken**. This is why PostgreSQL stores everything — system catalogs, statistics, indexes — uniformly as heap relations. There is no hard-coded "system table" layer separate from user tables at the storage level.

The key engineering challenge PostgreSQL solves: **how do you allow many concurrent users to read and write a shared set of data pages with strong consistency guarantees, while remaining fast enough to be a production database?**

The answer involves four tightly coupled systems:
1. **Buffer Manager** — keeps hot pages in memory
2. **MVCC** — allows readers and writers to coexist without blocking
3. **WAL** — guarantees durability without synchronous page writes
4. **Query Planner** — finds the cheapest execution plan using statistics

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        POSTGRESQL BACKEND PROCESS                        │
│                                                                          │
│  ┌───────────┐   ┌────────────┐   ┌──────────────────────────────────┐  │
│  │  SQL      │   │  Parser    │   │ Rewriter (rule system)           │  │
│  │  input    │──▶│  (gram.y)  │──▶│ (views, rules → query trees)     │  │
│  └───────────┘   └────────────┘   └─────────────┬────────────────────┘  │
│                                                  │                       │
│                                      ┌───────────▼────────────────────┐  │
│                                      │ Planner / Optimizer            │  │
│                                      │  - pg_statistic analysis       │  │
│                                      │  - Cost estimation             │  │
│                                      │  - Join ordering (DP/genetic)  │  │
│                                      │  - Plan tree generation        │  │
│                                      └───────────┬────────────────────┘  │
│                                                  │                       │
│                                      ┌───────────▼────────────────────┐  │
│                                      │ Executor                       │  │
│                                      │  - SeqScan, IndexScan, etc.    │  │
│                                      │  - Hash Join, Merge Join, NL   │  │
│                                      │  - Aggregate, Sort, Limit      │  │
│                                      └───────────┬────────────────────┘  │
│                                                  │                       │
└──────────────────────────────────────────────────┼──────────────────────┘
                                                   │
                              ┌────────────────────▼────────────────────────┐
                              │           STORAGE SUBSYSTEM                  │
                              │                                              │
                              │  ┌─────────────────────────────────────┐    │
                              │  │ Buffer Manager (shared_buffers)     │    │
                              │  │  - 8KB page cache                   │    │
                              │  │  - Clock sweep replacement          │    │
                              │  │  - Dirty page tracking             │    │
                              │  └──────────────┬──────────────────────┘    │
                              │                 │                            │
                              │  ┌──────────────▼──────────────────────┐    │
                              │  │ Smgr (Storage Manager)              │    │
                              │  │  - md.c: magnetic disk (files)      │    │
                              │  └──────────────┬──────────────────────┘    │
                              │                 │                            │
                              │       ┌─────────▼──────────┐                │
                              │       │   OS Page Cache     │                │
                              │       └─────────┬──────────┘                │
                              │                 │                            │
                              │       ┌─────────▼──────────┐                │
                              │       │   Disk / SSD        │                │
                              │       └────────────────────┘                │
                              └──────────────────────────────────────────────┘

Background processes:
  - WAL Writer: flushes WAL buffers to pg_wal/
  - Checkpointer: flushes dirty buffer pages to disk
  - Autovacuum Launcher/Worker: reclaims dead tuples
  - Background Writer: proactively writes dirty pages
  - Stats Collector: gathers pg_stat_* data
```

---

## 3. Internal Design

### 3.1 Buffer Manager (`src/backend/storage/buffer/`)

The buffer manager is the caching layer between executor-level page requests and actual disk I/O. Its purpose: **maximize the fraction of page accesses that are served from RAM**.

#### Buffer Pool Structure

```
Shared Memory Layout:
┌─────────────────────────────────────────────────────────┐
│  BufferDescriptor array (one per buffer slot)           │
│  ┌─────────────────────────────────────────────────┐    │
│  │ buf_id, tag (relid+blocknum), state flags       │    │
│  │ refcount, usage_count, content_lock (LWLock)    │    │
│  └─────────────────────────────────────────────────┘    │
├─────────────────────────────────────────────────────────┤
│  Buffer Data (N × 8192 bytes)                          │
│  N = shared_buffers / 8KB  (typical: 16384 buffers)    │
│  Each slot holds exactly one 8KB database page          │
└─────────────────────────────────────────────────────────┘
```

#### Page Read Path (ReadBuffer → ReleaseBuffer)

```
ReadBuffer(relation, block_number):
  1. Compute hash of (relid, blocknum) → find hash bucket
  2. Search hash table for existing buffer:
     a. FOUND: pin the buffer (increment refcount), return it
  3. NOT FOUND: must load from disk
     a. Run clock sweep to find a victim buffer
     b. If victim is DIRTY: write it to disk first (or WAL must be flushed)
     c. Assign buffer to new (relid, blocknum)
     d. Read page from disk via smgrread()
     e. Return pinned buffer

ReleaseBuffer:
  - Decrement pin count (refcount)
  - Decrement usage_count (for clock sweep)
```

#### Clock Sweep Replacement Algorithm

PostgreSQL uses a **clock sweep** (approximate LRU) rather than true LRU:

```
Each buffer has usage_count (0–5):
  - When a buffer is accessed: usage_count++ (up to max)
  - Clock hand scans buffers in circular order:
    - If usage_count > 0: decrement usage_count, skip
    - If usage_count == 0: candidate for eviction
    - If not pinned (refcount == 0): evict it

Why not true LRU?
  - True LRU needs a global list with locks on every access
  - Clock sweep is O(1) with minimal lock contention
  - Approximate LRU is empirically just as effective
```

**Ring buffer for bulk operations**: Sequential scans (large table scans) use a dedicated small ring buffer (256 KB default) to avoid thrashing the main buffer pool. This is why a full table scan doesn't evict hot OLTP pages.

#### Dirty Page Management

When a page is modified:
1. The modifying backend marks the BufferDescriptor as `BM_DIRTY`
2. The page is NOT immediately written to disk
3. The **background writer** proactively writes some dirty pages between checkpoints
4. The **checkpointer** writes ALL dirty pages at checkpoint time
5. WAL is always written before the page (WAL-before-page rule)

### 3.2 B-Tree Implementation (`src/backend/access/nbtree/`)

#### B-Tree Structure

PostgreSQL implements a variant of Lehman-Yao concurrent B-Trees, which allow safe concurrent operations without holding locks on ancestor pages during traversal.

```
B-Tree Page Layout:
┌──────────────────────────────────────────────────────────────┐
│ PageHeader (24 bytes)                                        │
├──────────────────────────────────────────────────────────────┤
│ BTPageOpaqueData (special area, at end of page):            │
│   btpo_prev, btpo_next (sibling pointers, doubly linked)   │
│   btpo_level (0 = leaf), btpo_flags (leaf/internal/root)   │
├──────────────────────────────────────────────────────────────┤
│ Line Pointer Array                                          │
├──────────────────────────────────────────────────────────────┤
│ Index Tuples: [key value] → [heap TID (ctid)]              │
│   On leaf pages: TID points to heap tuple                  │
│   On internal pages: TID points to child B-Tree page       │
└──────────────────────────────────────────────────────────────┘
```

Key design decisions:
- **Rightlink pointers**: Every page has a `btpo_next` pointer. This is the Lehman-Yao trick — during a page split, the rightlink is set before the parent pointer is updated, ensuring that a concurrent reader following the old pointer can still find new keys by following the rightlink.
- **High key**: Internal pages store a "high key" — the maximum key on the page. This allows efficient range query termination.

#### Search Path

```
_bt_search(key):
  1. Start at root page (cached in metapage)
  2. At each internal page:
     a. Binary search through keys for correct child pointer
     b. Follow child pointer down
  3. Repeat until level == 0 (leaf page)
  4. Binary search leaf for key
  5. Return TID (ctid) → use to fetch heap tuple
```

Cost: O(log_B(N)) page reads where B = branching factor (~200–400 for typical indexes).

#### Page Split

```
_bt_split(page, new_tuple):
  1. Allocate new right-sibling page
  2. Move upper half of tuples to right sibling
  3. Set right sibling's rightlink to old page's rightlink
  4. Set old page's rightlink to new right sibling
  5. Insert "downlink" (separator key + right sibling TID) into parent
  6. If parent is full: recursively split parent

Why this ordering?
  - The rightlink is set atomically before the parent update
  - A concurrent reader following the old route can still find new data
    by following the rightlink (the Lehman-Yao safety invariant)
```

### 3.3 MVCC (Multi-Version Concurrency Control)

MVCC is the mechanism that allows PostgreSQL to provide **snapshot isolation** without readers blocking writers.

#### Tuple Versioning in the Heap

Every heap tuple has two hidden system columns:

```
HeapTupleHeader:
  xmin  — XID of the transaction that INSERTED this tuple
  xmax  — XID of the transaction that DELETED/UPDATED this tuple
          (0 if the tuple is still "live" to the inserting transaction)
  ctid  — self-pointer (relid, page, offset)
          Updated tuples: ctid points to the NEW version of the tuple
  infomask — flags: HEAP_XMIN_COMMITTED, HEAP_XMAX_COMMITTED, etc.
```

**INSERT:** Creates a new tuple with `xmin = current_txid`, `xmax = 0`

**UPDATE:** Marks old tuple's `xmax = current_txid`, inserts new tuple at a new location with `xmin = current_txid`. The old tuple's `ctid` is updated to point to the new version.

**DELETE:** Marks tuple's `xmax = current_txid`. No data moved.

#### Visibility Rules

A tuple is **visible** to transaction `T` with snapshot `S` if:
```
xmin is committed AND xmin is in the past of S
  AND
  (xmax is 0 OR xmax is NOT committed OR xmax is in the future of S)
```

More precisely, a snapshot `S = (xmin_S, xmax_S, xip_S)` where:
- `xmin_S`: oldest active XID at snapshot time
- `xmax_S`: next XID to be assigned
- `xip_S`: set of currently in-progress XIDs

```
Tuple with (xmin, xmax) is visible if:
  xmin < xmin_S AND xmin NOT IN xip_S  (inserter committed before snapshot)
  AND
  (xmax == 0 OR xmax >= xmax_S OR xmax IN xip_S)  (deleter not committed)
```

This logic runs on every tuple access during a sequential scan or index scan. It requires no lock, only arithmetic comparisons.

#### Why VACUUM is Necessary

MVCC never modifies tuples in place — it always appends new versions. Over time:

```
Before UPDATE (3 clients did UPDATE on same row):
  Tuple v1: xmin=100, xmax=101 (dead — txn 101 committed)
  Tuple v2: xmin=101, xmax=102 (dead — txn 102 committed)
  Tuple v3: xmin=102, xmax=103 (dead — txn 103 committed)
  Tuple v4: xmin=103, xmax=0   (live — current version)

Dead tuples v1, v2, v3 occupy space on disk pages.
They are invisible to all future transactions but still slow down scans.

VACUUM:
  1. Scans the heap looking for dead tuples
  2. Marks their slots as reusable (sets LP_DEAD in line pointer)
  3. Updates the Free Space Map (FSM)
  4. (VACUUM FULL: rewrites entire table compactly — exclusive lock)
```

Without VACUUM, tables grow indefinitely and sequential scans read increasing amounts of dead data. Critically, without `VACUUM FREEZE`, transaction IDs would wrap around in ~2 billion transactions, causing data corruption.

### 3.4 WAL (Write-Ahead Logging)

#### The Core Invariant

> **WAL Rule**: A dirty page (modified buffer) must NOT be written to disk until the WAL record that describes the modification has been written to disk first.

This is enforced by checking the page's LSN (Log Sequence Number) against the WAL write position:
- If `page.lsn > wal_write_position`: flush WAL before writing the page

#### WAL Record Structure

```
XLogRecord:
  xl_tot_len   — total length of this record
  xl_xid       — transaction ID that wrote this record
  xl_prev      — LSN of previous WAL record (for backward scan)
  xl_info      — record type (HEAP_INSERT, HEAP_UPDATE, BTREE_SPLIT, etc.)
  xl_rmid      — resource manager ID (Heap, BTree, Transaction, etc.)
  data         — resource-manager specific payload
                 (e.g., for HEAP_INSERT: page LSN, offset, tuple data)
```

Resource managers: each subsystem (Heap, BTree, Sequence, Transaction) registers itself as an RM. WAL replay calls the RM's `redo()` function.

#### Crash Recovery

```
On startup after crash:
  1. Find last checkpoint record in pg_control
  2. Start replaying WAL from checkpoint's redo LSN
  3. For each WAL record:
     a. Read the target page
     b. If page.lsn < record.lsn: apply the record (redo)
     c. If page.lsn >= record.lsn: skip (page already has this change)
  4. Rebuild in-progress transactions:
     a. If COMMIT record found: mark committed
     b. If no COMMIT record: roll back (replay undo)
  5. Database is now consistent
```

#### Checkpointing

Checkpoints reduce crash recovery time. At checkpoint time:
1. All dirty buffers are written to disk (in background over `checkpoint_completion_target` duration)
2. A `CHECKPOINT` WAL record is written
3. `pg_control` is updated with the new checkpoint LSN

This means crash recovery only needs to replay WAL from the last checkpoint — not from the beginning of time.

### 3.5 Query Planner and Statistics

#### EXPLAIN ANALYZE on a Multi-Table Join

```sql
CREATE TABLE orders (id SERIAL PRIMARY KEY, customer_id INT, total NUMERIC);
CREATE TABLE customers (id SERIAL PRIMARY KEY, name TEXT, city TEXT);
CREATE INDEX ON orders(customer_id);
INSERT INTO orders SELECT i, (i % 1000)+1, random()*1000 FROM generate_series(1,500000) i;
INSERT INTO customers SELECT i, 'Customer '||i, 'City '||i FROM generate_series(1,1000) i;
ANALYZE orders; ANALYZE customers;

EXPLAIN ANALYZE
SELECT c.name, SUM(o.total)
FROM orders o
JOIN customers c ON o.customer_id = c.id
WHERE c.city LIKE 'City 1%'
GROUP BY c.name
ORDER BY SUM(o.total) DESC
LIMIT 10;
```

**Expected plan structure:**
```
Limit  (cost=... rows=10 ...)
  → Sort (cost=... sort key: sum(o.total))
    → HashAggregate (cost=... group key: c.name)
      → Hash Join (cost=... hash cond: o.customer_id = c.id)
          → Seq Scan on orders (cost=... rows=500000)
          → Hash (cost=... rows=... batches=1 ...)
              → Seq Scan on customers (filter: city LIKE 'City 1%')
```

**Plan analysis:**
- **Hash Join** chosen over Nested Loop because both sides are large. Hash join: O(N+M). Nested Loop without index: O(N×M).
- **Seq Scan on orders** rather than index scan because the query touches a large fraction of rows (500k/500k). Index scan + heap fetch would be slower than a sequential scan.
- **Seq Scan on customers** because `LIKE 'City 1%'` cannot use a standard B-Tree index efficiently (unless `text_pattern_ops` index exists).

#### pg_statistic and the Planner

`ANALYZE` collects statistics into `pg_statistic` (per column):
- `stanullfrac`: fraction of NULL values
- `stawidth`: average column width in bytes
- `stadistinct`: number of distinct values (negative = fraction of rows)
- `stavalues1`: most common values (MCV list)
- `stanumbers1`: frequencies of MCVs
- `stavalues2`: histogram bounds (for range queries)

The planner uses these to estimate **selectivity** — what fraction of rows a predicate will match. Wrong statistics → wrong plan → catastrophic performance.

```sql
-- Verify statistics:
SELECT attname, stadistinct, correlation
FROM pg_stats
WHERE tablename = 'orders';
```

`correlation` is crucial: a value near 1.0 means the physical order of rows matches the index order → index scans are efficient. A value near 0 means heap fetches are random → index scans may be slower than sequential scans.

---

## 4. Design Trade-Offs

### 4.1 Buffer Management: Why Clock Sweep, Not LRU?

True LRU requires maintaining a doubly-linked list, and every buffer access must update the list position — this requires a global lock. With hundreds of concurrent backends each making thousands of buffer accesses per second, this lock would be catastrophically contended.

Clock sweep approximates LRU with only a `usage_count` field and the clock hand's `spinlock`. This is the classic systems trade-off: **exact algorithm with high synchronization cost vs approximate algorithm with minimal synchronization cost**.

### 4.2 MVCC vs Locking: Concurrency vs Space Efficiency

| | MVCC (PostgreSQL) | Locking (traditional) |
|---|---|---|
| Reader-writer conflict | None — readers don't block | Writers block readers |
| Space overhead | Dead tuples accumulate | Minimal extra space |
| Maintenance | VACUUM required | Lock manager state only |
| Snapshot cost | Snapshot taken per transaction | None |
| Long transactions | Hold old tuple versions (bloat) | Hold locks (deadlock risk) |

MVCC makes the trade: **pay with space (dead tuples) to gain concurrency (no reader-writer blocking)**.

### 4.3 WAL and the Durability–Performance Trade-Off

`synchronous_commit = on` (default): Every COMMIT waits for WAL to be flushed to disk. This is safe but adds 1–10ms latency per transaction.

`synchronous_commit = off`: COMMIT returns immediately. WAL is flushed asynchronously. If the server crashes within the last `wal_writer_delay` (200ms default), the last few committed transactions may be lost. Data is never corrupted — only recent commits may need to be redone.

This is a carefully designed **durability vs latency trade-off**. PostgreSQL exposes it as a per-transaction knob, allowing applications to make intentional choices.

---

## 5. Experiments / Observations

### Experiment 1: Observing Buffer Cache in Action

```sql
-- Enable pg_buffercache extension
CREATE EXTENSION pg_buffercache;

-- Before running query:
SELECT count(*) FROM pg_buffercache WHERE reldatabaseid = (SELECT oid FROM pg_database WHERE datname = 'mydb');

-- Run a large sequential scan
SELECT count(*) FROM orders;

-- After query — check buffer usage
SELECT c.relname, count(*) AS buffers
FROM pg_buffercache b
JOIN pg_class c ON b.relfilenode = c.relfilenode
GROUP BY c.relname
ORDER BY buffers DESC LIMIT 10;
```

**Observation**: After the scan, `orders` table pages fill the ring buffer (not the main pool). OLTP workload pages remain cached. This is the ring buffer optimization in action.

### Experiment 2: Dead Tuple Accumulation

```sql
-- Create table and run updates
CREATE TABLE bloat_test (id INT, val TEXT);
INSERT INTO bloat_test SELECT i, repeat('x', 100) FROM generate_series(1,10000) i;

-- Measure table size before
SELECT pg_size_pretty(pg_total_relation_size('bloat_test'));

-- Run many updates (creates dead tuples)
UPDATE bloat_test SET val = repeat('y', 100);
UPDATE bloat_test SET val = repeat('z', 100);

-- Measure after (table is now 3x size)
SELECT pg_size_pretty(pg_total_relation_size('bloat_test'));

-- Check dead tuples
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname = 'bloat_test';

-- Run vacuum and re-check
VACUUM bloat_test;
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname = 'bloat_test';
```

**Observation**: Dead tuples accumulate proportional to update count. VACUUM reclaims them (marking pages as reusable) but doesn't shrink file size (that requires VACUUM FULL).

### Experiment 3: WAL Activity Monitoring

```sql
SELECT pg_current_wal_lsn();
-- Run a burst of inserts
INSERT INTO orders SELECT i, i%1000+1, random()*100 FROM generate_series(1,100000) i;
SELECT pg_current_wal_lsn();
SELECT pg_size_pretty(pg_wal_lsn_diff(pg_current_wal_lsn(), '<previous_lsn>'::pg_lsn));
```

**Observation**: 100,000 inserts generate several MB of WAL data. Each insert generates approximately: WAL record header + heap insert record ≈ 80–150 bytes per row.

---

## 6. Key Learnings

### Architectural Lessons

1. **Shared memory is the PostgreSQL coordination backbone**: All backends communicate through shared buffers, lock tables, and WAL buffers. Understanding what lives in shared memory vs per-process memory explains most of PostgreSQL's concurrency model.

2. **MVCC is not free — it creates a maintenance workload**: The elegance of readers-never-block-writers comes at the operational cost of VACUUM. In production systems, improperly tuned autovacuum is one of the most common sources of PostgreSQL performance problems (table bloat, XID wraparound, stale statistics).

3. **WAL is more than durability**: WAL is also the mechanism for streaming replication, logical decoding (CDC), and point-in-time recovery. The WAL design choice — sequential writes to a log — is also why PostgreSQL is efficient on spinning disks: sequential I/O is always faster than random I/O.

4. **Statistics are part of the query engine**: The planner is only as good as `pg_statistic`. Running ANALYZE after large data loads is not optional — it's part of the query processing contract.

5. **The Lehman-Yao B-Tree is a remarkable engineering achievement**: Allowing concurrent B-Tree modifications without holding locks on the entire path from root to leaf — using only rightlink pointers and the high-key invariant — is a beautiful example of designing around lock contention through careful data structure reasoning.

### Surprising Observations

- PostgreSQL's buffer manager can hold at most `shared_buffers` pages in memory. With the default of 128MB and 8KB pages, this is only 16,384 pages. For a 10GB table, this is less than 0.1% of the data. PostgreSQL relies heavily on the **OS page cache** as a second-level cache — the OS also caches recently read files.
- The `correlation` statistic in `pg_stats` can explain why the planner chooses a sequential scan over an index scan even with high selectivity. An index with low correlation causes random disk I/O that's worse than a sequential scan.
- PostgreSQL's WAL-based crash recovery can replay from checkpoint in **seconds**, even for databases that had gigabytes of changes in flight. The WAL's sequential, append-only structure is why: replaying sequential writes is extremely fast.

---

*References:*
- *PostgreSQL Source: `src/backend/storage/buffer/bufmgr.c`*
- *PostgreSQL Source: `src/backend/access/nbtree/nbtinsert.c`, `nbtsearch.c`*
- *PostgreSQL Source: `src/backend/access/heap/heapam.c`*
- *Lehman, P., Yao, S.B. "Efficient Locking for Concurrent Operations on B-Trees" (1981)*
- *PostgreSQL Documentation: Chapters 68–73 (Internals)*
- *The Internals of PostgreSQL — Hironobu Suzuki (https://www.interdb.jp/pg/)*
