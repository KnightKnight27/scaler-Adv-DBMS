# PostgreSQL Internal Architecture

**Student:** Romit Raj Sahu | 24BCS10436

---

## 1. Problem Background

PostgreSQL's internal architecture solves a specific hard problem: how do you give many concurrent users the illusion that they each have exclusive, consistent access to the database, while actually letting all of them read and write simultaneously?

The answer involves four interlocking systems that are worth understanding deeply — not as isolated components, but as a coherent design where each exists because of constraints imposed by the others:

- The **Buffer Manager** exists because disk I/O is the bottleneck. Reading a page from disk takes ~1ms; reading it from RAM takes ~100ns. The database must manage which pages stay in memory.
- The **B-Tree implementation** exists because sequential scans are O(n). For large tables, finding a row by a non-rowid column requires an index that the database can traverse in O(log n).
- **MVCC** exists because locking-based concurrency (readers block writers, writers block readers) is too slow for real workloads. MVCC gives readers a consistent snapshot without blocking writers.
- **WAL** exists because pages in the buffer pool are lost on a crash. WAL guarantees that committed transactions survive power failures without requiring a full fsync of every dirty page.

---

## 2. Architecture Overview

```
Client Query
     │
     ▼
┌─────────────────────────────────────────────────────────┐
│  Backend Process (one per connection)                   │
│                                                         │
│  Parser → Analyzer → Rewriter → Planner → Executor     │
│                                    │                    │
│                             Uses statistics             │
│                             (pg_statistic)              │
│                                    │                    │
│                                    ▼                    │
│                            Executor calls               │
│                            Access Methods               │
│                            (heap scan, index scan)      │
│                                    │                    │
└────────────────────────────────────┼────────────────────┘
                                     │
                                     ▼
┌─────────────────────────────────────────────────────────┐
│  Shared Memory                                          │
│                                                         │
│  ┌──────────────────────┐  ┌───────────────────────┐   │
│  │   shared_buffers     │  │   WAL Buffers         │   │
│  │   (8KB pages)        │  │   (wal_buffers)       │   │
│  │   BufferDescriptors  │  └───────────────────────┘   │
│  │   BufferTable (hash) │                               │
│  └──────────────────────┘  ┌───────────────────────┐   │
│                             │   Lock Table          │   │
│  ┌──────────────────────┐  │   ProcArray           │   │
│  │   Proc Array         │  │   CLOG (commit log)   │   │
│  └──────────────────────┘  └───────────────────────┘   │
└─────────────────────────────────────────────────────────┘
                                     │
                                     ▼
┌─────────────────────────────────────────────────────────┐
│  Storage Layer                                          │
│  base/<db_oid>/<rel_oid>       (heap files)             │
│  base/<db_oid>/<rel_oid>_fsm   (free space map)         │
│  base/<db_oid>/<rel_oid>_vm    (visibility map)         │
│  pg_wal/                       (WAL segment files)      │
└─────────────────────────────────────────────────────────┘
```

---

## 3. Internal Design

### 3.1 Buffer Manager

**Location in source:** `src/backend/storage/buffer/`

The buffer manager answers one question: when the executor needs page (filenode, block_number), is it already in shared_buffers? If yes, return a pointer. If no, find a free buffer, load the page, and return a pointer.

**Data structures:**

```
BufferTable: hash map
  key   = BufferTag { spc_oid, db_oid, rel_oid, fork_num, block_num }
  value = buffer_id (index into BufferDescriptors array)

BufferDescriptors: array[NBuffers]
  Each descriptor:
    tag         = which page is in this slot
    state       = { free, valid, dirty, ... }
    usage_count = 0–5 (for clock sweep)
    refcount    = how many backends have pinned this buffer

shared_buffers: raw page data array[NBuffers][8192 bytes]
```

**Page lookup:**

```
1. Hash BufferTag → look up BufferTable
2. If found: pin the buffer (increment refcount), return pointer
3. If not found:
   a. Run clock sweep to find a victim buffer
   b. If victim is dirty: write it to disk (smgrwrite)
   c. Read new page from disk into victim slot (smgrread)
   d. Update BufferTable with new tag
   e. Return pointer
```

**Clock sweep (eviction):**

Unlike LRU, PostgreSQL uses clock sweep because LRU requires moving elements in a list on every access, which is expensive under high concurrency. Clock sweep approximates LRU with a single pass:

