# PostgreSQL Internal Architecture

## 1. Problem Background

PostgreSQL was designed to solve a fundamental challenge: how do you build a database system that is simultaneously correct, concurrent, durable, and extensible — without sacrificing any of these properties under production workloads?

The original POSTGRES project (Stonebraker, 1986) was motivated by the inadequacy of existing systems in handling complex objects, procedural language integration, and proper ACID semantics across concurrent users. The key engineering insight was that **correctness and performance are not in opposition** — you can have strong guarantees AND high throughput, but you need clever internal machinery to achieve both.

The four pillars of that machinery are:
- **Buffer Manager**: control how pages move between disk and RAM
- **MVCC**: allow readers and writers to coexist without blocking
- **WAL**: guarantee durability without synchronous disk writes on every operation
- **Query Planner**: choose the cheapest execution strategy using collected statistics

Understanding PostgreSQL internals means understanding how these four systems interact.

---

## 2. Architecture Overview

```
┌──────────────────────────────────────────────────────────────────────┐
│                     PostgreSQL Process Architecture                  │
│                                                                      │
│  Client  ──TCP──►  Postmaster  ──fork──►  Backend Process           │
│                                              │                       │
│                                    ┌─────────▼──────────────────┐   │
│                                    │     Query Pipeline         │   │
│                                    │  Parser → Analyzer →       │   │
│                                    │  Rewriter → Planner →      │   │
│                                    │  Executor                  │   │
│                                    └─────────┬──────────────────┘   │
│                                              │                       │
│            ┌─────────────────────────────────▼──────────────────┐   │
│            │              Shared Memory                         │   │
│            │  ┌──────────────────┐  ┌──────────────────────┐   │   │
│            │  │  Shared Buffers  │  │  WAL Buffers         │   │   │
│            │  │  (buffer pool)   │  │  (wal_buffers)       │   │   │
│            │  └────────┬─────────┘  └──────────┬───────────┘   │   │
│            └───────────┼───────────────────────┼───────────────┘   │
│                        │                       │                    │
│            ┌───────────▼───────────────────────▼───────────────┐   │
│            │                  Storage                          │   │
│            │  base/           pg_wal/         pg_xact/         │   │
│            │  (heap+indexes)  (WAL segments)  (commit log)     │   │
│            └───────────────────────────────────────────────────┘   │
│                                                                      │
│  Background Workers:                                                 │
│    WAL Writer │ Checkpointer │ Autovacuum │ Stats Collector          │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 3. Internal Design

### 3.1 Buffer Manager

**Source**: `src/backend/storage/buffer/`

The Buffer Manager is the memory layer between the executor and disk. Its job: keep frequently-accessed pages in RAM, minimize disk I/O, and coordinate access across multiple backend processes.

#### Shared Buffers

All backend processes share a single buffer pool in shared memory (`shared_buffers`, default 128MB). Each slot in the pool holds exactly one 8KB page.

```
Shared Buffer Pool Structure:
┌──────────────────────────────────────────────────────────┐
│ Buffer Descriptor Array (one entry per buffer slot)      │
│                                                          │
│  [0]: { tag: (rel=16385, fork=0, block=42),              │
│          refcount: 2,                                    │
│          usage_count: 3,                                 │
│          flags: BM_DIRTY | BM_VALID,                     │
│          buf_id: 0 }                                     │
│  [1]: { tag: (rel=16386, fork=0, block=7), ... }         │
│  ...                                                     │
│  [N]: { ... }                                            │
│                                                          │
├──────────────────────────────────────────────────────────│
│ Buffer Hash Table                                        │
│   (rel_oid, fork, block_num) → buffer_slot_index         │
│                                                          │
├──────────────────────────────────────────────────────────│
│ Actual Page Data (N × 8KB blocks)                        │
│  Slot 0: [raw bytes of block 42 of relation 16385]       │
│  Slot 1: [raw bytes of block 7 of relation 16386]        │
│  ...                                                     │
└──────────────────────────────────────────────────────────┘
```

#### Buffer Replacement: Clock-Sweep

PostgreSQL uses a **clock-sweep** algorithm (an approximation of LRU) rather than true LRU:

```
Clock Sweep Algorithm:
──────────────────────
hand → scans buffer descriptors in a circular fashion

