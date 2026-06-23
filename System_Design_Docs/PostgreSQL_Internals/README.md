# PostgreSQL Internal Architecture

**Course:** Advanced Database Management Systems
**Student:** Indrajeet Yadav | 23BCS10199

---

## 1. Problem Background

PostgreSQL was designed to answer a question that 1980s relational databases couldn't: *what happens when the data model needs to evolve — when you need user-defined types, complex queries, and transactional guarantees all at once?*

The INGRES project at UC Berkeley gave way to POSTGRES (1986) under Michael Stonebraker. The design goal was not just to store data but to build a system that could support arbitrary data types, rules, and complex integrity constraints — without sacrificing ACID guarantees.

The internal architecture reflects this: every component — the buffer manager, MVCC, WAL, the query planner — is built to be correct under concurrent, long-running workloads with complex queries. Performance is a goal, but correctness is never sacrificed for it.

---

## 2. Architecture Overview

```
                        Client
                          |
                   TCP / Unix socket
                          |
                 +--------v--------+
                 |   Postmaster    |  <- listens, forks backends
                 +--------+--------+
                          | fork() per connection
                 +--------v----------------------------------+
                 |       Backend Process                     |
                 |  SQL Text                                 |
                 |     |                                     |
                 |  +--v------+                              |
                 |  | Parser  | -> Parse tree                |
                 |  +--+------+                              |
                 |  +--v------+                              |
                 |  |Rewriter | -> Applies rules             |
                 |  +--+------+                              |
                 |  +--v------+                              |
                 |  | Planner | -> Query plan                |
                 |  +--+------+  (uses pg_statistic)        |
                 |  +--v------+                              |
                 |  |Executor | -> Fetches tuples            |
                 |  +--+------+                              |
                 +-----+-------------------------------------+
                       | reads/writes pages
              +--------v--------------------------+
              |    Shared Buffers                  |
              |  (shared across backends)          |
              |  Buffer Pool + ClockSweep          |
              +--------+--------------------------+
                       |
              +--------v--------------------------+
              |    Storage Manager                 |
              |  Heap files + Index files          |
              |  WAL (pg_wal/)                     |
              +-----------------------------------+
```

The process model is central: each client gets its own **backend process** (not a thread). They share only a fixed shared memory segment (shared buffers, WAL buffers, lock table, CLOG). This means ~5–10 MB RAM per connection, which is why connection poolers (PgBouncer) are essential at scale.

---

## 3. Internal Design

### 3.1 Buffer Manager (`src/backend/storage/buffer/`)

The buffer manager keeps frequently used pages in RAM, evicting cold pages when the pool fills.

**Shared buffers** is a fixed pool of 8 KB page frames (default 128 MB, typically 25% of RAM). All backends share this pool.

**ClockSweep replacement policy** (implemented in `freelist.c`): each buffer frame has a `usage_count` (0–5). A circular "clock hand" sweeps through frames:
- If `usage_count > 0`: decrement, skip
- If `usage_count == 0` and not pinned: evict this frame

When a page is accessed, its `usage_count` is incremented (capped at 5). Hot pages accumulate higher counts and survive more sweep rounds.

**Why not LRU?** A doubly-linked list LRU requires a lock on every page access to reorder the list. Under high concurrency this becomes a serialization bottleneck. ClockSweep accesses `usage_count` with a simple atomic increment — far lower contention.

**Sequential scan protection:** Full table scan pages get `usage_count = 1`, so they are quickly reclaimed without evicting hot index pages. An LRU would flush the entire hot set.

### 3.2 B-Tree Implementation (`src/backend/access/nbtree/`)

**Page structure:**
```
B-Tree Leaf Page (8 KB)
+------------------------+
| BTPageOpaqueData       |  <- btpo_prev, btpo_next (sibling links)
|  prev/next page links  |     btpo_level, btpo_flags
+------------------------+
| Item pointer array     |  <- sorted offsets to index tuples
+------------------------+
|       free space       |
+------------------------+
| Index tuples           |  <- (key, heap TID) pairs
+------------------------+
```

Leaf pages are linked as a **doubly-linked list** (btpo_prev / btpo_next). This enables fast range scans: find the first matching key via tree traversal, then follow sibling pointers without going back up.

**Why B-tree and not hash indexes for everything?** Hash indexes support only equality (`=`). B-trees support `=`, `<`, `>`, `BETWEEN`, `ORDER BY`, and prefix matches on composite keys — far more versatile.

### 3.3 MVCC — Multi-Version Concurrency Control

**Heap tuple header (simplified):**
```
+--------------------------------+
| t_xmin  (uint32)               |  <- xid that inserted this version
| t_xmax  (uint32)               |  <- xid that deleted/updated (0 = live)
| t_ctid  (ItemPointer)          |  <- points to newest version of this row
| t_infomask                     |  <- commit status hints
| ... column data ...            |
+--------------------------------+
```