```
clock hand → scans BufferDescriptors in circular order
for each buffer:
  if refcount > 0:       skip (pinned, cannot evict)
  if usage_count > 0:    usage_count-- and skip
  if usage_count == 0:   this is the victim → evict it
```

Each time a page is accessed, its `usage_count` is incremented (capped at 5). This means frequently accessed pages survive many passes of the clock hand. A page that was accessed once gets evicted after one full scan of the buffer pool. This approximates the behavior of LRU without the per-access linked list manipulation.

**Why not just use mmap?**

If PostgreSQL used mmap, the OS would manage page eviction. The problem: the OS does not know which pages are dirty versus which are cleanly flushed, it does not know the database's WAL ordering requirements, and it cannot guarantee that a page is only evicted after its WAL record has been flushed. Without controlling eviction order, WAL-based crash recovery breaks. The database must own the buffer pool.

---

### 3.2 B-Tree Implementation (nbtree)

**Location in source:** `src/backend/access/nbtree/`

PostgreSQL B-trees are B+-trees: all data is at the leaf level. Internal nodes contain only separator keys and child pointers.

**Page layout for a leaf page:**

```
┌────────────────────────────────────────────────┐
│ BTPageOpaque (special space, at pd_special)    │
│   btpo_prev: left sibling page number          │
│   btpo_next: right sibling page number         │
│   btpo_level: 0 for leaf, 1+ for internal      │
│   btpo_flags: BTP_LEAF, BTP_ROOT, etc.         │
├────────────────────────────────────────────────┤
│ PageHeader                                     │
│ ItemId array → IndexTuple for each entry       │
│                                                │
│ IndexTuple format:                             │
│   t_tid (ItemPointer): heap TID (page, slot)   │
│   t_info (length flags)                        │
│   key data (the indexed column value)          │
└────────────────────────────────────────────────┘
```

**High key:** Every non-rightmost leaf page has a "high key" stored as the first IndexTuple (with a special flag). The high key is the upper bound of all keys on this page. This is used during concurrent index scans: if a page split happens while you're scanning, you can detect that keys may have moved to the new right sibling by checking whether the key you're looking for exceeds the high key.

**Insert path:**

```
1. Descend B-tree from root to target leaf
   (at each level: binary search to find correct child)
2. Lock the target leaf page (exclusive)
3. Insert new IndexTuple in sorted position
4. If page is full (no space for new tuple):
   a. Split: allocate new page, move upper half of entries to new page
   b. New page becomes right sibling
   c. Insert separator key into parent (recursive — parent may also split)
   d. If root splits: new root is created, tree height increases by 1
5. Unlock
```

**Sequential insert optimization:**

When inserting keys in ascending order (common for auto-increment primary keys), PostgreSQL keeps a "fast path" — a cached pointer to the rightmost leaf. Instead of descending from root on every insert, it checks if the new key goes to the rightmost page, acquires only that page's lock, and inserts. This turns O(log n) into effectively O(1) amortized for bulk sequential inserts.

---

### 3.3 MVCC

**The problem MVCC solves:**

Without MVCC: a reader needs to acquire a shared lock on every row it reads. A writer needs exclusive locks. Readers and writers block each other. Under any real workload, this creates severe contention.

**PostgreSQL's solution:** Never lock rows for reads. Instead, keep multiple versions of each row. A reader gets a snapshot at transaction start and reads only the version visible to that snapshot.

**Tuple versioning:**

Every heap tuple has a header:
```
xmin  — transaction ID that inserted this tuple
xmax  — transaction ID that deleted/updated this tuple (0 = live)
ctid  — pointer to the newest version of this row (self if current)
```

When a row is updated:
```
Old version:  xmin=100, xmax=205, ctid → new version
New version:  xmin=205, xmax=0,   ctid → self
```

The old version is not immediately removed. It stays in the heap until VACUUM reclaims it.

**Snapshot and visibility:**

At the start of each transaction, PostgreSQL takes a snapshot containing:
- `xmin`: the lowest active transaction ID
- `xmax`: the next transaction ID to be assigned
- `xip[]`: list of transaction IDs that were in-progress at snapshot time