For each buffer:
  if usage_count > 0:
      decrement usage_count
      move hand forward
  else (usage_count == 0):
      if not pinned:
          EVICT this buffer (write to disk if dirty)
          return slot for new page

When a buffer is accessed: usage_count is incremented (up to max 5)

Why not true LRU?
  - LRU requires a lock on a global linked list for every access
  - Clock-sweep is O(1) average case, much lower contention
  - Good enough approximation for database workloads
```

#### Page Reads and Writes

```
Read Path:
  1. Hash lookup: is (rel, fork, block) in buffer pool?
  2. HIT: pin buffer, increment refcount, return pointer
  3. MISS:
     a. Allocate a slot (evict if needed, write dirty page)
     b. smgrread() — read from OS file cache or disk
     c. Verify checksum if data_checksums=on
     d. Insert into hash table, pin buffer, return

Write Path (dirty page):
  1. Modify buffer in shared memory (page is now dirty)
  2. WAL record written FIRST (write-ahead guarantee)
  3. Background: bgwriter or checkpointer eventually flushes
     dirty pages to disk via smgrwrite()
```

**Key insight**: PostgreSQL never writes a dirty page to disk before its WAL record is written. This is the WAL protocol — it's what makes crash recovery possible.

---

### 3.2 B-Tree Implementation (nbtree)

**Source**: `src/backend/access/nbtree/`

PostgreSQL's B-tree implementation supports equality and range queries on any ordered data type. It's used for primary keys, unique constraints, and general indexes.

#### Index Structure

```
B-Tree (height 3, simplified):
                    ┌─────────────┐
                    │ ROOT (lvl 2) │
                    │  [50 | 100] │
                    └──┬──────┬───┘
                       │      │
           ┌───────────┘      └───────────────┐
           ▼                                  ▼
    ┌─────────────┐                    ┌─────────────┐
    │ INTERNAL(1) │                    │ INTERNAL(1) │
    │  [20 | 35]  │                    │ [65 | 80]  │
    └──┬───────┬──┘                    └──┬──────┬───┘
       │       │                          │      │
  ┌────┘   ┌───┘                     ┌────┘  ┌───┘
  ▼        ▼                         ▼       ▼
[LEAF]  [LEAF] ←──────────────────► [LEAF] [LEAF]
 prev=0  prev=←                      ←     next=0

Leaf pages are doubly-linked for range scans!
```

#### Leaf Page Layout

```
nbtree Leaf Page:
┌───────────────────────────────────────────────┐
│ BTPageOpaqueData                              │
│   btpo_prev: page_no of left sibling          │
│   btpo_next: page_no of right sibling         │
│   btpo_level: 0 (leaf)                        │
│   btpo_flags: BTP_LEAF                        │
├───────────────────────────────────────────────│
│ IndexTuple array:                             │
│  [key=15, TID=(page=4, slot=2)]               │
│  [key=17, TID=(page=4, slot=5)]               │
│  [key=19, TID=(page=7, slot=1)]               │
│  ...                                          │
└───────────────────────────────────────────────┘
TID = (heap block number, item slot within block)
```

#### Page Splits

When a leaf page fills up, a **page split** occurs:

```
Page Split Process:
1. Determine split point (typically 50/50 for random inserts,
   90/10 for sequential inserts — optimization!)
2. Allocate new right page
3. Copy right half of tuples to new page
4. Update parent: insert new high key + pointer to right page
5. If parent is also full → split propagates upward
6. Root split → new root is allocated (tree grows taller)

All split steps are WAL-logged atomically.
```

---

### 3.3 MVCC (Multi-Version Concurrency Control)

MVCC is the heart of PostgreSQL's concurrency model. It allows multiple transactions to read and write simultaneously without blocking each other.

#### Heap Tuple Versioning

Every heap tuple carries two transaction IDs:

```
HeapTupleHeader (23 bytes):
┌─────────────────────────────────────────────┐
│  t_xmin   (4 bytes) - inserting transaction │
│  t_xmax   (4 bytes) - deleting transaction  │
│  t_ctid   (6 bytes) - current tuple TID     │
│  t_infomask (2 bytes) - flags               │
│  t_hoff   (1 byte)  - header size           │
│  ...                                        │
└─────────────────────────────────────────────┘
```

- `xmin`: the transaction that created this tuple version
- `xmax`: the transaction that deleted/updated this tuple (0 = still live)
- `ctid`: for updated tuples, points to the newer version

#### MVCC Operation Example

```
Initial state: row (id=1, name="Alice")
INSERT by txn 100:
  Tuple: xmin=100, xmax=0, data="Alice"

