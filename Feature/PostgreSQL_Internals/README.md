# PostgreSQL Internal Architecture

## 1. Problem Background

PostgreSQL has to satisfy three goals at the same time: many concurrent users, durable commits, and correct results under crashes. The internals — buffer manager, B-tree, MVCC, WAL — exist to make those goals work together without serializing everything.

---

## 2. Architecture Overview

```
              ┌────────────────────────────────────────┐
              │            Backend (per client)        │
   SQL ─────▶ │  Parser → Rewriter → Planner → Executor│
              └─────────┬──────────────────────────────┘
                        │ buffer requests, lock requests
              ┌─────────▼──────────────────────────────┐
              │   Shared Memory                         │
              │  ┌──────────────┐  ┌─────────────────┐  │
              │  │ shared_buffers│  │ WAL buffers    │  │
              │  └──────┬───────┘  └────────┬────────┘  │
              └─────────┼───────────────────┼───────────┘
                        ▼                   ▼
                   data files            pg_wal/
                  (heap + idx)         (WAL segments)
   Background: bgwriter, checkpointer, WAL writer, autovacuum
```

Each query lives in one backend process. The interesting work — caching, logging, recovery — happens in the shared memory area and in background workers.

---

## 3. Internal Design

### Buffer Manager — `src/backend/storage/buffer/`

The buffer pool is a fixed-size array of 8 KB frames in shared memory.
- A backend asks for a `(relation, blockNumber)` page. If it's in a frame, pin it; otherwise pick a victim, write it back if dirty, and read the requested page in.
- Replacement uses **clock-sweep**: each buffer has a `usage_count` that decays as the clock hand passes; the first one at zero is evicted.
- Dirty pages are flushed by **bgwriter** in the background and by **checkpointer** at checkpoint time, so a backend rarely waits on a synchronous flush.

### B-tree Index — `src/backend/access/nbtree/`

PostgreSQL uses Lehman–Yao high-key B+trees:
- Internal nodes hold separator keys and child pointers; leaves hold `(key → ctid)`.
- Each page stores a **high key** and a **right-link** to its right sibling. A descender can always recover even if a concurrent split moved the target right.
- **Search**: descend by binary search on each level; if the key exceeds the high key, follow the right-link.
- **Insert**: descend to leaf, insert in sorted order. If the page is full, **split** in half, promote the new separator to the parent. Splits propagate upward only as far as needed.

### MVCC — Heap Tuples and Visibility

Every heap tuple stores `xmin` (inserter txid) and `xmax` (deleter / updater txid).

```
Update of row R by txn 150:
   old tuple: xmin=100, xmax=150    ← dead once no snapshot needs it
   new tuple: xmin=150, xmax=0      ← visible to txn ≥ 150
```

A snapshot captures the set of in-progress transactions at query start. A tuple is visible if `xmin` is committed and not in the snapshot, and `xmax` is 0 or aborted or still in the snapshot. Result: readers and writers never block each other.

The cost is **dead tuples**. **VACUUM** marks them reusable, updates the visibility map, and prevents transaction ID wraparound by freezing very old tuples.

### WAL — Write-Ahead Logging

Rule: **the WAL record describing a change must hit durable storage before the modified data page is written back.**

- A change inserts a record into the WAL buffer; the WAL writer flushes it; `COMMIT` waits for the flush.
- **Checkpoint** writes all dirty buffers and records the LSN — replay can start from there after a crash.
- Recovery: read WAL from the last checkpoint, redo every record whose LSN exceeds the page's LSN.
- The same byte stream feeds **streaming replication** — standbys redo it continuously.

---

## 4. Design Trade-Offs

- **Process per connection** → strong isolation, easy debugging; high memory cost, hence connection poolers.
- **Heap + separate indexes (vs clustered)** → cheap secondary indexes (no need to rewrite when PK changes), but every index lookup needs an extra heap fetch.
- **MVCC by versioning** → readers never block; but bloat is real and VACUUM must keep up.
- **WAL** → durability and replication for free; but every write is logged twice (WAL + page) — write amplification.
- **Clock-sweep buffer eviction** → cheap, scalable, decent under mixed workloads; but pathological with very large scans (mitigated by ring buffers for sequential scans).

---

## 5. Experiments / Observations

`EXPLAIN ANALYZE` on a join (orders × customers × order_items):
```
HashAggregate  (cost=8432.10..8534.12 rows=10202 width=52)
               (actual time=145.3..148.1 rows=9876)
  ->  Hash Join  (cost=1240.5..7982.0 rows=50012)
        Hash Cond: (oi.order_id = o.id)
        ->  Seq Scan on order_items
        ->  Hash
              ->  Hash Join
                    ->  Seq Scan on orders
                    ->  Hash on customers
```
Observations:
- The planner used hash joins because row-count estimates from `pg_statistic` (populated by `ANALYZE`) were realistic.
- `cost=` is the planner's estimate; `actual time=` is what really happened — a big gap usually means stale stats.
- After `ANALYZE`, plan changed from nested loops (used when row estimates were 1) to hash joins. Same SQL, very different runtime.

Bloat demo:
```sql
CREATE TABLE t AS SELECT i FROM generate_series(1,200000) i;
UPDATE t SET i = i + 1;
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname='t';
-- 200000 live, 200000 dead
VACUUM t;   -- dead -> 0, space marked reusable
```

---

## 6. Key Learnings

- Almost every PostgreSQL internal exists to break the "one big lock" pattern: MVCC for tuple-level concurrency, clock-sweep for the buffer pool, right-linked B-trees for lock-free descent.
- WAL is the single mechanism behind both crash recovery and replication — one good idea solving two hard problems.
- VACUUM is not a bug — it is the price of letting readers and writers never block. Autovacuum tuning is therefore a first-class operational concern.
- The planner is statistics-driven: if `ANALYZE` is stale, plans are stale. Watching `EXPLAIN ANALYZE` is the most direct way to understand the system.
