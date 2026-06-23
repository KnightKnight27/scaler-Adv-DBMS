# PostgreSQL Internal Architecture

> **Author:** Pranay | Roll No: 24BCS10133  
> **Course:** Advanced DBMS — System Design Discussion

---

## 1. Problem Background

PostgreSQL's internal architecture answers a deceptively simple question: **how do you serve hundreds of clients reading and writing shared data simultaneously, without losing any committed data, and without making reads wait for writes?**

Each component of PostgreSQL's internals exists to answer one part of that question:
- **Buffer Manager** — how pages travel between disk and memory efficiently
- **B-Tree** — how indexed lookups find rows in O(log n) without scanning everything
- **MVCC** — how concurrent readers and writers coexist without blocking each other
- **WAL** — how PostgreSQL guarantees that committed data survives a crash

These four systems are deeply intertwined. Understanding each one in isolation misses how they cooperate to produce the behavior we observe.

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     PostgreSQL Internal Architecture                        │
│                                                                             │
│  Client Connection                                                          │
│       │                                                                     │
│       ▼                                                                     │
│  ┌─────────────┐                                                            │
│  │  Postmaster │  (listens on port 5432, forks backends)                   │
│  └──────┬──────┘                                                            │
│         │ fork()                                                            │
│         ▼                                                                   │
│  ┌─────────────────────────────────────────────────────────────┐           │
│  │                   Backend Process                           │           │
│  │  Parser → Rewriter → Planner → Executor                    │           │
│  │                                    │                        │           │
│  │                                    ▼                        │           │
│  │                           Buffer Manager ◄──────────────┐  │           │
│  │                                    │                    │  │           │
│  └────────────────────────────────────│────────────────────│──┘           │
│                                       │                    │               │
│  ┌────────────────────────────────────│────────────────────│──────────┐   │
│  │                    Shared Memory   │                    │          │   │
│  │                                   ▼                    │          │   │
│  │  ┌──────────────────────────────────────┐              │          │   │
│  │  │       Shared Buffer Pool             │              │          │   │
│  │  │  (shared_buffers: default 128MB)     │◄─────────────┘          │   │
│  │  │  8KB pages, LRU-based eviction       │    WAL Buffers          │   │
│  │  └──────────────────────────────────────┘    Lock Table           │   │
│  │                     │                        Proc Array           │   │
│  └─────────────────────│──────────────────────────────────────────────┘   │
│                         │                                                   │
│                         ▼                                                   │
│              ┌──────────────────┐                                           │
│              │   Disk Storage   │                                           │
│              │  Heap files      │                                           │
│              │  Index files     │                                           │
│              │  WAL (pg_wal/)   │                                           │
│              └──────────────────┘                                           │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Internal Design

### 3.1 Buffer Manager

The Buffer Manager is PostgreSQL's memory cache for disk pages. Its goal: minimize disk I/O by keeping frequently accessed pages in RAM.

#### Structure

```
shared_buffers region (e.g., 1GB = 131072 pages of 8KB each):
┌──────────────────────────────────────────────────────────────┐
│ Buffer Descriptor Array (one per buffer slot)                │
│  Each descriptor tracks:                                     │
│  - buf_id: which slot                                        │
│  - tag: (relfilenode, fork, blocknum) — which page this is   │
│  - state: pinned? dirty? valid?                              │
│  - usage_count: for Clock replacement (0-5)                  │
│  - content_lock: shared/exclusive lock on the page           │
│  - io_in_progress: another backend is reading this page      │
├──────────────────────────────────────────────────────────────┤
│ Buffer Pool (actual 8KB page data)                           │
│  Slot 0: [page data...]                                      │
│  Slot 1: [page data...]                                      │
│  ...                                                         │
│  Slot N: [page data...]                                      │
├──────────────────────────────────────────────────────────────┤
│ Buffer Hash Table                                            │
│  (relfilenode, fork, blocknum) → buffer slot number         │
│  Used to check: "is this page already in memory?"           │
└──────────────────────────────────────────────────────────────┘
```

#### Page Read Path