UPDATE by txn 200 (sets name="Bob"):
  Old tuple: xmin=100, xmax=200, data="Alice"  ← dead for txn>200
  New tuple: xmin=200, xmax=0,   data="Bob"    ← live

DELETE by txn 300:
  Tuple: xmin=200, xmax=300, data="Bob"        ← dead for txn>300

Visibility Rule for snapshot at xid=S:
  A tuple version (xmin, xmax) is visible if:
    xmin < S   AND (committed before snapshot)
    xmax = 0   OR  xmax >= S (not yet deleted in this snapshot)
```

#### Why VACUUM Is Necessary

```
Problem: Dead tuples accumulate over time

After 1000 UPDATEs to the same row:
  Disk holds 1001 tuple versions in the heap!
  Only the latest is "live" for current transactions.
  Storage grows; sequential scans slow down.

VACUUM solution:
  Scans heap pages
  Identifies tuples where xmax < oldest_active_xid
    (dead to ALL current transactions)
  Removes them from line pointer array
  Updates Free Space Map
  Updates Visibility Map (for index-only scans)
  Does NOT return space to OS (unless VACUUM FULL)

VACUUM FULL:
  Rewrites entire table without dead tuples
  Requires table-level lock — avoid in production!
  Use pg_repack extension instead for online compaction.
```

---

### 3.4 WAL (Write-Ahead Logging)

**Source**: `src/backend/access/transam/xlog.c`

WAL is PostgreSQL's mechanism for guaranteeing durability and enabling crash recovery. The protocol: **a page modification must be logged to WAL before the modified page is written to disk**.

#### WAL Record Structure

```
WAL Segment File (16MB default):
┌──────────────────────────────────────────────┐
│ XLogRecord 1                                 │
│   xl_tot_len, xl_xid, xl_prev_lsn            │
│   xl_info (record type), xl_rmid (resource   │
│   manager: Heap, Index, Transaction, ...)    │
│   crc32 checksum                             │
│   data blocks (full page or delta)           │
├──────────────────────────────────────────────│
│ XLogRecord 2                                 │
│   ...                                        │
└──────────────────────────────────────────────┘

LSN (Log Sequence Number) = byte offset in WAL stream
  Every WAL record has a unique, monotonically increasing LSN
  Every heap/index page stores the LSN of its last WAL record
```

#### Durability Guarantee

```
Transaction COMMIT sequence:
1. All changes already logged in WAL buffers (in memory)
2. COMMIT record written to WAL buffers
3. WAL buffers flushed to disk (fsync up to commit LSN)
4. Client receives "COMMIT" acknowledgment
5. (Later) dirty heap/index pages flushed by checkpointer

Crash Recovery:
1. At startup, find last checkpoint record in WAL
2. Replay all WAL records from checkpoint forward
3. REDO: re-apply modifications from WAL to data pages
4. Result: database is in consistent committed state
5. UNDO: not needed (PostgreSQL uses MVCC, old versions just become invisible)
```

#### Checkpointing

```
Checkpoint writes all dirty buffer pages to disk:
┌─────────────────────────────────────────────────────┐
│  1. Write CHECKPOINT_REDO record to WAL              │
│  2. Write all dirty shared buffers to disk           │
│  3. Write CHECKPOINT record with checkpoint LSN      │
│  4. Update pg_control with new checkpoint location   │
└─────────────────────────────────────────────────────┘

Effect: After checkpoint, crash recovery only needs to
replay WAL from the checkpoint LSN forward — not from
the beginning of all WAL files.

Triggered by:
  - checkpoint_timeout (default 5 min)
  - max_wal_size threshold
  - Manual: SELECT pg_checkpoint();
