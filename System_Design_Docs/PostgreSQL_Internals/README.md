# PostgreSQL Internal Architecture

**Course:** Advanced Database Management Systems  
**Student:** Vivek Anand Singh | 23BCS10172

---

## 1. Problem Background

PostgreSQL was designed to answer a question that 1980s relational databases couldn't: *what happens when the data model needs to evolve — when you need user-defined types, complex queries, and transactional guarantees all at once?*

The INGRES project at UC Berkeley gave way to POSTGRES (1986) under Michael Stonebraker. The design goal was not just to store data but to build a system that could support arbitrary data types, rules, and complex integrity constraints — without sacrificing ACID guarantees. When POSTGRES was open-sourced and SQL support was added in the 1990s, it became PostgreSQL.

The internal architecture reflects this goal: every component — the buffer manager, MVCC, WAL, the query planner — is built to be correct under concurrent, long-running workloads with complex queries. Performance is a goal, but correctness is never sacrificed for it.

---

## 2. Architecture Overview

```
                        Client
                          │
                   TCP / Unix socket
                          │
                 ┌────────▼────────┐
                 │   Postmaster    │  ← listens, forks backends
                 └────────┬────────┘
                          │ fork() per connection
                 ┌────────▼────────────────────────┐
                 │       Backend Process            │
                 │                                  │
                 │  SQL Text                        │
                 │     │                            │
                 │  ┌──▼──────┐                     │
                 │  │ Parser  │  → Parse tree        │
                 │  └──┬──────┘                     │
                 │  ┌──▼──────┐                     │
                 │  │Rewriter │  → Applies rules     │
                 │  └──┬──────┘                     │
                 │  ┌──▼──────┐                     │
                 │  │ Planner │  → Query plan        │
                 │  └──┬──────┘  (uses pg_statistic) │
                 │  ┌──▼──────┐                     │
                 │  │Executor │  → Fetches tuples    │
                 │  └──┬──────┘                     │
                 └─────┼────────────────────────────┘
                       │ reads/writes pages
              ┌────────▼──────────────────┐
              │    Shared Buffers          │
              │  (shared across backends)  │
              │  Buffer Pool + LRU/Clock   │
              └────────┬──────────────────┘
                       │
              ┌────────▼──────────────────┐
              │    Storage Manager         │
              │  Heap files + Index files  │
              │  WAL (pg_wal/)             │
              └───────────────────────────┘
```

The process model is central: each client gets its own **backend process** (not a thread). They share only a fixed shared memory segment (shared buffers, WAL buffers, lock table, CLOG). This isolation means a crashing client cannot corrupt shared state, but it also means PostgreSQL consumes ~5–10 MB RAM per connection, which is why connection poolers (PgBouncer) are essential at scale.

---

## 3. Internal Design

### 3.1 Buffer Manager (`src/backend/storage/buffer/`)

The buffer manager is the memory management layer between the executor and the disk. Its job: keep frequently used pages in RAM, evict cold pages when the buffer pool fills.

**Shared buffers** is a fixed pool of 8 KB page frames (default 128 MB, typically set to 25% of RAM). Every backend reads and writes pages through this pool — there is no per-process page cache.

**ClockSweep replacement policy:**  
PostgreSQL does not use LRU. Instead it uses ClockSweep (implemented in `freelist.c`). Each buffer frame has a `usage_count` (0–5). A circular "clock hand" sweeps through frames:
- If `usage_count > 0`: decrement, skip
- If `usage_count == 0` and not pinned: evict this frame

When a page is accessed, its `usage_count` is incremented (capped at 5). Hot pages (frequently accessed) accumulate higher counts and survive more sweep rounds. This approximates LRU without the overhead of maintaining a sorted list — critical when thousands of operations per second touch the buffer pool.

**Why not LRU?**  
A doubly-linked list LRU requires a lock on every page access to reorder the list. Under high concurrency, this becomes a global bottleneck. ClockSweep accesses `usage_count` with a simple increment — far lower contention.

**Sequential scan protection:**  
A full table scan would pollute the entire buffer pool under LRU (every page in a large table gets used exactly once, evicting hot index pages). ClockSweep limits this damage: each scan page gets `usage_count = 1`, so the sweep quickly reclaims them without destroying the hot set.

### 3.2 B-Tree Implementation (`src/backend/access/nbtree/`)

PostgreSQL B-trees are the default index type. They are **balanced**, **sorted**, and support equality, range, and prefix queries efficiently.

**Page structure:**
```
B-Tree Leaf Page (8 KB)
┌────────────────────────┐
│ BTPageOpaqueData       │  ← btpo_prev, btpo_next (sibling links)
│  prev/next page links  │     btpo_level, btpo_flags
├────────────────────────┤
│ Item pointer array     │  ← sorted offsets to index tuples
├────────────────────────┤
│       free space       │
├────────────────────────┤
│ Index tuples           │  ← (key, heap TID) pairs
└────────────────────────┘
```