```
Backend needs page (rel=users, block=42):

1. Hash lookup: (users, main, 42) in buffer hash table
   → Found? Increment pin count, acquire content_lock → return buffer slot
   → Not found? Need to load from disk

2. Find a victim buffer (Clock Sweep algorithm):
   - Sweep clock hand through buffer descriptors
   - If usage_count > 0: decrement it, skip
   - If usage_count == 0 and not pinned: evict this slot
   
3. If victim is dirty:
   - Write dirty page to heap file (bgwriter normally does this proactively)
   - Clear dirty flag
   
4. Read page from disk into victim slot
5. Update hash table: (users, main, 42) → victim slot
6. Return slot to backend
```

**Why Clock Sweep and not LRU?** True LRU requires updating a global data structure on every access — too much contention. Clock Sweep is an approximation: `usage_count` acts as a "how recently was this used" signal. Pages accessed more frequently naturally have higher counts and survive longer. Good enough for production, much cheaper to implement.

#### bgwriter and checkpointer

Two background processes help the buffer manager:

- **bgwriter**: Proactively writes dirty buffers to disk during idle periods. Goal: ensure clean buffers are available so foreground processes don't have to wait for eviction writes.
- **checkpointer**: Periodically writes ALL dirty buffers to disk and records a checkpoint in WAL. Checkpoints bound crash recovery time — PostgreSQL only needs to replay WAL from the last checkpoint, not from the beginning.

### 3.2 B-Tree Implementation (nbtree)

PostgreSQL implements B+-Tree indexes, where all data lives in leaf nodes and interior nodes only contain separator keys for navigation.

#### Index File Structure

```
nbtree Index File:
┌────────────────────────────────────────────────────────────────┐
│ Metapage (block 0)                                             │
│  - root block number                                           │
│  - fast root block (optimization for shallow trees)           │
│  - level of the tree                                          │
├────────────────────────────────────────────────────────────────┤
│ Root Page (block 1 typically)                                  │
│  Interior node: [P0 | K1 | P1 | K2 | P2 | ... | Kn | Pn]     │
│  Pi = child page number, Ki = separator key                    │
├────────────────────────────────────────────────────────────────┤
│ Interior Pages...                                              │
├────────────────────────────────────────────────────────────────┤
│ Leaf Pages (doubly linked for range scans)                     │
│  Each entry: [key value | heap TID (page, offset)]             │
│  ← prev_page | next_page →  (for range scan traversal)        │
└────────────────────────────────────────────────────────────────┘
```

**Why B+-Tree and not B-Tree?** In a B-Tree, data can appear at any level (interior or leaf). In B+-Tree, all data is in leaves, and interior pages contain only keys. This means interior pages can hold more entries (no data payload), making the tree shorter and reducing I/Os per lookup. Range scans are also much more efficient — once you find the starting leaf, just follow the linked list of leaf pages.

#### Search Path

```sql
-- Query: SELECT * FROM users WHERE email = 'alice@example.com'
-- Assuming index on (email)
```

```
1. Read metapage → get root block number (block 1)
2. Read root page:
   - Binary search for key 'alice@example.com' among separator keys
   - Follow child pointer to next level
3. Repeat at each interior level (tree height = log_order(n_tuples))
4. Reach leaf page:
   - Binary search for exact key
   - Extract heap TID: (page=42, offset=7)
5. Heap fetch: read heap page 42, find tuple at offset 7
6. Check tuple visibility (xmin/xmax) — MVCC step
7. Return row if visible
```

**Total I/Os for a btree lookup:** tree height (typically 2-4 for large tables) + 1 heap fetch. For a table with 1 million rows, height ≈ 3. So 4 I/Os total, regardless of table size. Compare to sequential scan: up to 10,000+ I/Os.

#### Page Splits

When a leaf page is full and a new index entry must be inserted:

```
Before split:
Leaf Page 7 (FULL): [A | B | C | D | E | F]

After split:
Leaf Page 7:  [A | B | C]
Leaf Page 21: [D | E | F]
Parent gets new separator key 'D' pointing to page 21

If parent is also full → split propagates upward
If root splits → tree grows one level (new root created)
```