A tuple is visible to a snapshot if:
```
xmin is committed AND xmin < snapshot.xmax AND xmin NOT in xip[]
AND (xmax = 0
     OR xmax is not committed
     OR xmax > snapshot.xmax
     OR xmax in xip[])
```

This means: the inserting transaction committed before my snapshot was taken, AND the deleting transaction either hasn't committed or committed after my snapshot.

**Why VACUUM is necessary:**

Dead tuple versions accumulate in the heap. A heap page might have 10 live rows and 40 dead versions of those rows that have been overwritten. Without cleanup:
- Pages fill up faster (writes need space)
- Sequential scans are slower (they visit dead tuples)
- Index bloat grows (indexes point to dead tuple versions)
- **Transaction ID wraparound**: PostgreSQL uses 32-bit transaction IDs. After ~2 billion transactions, the counter wraps. Old xmin values would become "in the future" from the new counter's perspective, making all old rows invisible. VACUUM FREEZE prevents this by replacing old xmin values with a special FrozenTransactionId that is always considered "in the past".

---

### 3.4 WAL (Write-Ahead Logging)

**The durability problem:**

When the executor modifies a page in shared_buffers, the change is in RAM. The OS may crash before that dirty page is written to disk. The committed transaction's changes would be lost.

The naive solution — fsync every dirty page on every commit — is too slow. A single commit might touch many pages scattered across the heap and multiple indexes, requiring dozens of random writes.

**WAL's solution:**

Write a compact sequential log record first. Only when that log record is flushed to disk (sequential write) can we acknowledge commit. The dirty pages in shared_buffers get written lazily by the bgwriter and checkpointer.

```
On every page modification:
1. Construct WAL record describing the change (REDO information only)
2. Write WAL record to WAL buffer in shared memory
3. On commit: flush WAL buffer to disk (fsync of WAL segment file)
4. Dirty page in shared_buffers remains dirty — written lazily later

WAL record structure:
  xl_tot_len    total record length
  xl_xid        transaction ID
  xl_rmid       resource manager ID (heap, btree, etc.)
  xl_info       operation type within resource manager
  data          the actual change (enough to REDO from scratch)
```

**Why sequential writes are faster:**

A WAL record is written sequentially to the end of the current WAL segment file (default 16MB). This is one sequential write. The corresponding heap and index page writes would be random writes to locations scattered across the data directory. On spinning disks, sequential writes are ~100x faster than random writes. Even on SSDs, sequential writes have better write amplification characteristics.

**Crash recovery:**

```
PostgreSQL startup after crash:
1. Read last checkpoint record from pg_control
2. Open WAL at the checkpoint's WAL position
3. Replay all WAL records from checkpoint to end of WAL
4. For each record: apply the REDO operation to the appropriate page
5. Database is now in a consistent state
```

The checkpoint exists so that recovery does not replay from the beginning of time. The checkpointer periodically flushes all dirty buffers to disk and writes a checkpoint record to WAL. Recovery only needs to replay from the last checkpoint.

---

## 4. Design Trade-offs

**Buffer Manager: clock sweep vs LRU**

Clock sweep is O(1) amortized per eviction with no lock contention on the LRU list. True LRU requires moving elements in a doubly-linked list on every access, which needs a lock. Under thousands of concurrent backend processes, that single LRU lock would become a bottleneck. Clock sweep is a good approximation of LRU at much lower cost.

**MVCC: no read locks, but VACUUM overhead**

The inability to block reads with row locks is one of PostgreSQL's most important performance properties. A long-running OLAP query never blocks short OLTP writes. But dead tuples must be cleaned up. VACUUM is a background process that runs continuously in well-configured systems, but autovacuum can fall behind in very write-heavy workloads, leading to table bloat and eventually transaction ID wraparound emergencies.

**WAL: durability without per-commit random I/O**

WAL enables commit acknowledgment with a single sequential fsync. The cost is the WAL write itself (adds latency) and the recovery time (must replay WAL from the last checkpoint). Longer checkpoint intervals mean faster normal operation (fewer dirty page writes) but longer recovery time after a crash.

**B-Tree page splits: correctness vs concurrency**

PostgreSQL uses a "right-sibling" page split protocol that allows concurrent readers to navigate splits without locking the entire tree. When a page splits, the new right sibling is linked before the separator key is inserted into the parent. Readers detect they've hit a split by checking whether their target key exceeds the page's high key. This is the Lehman-Yao protocol and allows lock coupling (only two adjacent levels locked at once during a descent) instead of holding a root-to-leaf lock chain.