Leaf pages are linked as a **doubly-linked list** (btpo_prev / btpo_next). This is the key that makes range scans efficient: after finding the first matching key via tree traversal, the scan simply follows the sibling pointers without going back up the tree.

**Search path:**  
Root → internal pages (contain separator keys + child page pointers) → leaf page → heap TID → fetch actual tuple from heap file.

**Page splits:**  
When a leaf page fills up, PostgreSQL splits it at the median key. The right half becomes a new page; a separator key is inserted into the parent. If the parent is also full, splits cascade upward. This is the standard B-tree split protocol.

**Why B-tree and not hash indexes for everything?**  
Hash indexes support only equality (`=`) — they don't maintain order. B-trees support `=`, `<`, `>`, `BETWEEN`, `ORDER BY`, and prefix matches on composite keys. The sorted structure comes at the cost of O(log n) per operation vs O(1) average for hash, but the versatility makes B-trees the dominant choice.

### 3.3 MVCC — Multi-Version Concurrency Control

This is PostgreSQL's most distinctive internal mechanism.

**Heap tuple header (simplified):**
```
┌────────────────────────────────┐
│ t_xmin  (uint32)               │  ← xid that inserted this version
│ t_xmax  (uint32)               │  ← xid that deleted/updated (0 = live)
│ t_ctid  (ItemPointer)          │  ← points to newest version of this row
│ t_infomask                     │  ← commit status hints
│ ... column data ...            │
└────────────────────────────────┘
```

**How an UPDATE works:**
1. The old tuple's `t_xmax` is set to the current transaction's xid
2. A new tuple is inserted with `t_xmin` = current xid, `t_xmax` = 0
3. The old tuple's `t_ctid` is updated to point to the new tuple

No tuple is overwritten in place. The heap grows with new versions; old versions become "dead" once no active transaction can see them.

**Visibility rule:**  
A tuple version is visible to transaction T (with snapshot S) if:
- `t_xmin` is committed AND `t_xmin` started before S was taken
- AND either `t_xmax` is 0, OR `t_xmax` was not committed before S was taken

**CLOG (Commit Log):**  
PostgreSQL does not store commit status in the tuple itself at write time. Instead, `t_infomask` caches hints, and the authoritative commit/abort status lives in the CLOG (a bitfield in `pg_xact/`). This is checked during visibility tests when the hints aren't set yet.

**Why VACUUM is necessary:**  
Dead tuples are never cleaned by the writer — that would require the writer to know which transactions have since committed and moved past this version. `VACUUM` scans the heap, identifies tuples whose `t_xmax` is committed and older than the oldest running transaction's snapshot, and marks their space as free. Without VACUUM, tables grow without bound.

### 3.4 WAL — Write-Ahead Logging (`src/backend/access/transam/xlog.c`)

**The core invariant:** a change must be written to the WAL before it can be written to the data file. This is the "write-ahead" in WAL.

**WAL record structure:**
```
┌────────────────────────────────┐
│ xl_tot_len (record length)     │
│ xl_xid     (transaction id)    │
│ xl_prev    (previous LSN)      │
│ xl_rmid    (resource manager)  │
│ xl_info    (operation type)    │
├────────────────────────────────┤
│ Resource manager data          │
│  (e.g., heap tuple, B-tree op) │
└────────────────────────────────┘
```

**LSN (Log Sequence Number):** A monotonically increasing 64-bit offset into the WAL stream. Every page header stores the LSN of the most recent WAL record that modified it. During crash recovery, PostgreSQL checks if a page's LSN is older than the WAL record being replayed — if it is, the page needs the update; if it's already newer, the update is idempotent and skipped.

**Crash recovery:**
1. PostgreSQL starts, reads the last checkpoint record from `pg_control`
2. Replays all WAL records from that checkpoint LSN forward
3. For each WAL record: if the target page's LSN < record's LSN, apply the change
4. The database reaches the consistent committed state

**Checkpointing:**  
A checkpoint writes all dirty shared buffers to disk and records the checkpoint LSN. This bounds crash recovery time — without checkpoints, you'd replay WAL from the beginning of time. The `checkpoint_completion_target` parameter spreads checkpoint I/O to avoid I/O spikes.

---

## 4. Design Trade-Offs

### Buffer manager: ClockSweep vs LRU

| | LRU | ClockSweep |
|---|---|---|
| Eviction quality | Exact recency | Approximate recency |
| Lock required per access | Yes (to update list) | No (atomic increment) |
| Sequential scan protection | Poor (floods cache) | Good (usage_count = 1) |
| Implementation complexity | Higher | Lower |

PostgreSQL chose ClockSweep specifically because LRU's per-access lock becomes a serialization point under heavy concurrency. The approximation quality loss is acceptable.

### MVCC vs lock-based concurrency

| | MVCC (PostgreSQL) | 2PL locking |
|---|---|---|
| Reader/writer conflict | None | Readers block writers |
| Write-write conflict | Still serialized via row locks | Serialized |
| Storage overhead | Dead tuples accumulate | No extra versions |
| Need for cleanup | VACUUM required | None |