**Why not just move to a sibling?** Page splits are triggered only when the target page is full AND no sibling has space. In practice, PostgreSQL uses a fill factor (default 90%) — leaf pages are only 90% filled initially, leaving room for future inserts without immediate splitting. This is critical for write-heavy tables.

### 3.3 MVCC (Multiversion Concurrency Control)

MVCC is the answer to: "how do I let a reader see a consistent snapshot of the database while writers are actively modifying it?"

#### Heap Tuple Header

Every tuple (row) in the heap has:

```
HeapTupleHeader:
┌─────────────────────────────────────────────────────┐
│ t_xmin    (4 bytes) — txid that INSERTed this tuple │
│ t_xmax    (4 bytes) — txid that DELETEd/UPDATEd it │
│            (0 = still alive, not deleted)            │
│ t_ctid    (6 bytes) — TID of newer version (if any) │
│ t_infomask (2 bytes) — flags: null bitmap, hints    │
│ t_hoff    (1 byte)  — offset to actual data         │
├─────────────────────────────────────────────────────┤
│ Actual column data                                  │
└─────────────────────────────────────────────────────┘
```

#### Update Lifecycle

```
Initial state (row inserted by txid 100):
  Tuple v1: xmin=100, xmax=0, data="Alice"

Transaction 200 updates the row:
  Tuple v1: xmin=100, xmax=200, ctid→(page5, offset3)
  Tuple v2: xmin=200, xmax=0,   data="Alice Smith"
  (both tuples are on disk simultaneously)

Transaction 300 begins (after txid 200 committed):
  Snapshot: "see all committed transactions before 300"
  Reads: Tuple v1 (xmax=200, which committed) → INVISIBLE
         Tuple v2 (xmin=200, committed, xmax=0) → VISIBLE ✓

Transaction 150 (started before txid 200):
  Snapshot: "see all committed transactions before 150"
  Reads: Tuple v1 (xmin=100 committed, xmax=200 not yet committed when snapshot taken) → VISIBLE ✓
         Tuple v2 (xmin=200 not in snapshot) → INVISIBLE
```

**The critical insight:** A transaction's "snapshot" is determined at its start time (for REPEATABLE READ / SERIALIZABLE) or per-statement (for READ COMMITTED). Checking visibility is just arithmetic on transaction IDs — no locks acquired by readers.

#### Transaction ID Wraparound

PostgreSQL uses 32-bit transaction IDs. At ~2 billion transactions, IDs wrap around. PostgreSQL handles this via **transaction ID freezing** (VACUUM FREEZE): old tuples have their xmin replaced with a special "frozen" marker meaning "visible to all future transactions." Without periodic VACUUM FREEZE, a database would eventually corrupt (old tuples appear to be "from the future").

This is why **autovacuum is not optional** — it's a fundamental requirement of the MVCC architecture.

#### VACUUM: The MVCC Cleanup Crew

```
What VACUUM does:
1. Scan each heap page
2. For each tuple where xmax is set and the transaction committed:
   - Mark tuple as DEAD in the line pointer (LP_DEAD flag)
3. For dead tuples:
   - Reclaim space on the page
   - Remove corresponding index entries
4. Update Free Space Map (FSM) with reclaimed space
5. Optionally: freeze old xmin values (VACUUM FREEZE)

What autovacuum does:
- Runs VACUUM automatically when:
  (n_dead_tup > autovacuum_vacuum_threshold + autovacuum_vacuum_scale_factor * n_live_tup)
  (default: 50 + 0.2 * n_live_tup)
```

**Why is VACUUM so important?** Without VACUUM, dead tuples accumulate indefinitely, bloating tables. Index scans still find entries pointing to dead tuples, requiring heap fetches to discover they're dead — wasted I/O. And without FREEZE, transaction ID wraparound eventually corrupts the database.

### 3.4 WAL (Write-Ahead Logging)