---

## 5. Experiments and Observations

### EXPLAIN ANALYZE on a multi-table join

Query:
```sql
EXPLAIN ANALYZE
SELECT c.company_name, COUNT(o.order_id) as order_count, SUM(od.unit_price * od.quantity) as revenue
FROM customers c
JOIN orders o ON c.customer_id = o.customer_id
JOIN order_details od ON o.order_id = od.order_id
GROUP BY c.company_name
ORDER BY revenue DESC;
```

Representative output:
```
Sort  (cost=847.23..849.23 rows=800 width=48) (actual time=12.4..12.5 rows=89)
  Sort Key: (sum((od.unit_price * od.quantity))) DESC
  ->  HashAggregate  (cost=802.45..810.45 rows=800 width=48) (actual time=12.1..12.3 rows=89)
        ->  Hash Join  (cost=412.00..776.22 rows=2616 width=28) (actual time=1.8..11.4 rows=2155)
              Hash Cond: (o.customer_id = c.customer_id)
              ->  Hash Join  (cost=32.00..348.65 rows=2616 width=20) (actual time=0.3..7.8 rows=2155)
                    Hash Cond: (od.order_id = o.order_id)
                    ->  Seq Scan on order_details od  (cost=0..58.16 rows=2155 width=12)
                    ->  Hash on orders o  (cost=19.30..19.30 rows=830 width=12)
                          Buckets: 1024, Batches: 1
              ->  Hash on customers c  (cost=22.00..22.00 rows=89 width=16)
```

**Analysis:**

The planner chose Hash Join over Nested Loop Join because both sides are large and unsorted — building a hash table and probing it is O(n+m) vs O(n*m) for nested loop. The planner knew the size of each table from `pg_statistic` (row count estimates) and made this choice accordingly.

The Seq Scan on `order_details` is correct — no WHERE clause filtering on that table, so a full scan is unavoidable and faster than a random-access index scan.

The "cost" numbers (802.45, etc.) are unit-free estimates based on `seq_page_cost` and `random_page_cost` parameters. The planner compares these estimates, not actual time. When estimates are wrong (stale statistics), the planner chooses suboptimal plans — this is why `ANALYZE` (update statistics) is important.

### Observing MVCC dead tuples

```sql
-- Create a table and update rows repeatedly
CREATE TABLE test (id int, val text);
INSERT INTO test SELECT generate_series(1,1000), 'original';
UPDATE test SET val = 'updated' WHERE id <= 500;

-- Check dead tuple count
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname = 'test';
-- n_live_tup: 1000, n_dead_tup: 500

-- After VACUUM
VACUUM test;
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname = 'test';
-- n_live_tup: 1000, n_dead_tup: 0
```

This directly demonstrates the MVCC lifecycle: UPDATE creates dead tuples, VACUUM reclaims them.

---

## 6. Key Learnings

**Buffer manager design is a concurrency problem as much as a caching problem.** The choice of clock sweep over LRU is not about cache hit rate — it is about avoiding a single hot lock on a linked list that thousands of backend processes would contend over. Database systems routinely trade a small amount of cache efficiency for much better lock scalability.

**MVCC's invisible cost is time, not space.** The space cost of dead tuples is visible in `pg_stat_user_tables`. What is less obvious is the time cost: tables with many dead tuples have slower sequential scans, larger indexes, and worse planner estimates (statistics are over all tuples, not just live ones). Systems that do heavy UPDATE workloads need autovacuum tuned aggressively.

**WAL is the foundation everything else sits on.** Without WAL, the buffer manager cannot safely keep dirty pages in memory. Without WAL, crash recovery is impossible without keeping every dirty page fsynced. Without WAL, point-in-time recovery and replication would not exist. WAL is not just a durability mechanism — it is the primitive that makes the entire lazy-write architecture safe.

**The query planner is only as good as its statistics.** The most elegant executor plan is useless if the planner estimates 10 rows when the actual count is 10,000. The `pg_statistic` table, populated by `ANALYZE`, is as important to query performance as any index. This is a subtle architectural lesson: the boundary between the physical storage layer and the logical query layer is the statistics system.