**How an UPDATE works:**
1. Old tuple's `t_xmax` is set to current transaction's xid
2. New tuple is inserted with `t_xmin` = current xid, `t_xmax` = 0
3. Old tuple's `t_ctid` points to the new tuple

No tuple is overwritten in place.

**Visibility rule:** A version is visible to transaction T (with snapshot S) if:
- `t_xmin` is committed AND `t_xmin` < S
- AND either `t_xmax` = 0, OR `t_xmax` was not committed before S

**CLOG (Commit Log):** Commit/abort status lives in bitfields under `pg_xact/`. The `t_infomask` caches hints to avoid CLOG reads on hot paths.

**Why VACUUM is necessary:** Dead tuples are never cleaned by the writer. `VACUUM` scans the heap, finds tuples whose `t_xmax` is committed and older than the oldest active snapshot, and marks their space as reusable.

### 3.4 WAL — Write-Ahead Logging (`src/backend/access/transam/xlog.c`)

**Core invariant:** A change must be written to the WAL before it can be written to the data file.

**WAL record structure:**
```
+--------------------------------+
| xl_tot_len (record length)     |
| xl_xid     (transaction id)    |
| xl_prev    (previous LSN)      |
| xl_rmid    (resource manager)  |
| xl_info    (operation type)    |
+--------------------------------+
| Resource manager data          |
+--------------------------------+
```

**LSN (Log Sequence Number):** A monotonically increasing 64-bit offset into the WAL stream. Each page header stores the LSN of the most recent WAL record that modified it.

**Crash recovery:**
1. Read last checkpoint from `pg_control`
2. Replay all WAL records from that checkpoint forward
3. For each record: if page LSN < record LSN, apply the change
4. Database reaches consistent committed state

**Checkpointing:** Writes all dirty shared buffers to disk and records the checkpoint LSN. Bounds crash recovery time. `checkpoint_completion_target` spreads I/O to avoid spikes.

---

## 4. Design Trade-Offs

### Buffer manager: ClockSweep vs LRU

| | LRU | ClockSweep |
|---|---|---|
| Eviction quality | Exact recency | Approximate recency |
| Lock per access | Yes (to update list) | No (atomic increment) |
| Sequential scan protection | Poor | Good (usage_count = 1) |
| Implementation complexity | Higher | Lower |

### MVCC vs lock-based concurrency

| | MVCC (PostgreSQL) | 2PL locking |
|---|---|---|
| Reader/writer conflict | None | Readers block writers |
| Write-write conflict | Serialized via row locks | Serialized |
| Storage overhead | Dead tuples accumulate | No extra versions |
| Need for cleanup | VACUUM required | None |

### Append-only updates vs in-place updates

PostgreSQL writes new tuple versions in the heap (append-only). InnoDB modifies the row in-place and writes the old version to an undo log. PostgreSQL's approach simplifies the code path but scatters live tuples anywhere in the heap.

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
  ->  Hash Join  (cost=28.50..3571.00 ...)
        Hash Cond: (o.user_id = u.id)
        ->  Seq Scan on orders  (actual rows=100000)
        ->  Hash  (actual rows=1000)
              ->  Seq Scan on users  (actual rows=1000)
```

Planner chose Hash Join because nested loop would be O(100K x 1K) = 100M comparisons. The smaller `users` table (1,000 rows) builds the hash table.

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

### 5.3 Buffer manager: shared_buffers effect

```sql
SET enable_seqscan = off;
-- First run (cold cache):  Execution Time: 45ms
-- Second run (hot cache):  Execution Time: 2ms
-- 22x speedup from buffer manager caching pages
```

---

## 6. Key Learnings

1. **ClockSweep is a deliberate accuracy-for-throughput trade.** Avoiding LRU's per-access lock is a concurrency engineering decision, not laziness. The approximate eviction quality is an acceptable price.

2. **MVCC creates a hidden maintenance burden.** Every UPDATE and DELETE leaves a dead tuple. VACUUM falling behind causes query plan degradation, table bloat, and in extreme cases (xid wraparound), a read-only database.

3. **B-tree sibling links are what make range scans fast.** Without them, each key in a range would require a separate root-to-leaf traversal. With them, a range scan traverses the tree once, then follows leaf-level links.

4. **WAL is the single source of truth for durability.** The data files are "just a cache" of committed WAL records. `pg_basebackup` + WAL archiving is a complete backup strategy.

5. **The query planner is statistics-driven, not rule-driven.** Wrong row count estimates (stale `ANALYZE`) produce wrong plan choices and unpredictably bad query performance.