WAL is PostgreSQL's durability mechanism. The rule: **before any change to a heap page takes effect, the corresponding WAL record must be written and flushed to disk.**

#### WAL Record Structure

```
WAL Record (variable length):
┌──────────────────────────────────────────────────────┐
│ XLogRecord Header                                    │
│  - xl_tot_len: total record length                   │
│  - xl_xid: transaction ID                           │
│  - xl_prev: LSN of previous record (chain)           │
│  - xl_info: record type flags                        │
│  - xl_rmid: resource manager (Heap, BTree, Xact...) │
│  - xl_crc: CRC32 checksum                            │
├──────────────────────────────────────────────────────┤
│ Block References (which pages were modified)         │
│  - relfilenode: which table/index file               │
│  - block number: which page                          │
│  - FPW: Full Page Write (on first modification after │
│          checkpoint — safety against partial writes)  │
├──────────────────────────────────────────────────────┤
│ Record-specific data                                 │
│  (e.g., for heap insert: the tuple data)             │
└──────────────────────────────────────────────────────┘
```

#### Full Page Writes (FPW)

The first time a page is modified after a checkpoint, PostgreSQL writes the entire 8KB page into the WAL record (not just the change). Why?

```
Scenario without FPW:
1. Checkpoint at LSN 1000
2. Page P is modified → WAL record at LSN 1100 (just the delta)
3. OS starts writing page P to disk (16KB OS page = 2x 8KB PostgreSQL pages)
4. CRASH — only half of the OS page is written
5. Page P on disk is now torn (corrupt)
6. Crash recovery tries to apply delta from LSN 1100 to corrupt page → wrong result
```

With FPW, the complete page is in WAL at LSN 1100. Crash recovery writes the complete page from WAL, then applies any subsequent deltas. The torn write doesn't matter.

FPW significantly increases WAL volume — `wal_compression = on` helps (compresses FPW images).

#### Checkpoint and Recovery

```
Normal operation:
Checkpoint ────── writes ────── Checkpoint ────── writes ──────►
    ↑                               ↑
  LSN_A                           LSN_B
  (flush all dirty pages,         (flush all dirty pages,
   record LSN_A in pg_control)     record LSN_B in pg_control)

Crash here ────────────────────────────────────────────── ✕ crash
                                           ↑
                                        LSN_C

Recovery:
1. Read pg_control → find last checkpoint at LSN_B
2. Scan WAL from LSN_B forward
3. Replay all WAL records → heap files reach state at LSN_C
4. Database consistent at point of crash
```

The time between checkpoints (`checkpoint_completion_target`, `max_wal_size`) directly controls recovery time. Longer intervals = less checkpoint I/O pressure but longer recovery after a crash.

---

## 4. Design Trade-Offs

### Buffer Manager Trade-Offs

| Choice | Reasoning | Trade-Off |
|---|---|---|
| Clock Sweep over LRU | Less contention on global structures | Not perfectly optimal; large sequential scans can pollute cache (PostgreSQL has "ring buffers" to mitigate this) |
| Shared buffer pool | All backends share one cache = efficient memory use | Requires locks on buffer descriptors; bottleneck at very high connection counts |
| Separate bgwriter | Spreads I/O over time, keeps foreground responsive | Still must write dirty pages at checkpoint regardless |

### B-Tree Trade-Offs

| Choice | Reasoning | Trade-Off |
|---|---|---|
| B+-Tree over hash index | Supports range queries, ORDER BY, BETWEEN | Slightly slower for pure equality lookups than hash indexes (O(log n) vs O(1)) |
| Fill factor 90% | Leaves room for inserts, fewer immediate splits | Wastes ~10% of index space; can be tuned down for append-only tables |
| Leaf page doubly linked list | Enables efficient range scans without backtracking to parent | Adds maintenance overhead during splits (update both neighbors) |
| Split 50/50 by default | Balanced; works for random inserts | Sequential inserts (auto-increment PKs) → always split at the end; PostgreSQL detects this and splits 90/10 for sequential patterns |

### MVCC Trade-Offs