```

---

## 4. Design Trade-Offs

### Buffer Manager Trade-offs

| Decision | Rationale | Cost |
|---|---|---|
| Clock-sweep over LRU | Lower lock contention, O(1) | Slightly less optimal eviction |
| Shared memory pool | All backends share cache | Requires OS huge pages for large pools |
| 8KB page size | Matches OS page, good locality | Large rows span multiple pages |
| bgwriter separation | Background flushing avoids user-visible I/O | Extra process, tuning complexity |

### MVCC Trade-offs

| Decision | Rationale | Cost |
|---|---|---|
| Append-only heap | No in-place updates → no update locks | Table bloat, VACUUM needed |
| Tuple versioning in heap | Simple, no separate undo log | Dead tuples consume space |
| Snapshot isolation | Strong consistency for reads | Serialization anomalies still possible at lower isolation levels |
| xmin/xmax in tuple | Fast visibility check | 8 extra bytes per tuple |

### WAL Trade-offs

| Decision | Rationale | Cost |
|---|---|---|
| Full-page writes | Prevent torn page writes | Amplifies WAL volume after checkpoint |
| Record-level WAL | Enables PITR, streaming replication | WAL parsing complexity |
| synchronous_commit=on | Guaranteed durability | Commit latency (disk I/O per commit) |
| synchronous_commit=off | Lower latency | Risk of losing last few transactions on crash |

---

## 5. Experiments / Observations

### Experiment 1: EXPLAIN ANALYZE on Multi-Table Join

```sql
-- Setup
CREATE TABLE orders (id SERIAL, customer_id INT, amount DECIMAL, created_at TIMESTAMP);
CREATE TABLE customers (id SERIAL, name TEXT, city TEXT);
CREATE INDEX ON orders(customer_id);
INSERT INTO customers SELECT i, 'Customer '||i, 'City '||(i%10) FROM generate_series(1,10000) i;
INSERT INTO orders SELECT i, (random()*9999)::int+1, random()*1000, now() - (random()*365 || ' days')::interval FROM generate_series(1,1000000) i;
ANALYZE;

-- Query
EXPLAIN (ANALYZE, BUFFERS, FORMAT TEXT)
SELECT c.city, SUM(o.amount), COUNT(*)
FROM orders o
JOIN customers c ON o.customer_id = c.id
WHERE o.created_at > NOW() - INTERVAL '30 days'
GROUP BY c.city
ORDER BY SUM(o.amount) DESC;
```

**Sample Output and Analysis:**

```
QUERY PLAN
────────────────────────────────────────────────────────────────────
Sort (cost=28543.12..28543.22 rows=10) (actual time=342.1..342.2 ms)
  Sort Key: (sum(o.amount)) DESC
  ->  HashAggregate (cost=28542.50..28542.87 rows=10) (actual 341.8 ms)
        ->  Hash Join  (cost=308.50..25432.10 rows=82000)  (actual 340.1 ms)
              Hash Cond: (o.customer_id = c.id)
              Buffers: shared hit=8432 read=3201
              ->  Seq Scan on orders o  (cost=0..18832.00 rows=82000)
                    Filter: (created_at > (now() - '30 days'::interval))
                    Rows Removed by Filter: 917742
                    Buffers: shared hit=6121 read=3201
              ->  Hash  (cost=183.00..183.00 rows=10000)
                    ->  Seq Scan on customers c  (cost=0..183.00)
                          Buffers: shared hit=83
Planning Time: 1.8 ms
Execution Time: 342.5 ms
```

**Analysis:**

| Observation | Interpretation |
|---|---|
| Hash Join (not Nested Loop) | Planner estimated 82K matching rows — hash join is O(N+M), better than nested loop O(N×M) |
| Seq Scan on orders | No index on `created_at`; planner chose sequential scan since filter removes 92% (selective but covers large fraction) |
| `shared hit=8432 read=3201` | 8432 pages from buffer cache, 3201 from disk — buffer pool partially warmed |
| 82K rows estimate vs actual | Planner used column statistics from `pg_statistic` — within 20% accuracy |
| Hash on customers | Smaller table (10K rows, 83 pages) built as hash table in memory |

**Adding an index on `created_at` changes the plan:**

```sql
CREATE INDEX ON orders(created_at);
-- Now planner uses Index Scan + Bitmap Heap Scan:

Bitmap Heap Scan on orders (cost=1532..8743 rows=82000)
  Recheck Cond: (created_at > ...)
  ->  Bitmap Index Scan on orders_created_at_idx
        Index Cond: (created_at > ...)