The read/write non-blocking is critical for OLTP: a reporting query reading millions of rows shouldn't block ongoing inserts.

### Append-only updates vs in-place updates (PostgreSQL vs InnoDB)

PostgreSQL writes new tuple versions in the heap (append-only). InnoDB modifies the row in-place and writes the old version to an undo log. PostgreSQL's approach simplifies the code path (no separate undo log) but scatters the live tuple anywhere in the heap. InnoDB's approach keeps the latest version at a predictable location but requires reading the undo chain for older snapshots.

---

## 5. Experiments / Observations

### 5.1 EXPLAIN ANALYZE on a join query

```sql
CREATE TABLE orders (id SERIAL PRIMARY KEY, user_id INT, amount NUMERIC);
CREATE TABLE users  (id SERIAL PRIMARY KEY, name TEXT);
CREATE INDEX idx_orders_user ON orders(user_id);

INSERT INTO users  SELECT g, 'user_'||g FROM generate_series(1,1000) g;
INSERT INTO orders SELECT g, (random()*1000)::int, (random()*1000)::numeric
                   FROM generate_series(1,100000) g;

ANALYZE users; ANALYZE orders;

EXPLAIN ANALYZE
  SELECT u.name, SUM(o.amount)
  FROM orders o JOIN users u ON o.user_id = u.id
  GROUP BY u.name;
```

**Output (key lines):**
```
HashAggregate  (cost=4821.00..4831.00 rows=1000 ...)
               (actual time=98.3..99.1 rows=1000 ...)
  ->  Hash Join  (cost=28.50..3571.00 ...)
                 (actual time=0.6..72.4 ...)
        Hash Cond: (o.user_id = u.id)
        ->  Seq Scan on orders  (actual rows=100000)
        ->  Hash  (actual rows=1000)
              ->  Seq Scan on users  (actual rows=1000)
```

**Analysis:**
- Planner chose **Hash Join** over Nested Loop because it estimated 100,000 order rows — nested loop would be O(100K × 1K) = 100M comparisons; hash join is O(100K + 1K).
- The `users` table (1,000 rows) was used to build the hash table (fits in `work_mem`), orders probe it.
- The planner got the row count estimates right because `ANALYZE` had just been run. Without `ANALYZE`, the planner would use default estimates and might choose a worse plan.

### 5.2 Observing MVCC dead tuples

```sql
UPDATE orders SET amount = amount * 1.1;  -- creates 100,000 dead tuples

SELECT n_dead_tup, n_live_tup
FROM pg_stat_user_tables WHERE relname = 'orders';
-- n_dead_tup: 100000  n_live_tup: 100000

VACUUM orders;

SELECT n_dead_tup FROM pg_stat_user_tables WHERE relname = 'orders';
-- n_dead_tup: 0
```

A single UPDATE on 100K rows created 100K dead tuples that doubled the table's logical size. VACUUM reclaimed the space in ~200ms.

### 5.3 Buffer manager: shared_buffers effect

```sql
SET enable_seqscan = off;
-- First run (cold cache): Execution Time: 45ms
-- Second run (hot cache): Execution Time: 2ms
-- 22x speedup from buffer manager caching pages
```

---

## 6. Key Learnings

**1. The buffer manager's ClockSweep is a deliberate accuracy-for-throughput trade.**  
The decision to avoid LRU isn't laziness — it's a concurrency engineering decision. A global LRU list under thousands of TPS becomes a bottleneck. ClockSweep's approximate eviction quality is an acceptable price for the absence of a global lock on every page access.

**2. MVCC creates a hidden maintenance burden.**  
Every UPDATE and DELETE leaves a dead tuple. This is invisible to the application developer until `VACUUM` falls behind — at which point query plans degrade (stale statistics), tables bloat, and in extreme cases (xid wraparound), the database goes read-only to protect data. Understanding MVCC requires understanding `VACUUM`, `autovacuum`, and `xid` management.

**3. B-tree sibling links are what make range scans fast.**  
The textbook B-tree only links children to parents. PostgreSQL's B-trees add sibling links on leaf pages. A range query `WHERE x BETWEEN 100 AND 200` traverses the tree once to find `x=100`, then follows leaf-level sibling links until `x>200`. Without sibling links, each key in the range would require a separate root-to-leaf traversal.

**4. WAL is the single source of truth for durability.**  
The data files are "just a cache" of the committed WAL records. If every data file were deleted but the WAL was intact, the database could be fully reconstructed. This is why `pg_basebackup` + WAL archiving is a complete backup strategy.

**5. The query planner is statistics-driven, not rule-driven.**  
PostgreSQL's planner does not have hardcoded rules like "always use index for primary key lookups." It estimates the cost of every candidate plan using row count estimates from `pg_statistic` (populated by `ANALYZE`) and the cost constants in `postgresql.conf`. A table without fresh `ANALYZE` statistics gets wrong estimates, wrong plan choices, and unpredictably bad query performance.