| Choice | Reasoning | Trade-Off |
|---|---|---|
| Heap-based tuple versioning | Updates don't need to move index entries (ctid chain) | Dead tuple bloat; VACUUM required; HOT (Heap Only Tuple) optimization mitigates when no indexed column changes |
| Per-tuple visibility metadata | No central lock table needed for reads | 8 bytes per tuple overhead; tuple header is never smaller than ~23 bytes |
| Snapshot isolation default | No read anomalies for most applications | Write skew anomalies still possible at REPEATABLE READ; need SERIALIZABLE for true isolation |

### WAL Trade-Offs

| Choice | Reasoning | Trade-Off |
|---|---|---|
| WAL before heap writes | Guarantees durability without double-syncing heap | WAL is extra I/O; on commit, WAL must fsync (can set synchronous_commit=off to trade durability for speed) |
| Full Page Writes | Protects against torn writes | Dramatically increases WAL size on first modification after checkpoint |
| Logical replication (WAL level = logical) | Enables CDC, streaming replication | Even more WAL volume; must carefully manage WAL retention for replicas |

---

## 5. Experiments / Observations

### Experiment 1: EXPLAIN ANALYZE on Multi-Table Join

```sql
-- Setup
CREATE TABLE orders (id SERIAL PRIMARY KEY, user_id INT, amount NUMERIC, created_at TIMESTAMP);
CREATE TABLE users (id SERIAL PRIMARY KEY, email TEXT, country TEXT);
CREATE INDEX idx_orders_user ON orders(user_id);
CREATE INDEX idx_users_country ON users(country);

-- Insert 100k orders, 10k users
-- Then:
EXPLAIN (ANALYZE, BUFFERS, FORMAT TEXT)
SELECT u.email, SUM(o.amount) as total
FROM orders o
JOIN users u ON o.user_id = u.id
WHERE u.country = 'IN'
GROUP BY u.email
ORDER BY total DESC
LIMIT 10;
```

**Observed query plan (annotated):**

```
Limit (cost=8234.50..8234.53 rows=10 width=56) (actual time=124.3..124.3 rows=10)
  -> Sort (cost=8234.50..8236.75 rows=900 width=56) (actual time=124.2..124.2 rows=10)
     Sort Key: (sum(o.amount)) DESC
     Sort Method: top-N heapsort  Memory: 25kB
     -> HashAggregate (cost=8180.00..8189.00 rows=900) (actual time=123.8..123.9 rows=842)
                                              ↑ planner estimated 900, got 842
        Group Key: u.email
        -> Hash Join (cost=310.00..7955.00 rows=45000 width=48)
                                              ↑ planner estimated 45000 rows from IN users
           Hash Cond: (o.user_id = u.id)
           Buffers: shared hit=1842 read=203
                         ↑ 203 pages read from disk (not in buffer cache)
           -> Seq Scan on orders (cost=0.00..2100.00 rows=100000)
              Buffers: shared hit=834 read=166
           -> Hash (cost=287.50..287.50 rows=1800)
              -> Bitmap Heap Scan on users (cost=21.25..287.50 rows=1800)
                 Recheck Cond: (country = 'IN')
                 -> Bitmap Index Scan on idx_users_country
                    Index Cond: (country = 'IN')
                    Buffers: shared hit=5
```

**Analysis:**
- Planner chose **Hash Join** over Nested Loop — for large tables, hash joins amortize the lookup cost; Nested Loop would be 100k × index lookups into users
- `shared hit=1842 read=203` — most pages were in buffer cache (1842), only 203 disk reads
- Planner used `pg_statistic` (populated by ANALYZE) to estimate 1800 users from IN and ~45k matching orders. Actual was 842 users — the estimate was off because the statistics histogram didn't perfectly represent the 'IN' bucket
- `Bitmap Heap Scan` — PostgreSQL collected all matching TIDs from the index, sorted them, then fetched heap pages in physical order (reduces random I/O)

### Experiment 2: Observing MVCC Dead Tuples