-- Execution: 89ms (vs 342ms) — 3.8× speedup for range-filtered join
```

### Experiment 2: Observing MVCC Dead Tuples

```sql
-- Create table and update rows repeatedly
CREATE TABLE mvcc_test (id INT, val INT);
INSERT INTO mvcc_test SELECT i, 0 FROM generate_series(1,100000) i;

-- Check dead tuples BEFORE updates
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables
WHERE relname = 'mvcc_test';
-- n_live_tup: 100000, n_dead_tup: 0

-- Run updates (creates dead tuples)
UPDATE mvcc_test SET val = val + 1;
UPDATE mvcc_test SET val = val + 1;
UPDATE mvcc_test SET val = val + 1;

-- Check dead tuples AFTER updates
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables
WHERE relname = 'mvcc_test';
-- n_live_tup: 100000, n_dead_tup: ~200000-300000

-- Run VACUUM and verify
VACUUM ANALYZE mvcc_test;
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables
WHERE relname = 'mvcc_test';
-- n_dead_tup: 0 (reclaimed!)
```

**Observation**: Each UPDATE creates a dead tuple (the old version). Without VACUUM, dead tuples accumulate causing table bloat and slower sequential scans.

### Experiment 3: pg_statistic and Planner Accuracy

```sql
-- View statistics PostgreSQL has collected
SELECT attname, n_distinct, correlation
FROM pg_stats
WHERE tablename = 'orders';

-- n_distinct: -0.0001 (near-unique, negative = fraction of table)
-- correlation: 0.98 (physical order matches logical order — heap scan is efficient)

-- View histogram buckets for amount column
SELECT histogram_bounds FROM pg_stats
WHERE tablename = 'orders' AND attname = 'amount';
-- Returns 100 buckets showing value distribution
-- Planner uses these to estimate rows matching WHERE amount BETWEEN x AND y
```

**Key insight**: `ANALYZE` populates `pg_statistic`. Without fresh statistics, the planner makes poor estimates, choosing wrong join strategies and resulting in slow queries.

---

## 6. Key Learnings

1. **The buffer manager is the performance core**: Almost every PostgreSQL performance tuning tip (shared_buffers, effective_cache_size, checkpoint_completion_target) relates to managing how pages flow through the buffer pool.

2. **MVCC elegance comes at a storage cost**: The insight that "never overwrite, always append" enables lock-free reads and crash recovery simplicity — but the consequence (dead tuple accumulation) is a hidden cost that requires operational management through VACUUM.

3. **WAL is the backbone of reliability**: Everything PostgreSQL can do reliably — crash recovery, point-in-time recovery, streaming replication — flows from WAL. The ordering guarantee (log before flush) is what makes all of it possible.

4. **The query planner is statistical**: PostgreSQL does not parse SQL and emit a fixed execution plan. It collects statistics, builds a cost model, and explores a plan search space. Outdated statistics or unusual data distributions cause plan regressions — this is why `ANALYZE` matters.

5. **Index-only scans depend on the Visibility Map**: When all needed columns are in an index, PostgreSQL can skip the heap entirely — but only for pages marked as "all-visible" in the VM. VACUUM maintains the VM; without regular vacuuming, index-only scans degrade to index + heap scans.

6. **B-tree page splits are why sequential inserts need special handling**: Random inserts cause 50/50 splits (wasted space); sequential inserts trigger 90/10 splits (fill-factor optimization). This is why UUID primary keys can cause more index bloat than SERIAL keys.

---

## References

- [PostgreSQL Source: buffer/](https://github.com/postgres/postgres/tree/master/src/backend/storage/buffer)
- [PostgreSQL Source: nbtree/](https://github.com/postgres/postgres/tree/master/src/backend/access/nbtree)
- [PostgreSQL Internals Book (Egor Rogov)](https://postgrespro.com/community/books/internals)
- [PostgreSQL MVCC Documentation](https://www.postgresql.org/docs/current/mvcc.html)
- [PostgreSQL WAL Documentation](https://www.postgresql.org/docs/current/wal.html)
- Momjian, B. "PostgreSQL Internals Through Pictures." (2001, regularly updated)