```sql
-- Create table and insert rows
CREATE TABLE mvcc_test (id SERIAL, val TEXT);
INSERT INTO mvcc_test SELECT i, 'value_' || i FROM generate_series(1, 10000) i;

-- Check initial state
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname = 'mvcc_test';
-- Result: n_live_tup=10000, n_dead_tup=0

-- Now update all rows (creates 10000 dead tuples)
UPDATE mvcc_test SET val = val || '_updated';

-- Check again (before autovacuum runs)
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname = 'mvcc_test';
-- Result: n_live_tup=10000, n_dead_tup=10000

-- Table file is now ~2x size (both old and new tuples on disk)
SELECT pg_size_pretty(pg_total_relation_size('mvcc_test'));
-- Result: ~1.7MB (was ~860KB)

-- Run VACUUM
VACUUM mvcc_test;

-- After VACUUM:
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname = 'mvcc_test';
-- Result: n_live_tup=10000, n_dead_tup=0
-- Note: file size doesn't shrink (VACUUM reuses space; VACUUM FULL truncates)
```

### Experiment 3: WAL Generation Rate

```bash
# Check WAL LSN before and after bulk insert
psql -c "SELECT pg_current_wal_lsn();"
-- 0/5A3C000

psql -c "INSERT INTO large_table SELECT generate_series(1,1000000), md5(random()::text);"

psql -c "SELECT pg_current_wal_lsn();"  
-- 0/14F8C000

psql -c "SELECT pg_size_pretty('0/14F8C000'::pg_lsn - '0/5A3C000'::pg_lsn);"
-- Result: ~251 MB of WAL generated for 1M row insert
-- With wal_compression=on: ~78 MB (69% reduction)
```

**Observation:** WAL volume is significant. For write-heavy workloads, WAL I/O can be the bottleneck. `wal_compression`, SSDs, and separate WAL disk (when using spinning rust) are standard production configurations.

---

## 6. Key Learnings

**1. Every system component exists to serve another component's needs.**  
MVCC creates dead tuples → VACUUM removes them. WAL ensures durability → Checkpoints bound recovery time. Buffer Manager caches pages → bgwriter keeps clean pages available. Understanding PostgreSQL means understanding these dependencies.

**2. The Buffer Manager is the central hub.**  
Almost all data reads/writes flow through it. Every buffer manager decision — what to cache, when to evict, how to handle contention — directly impacts query performance. The most common PostgreSQL tuning advice ("increase shared_buffers") exists because the buffer manager is so central.

**3. MVCC shifts cost from reads to cleanup.**  
MVCC makes reads cheap (no locks, just xmin/xmax checks) by deferring cleanup. That deferred cleanup cost (VACUUM) must be paid eventually. Systems that neglect VACUUM eventually face table bloat, index bloat, and transaction ID wraparound — all symptoms of an under-maintained MVCC implementation.

**4. B-Tree splits are designed to be rare, not avoided.**  
PostgreSQL's fill factor and split detection (sequential vs random inserts) show that engineers don't try to eliminate splits — they minimize them and make them efficient when they happen. This is realistic engineering: optimize the common case, handle the edge case gracefully.

**5. WAL trades I/O for correctness.**  
The WAL guarantee (durability before acknowledgment) means every committed transaction requires at least one fsync. `synchronous_commit = off` breaks this guarantee in exchange for throughput — a deliberate, documented trade-off that some applications (logging, analytics) can accept.

**6. Statistics drive the planner, not intuition.**  
The query planner's decisions are entirely based on `pg_statistic` — histograms, most common values, null fractions. Stale statistics (no ANALYZE) cause bad plans. This is why `autovacuum` runs ANALYZE too: good statistics are as important as dead tuple cleanup.

---

## References

- PostgreSQL source: `src/backend/storage/buffer/`, `src/backend/access/nbtree/`
- PostgreSQL docs: https://www.postgresql.org/docs/current/
- "PostgreSQL 14 Internals" — Egor Rogov
- "The Internals of PostgreSQL" — Hironobu Suzuki (http://www.interdb.jp/pg/)
- PostgreSQL MVCC: https://www.postgresql.org/docs/current/mvcc-intro.html
