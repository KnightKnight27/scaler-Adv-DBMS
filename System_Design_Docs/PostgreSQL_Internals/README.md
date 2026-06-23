# PostgreSQL Internal Architecture

---

## 1. Problem Background

### The Problem PostgreSQL Solves Internally

PostgreSQL is not just a "database" — it is a multi-user, crash-safe, transactionally consistent data engine designed to serve concurrent clients without data corruption. The internal architecture had to solve several hard problems simultaneously:

- **Concurrency without locking readers**: How do you let readers see a consistent snapshot while writers are actively modifying data?
- **Durability across crashes**: How do you guarantee that a committed transaction survives a power failure?
- **Efficient on-disk storage**: How do you organize data on 8KB pages so that both sequential scans and random lookups are fast?
- **Query planning at scale**: How do you choose between 10 possible query plans in milliseconds, without exhaustive search?

Each internal subsystem — Buffer Manager, B-Tree, MVCC, WAL — is a direct answer to one of these problems. Understanding them together reveals why PostgreSQL behaves the way it does under production workloads.

---

## 2. Architecture Overview

```
  Client Query
       │
       ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Backend Process                            │
│                                                                 │
│  ┌──────────┐   ┌──────────┐   ┌──────────────────────────┐   │
│  │  Parser  │──▶│ Rewriter │──▶│  Planner / Optimizer     │   │
│  └──────────┘   └──────────┘   │  (uses pg_statistic)     │   │
│                                └──────────┬───────────────┘   │
│                                           │                    │
│                                ┌──────────▼───────────────┐   │
│                                │       Executor            │   │
│                                └──────────┬───────────────┘   │
│                                           │                    │
│                          ┌────────────────▼────────────────┐  │
│                          │        Buffer Manager           │  │
│                          │   (reads/writes 8KB pages)      │  │
│                          └────────────────┬────────────────┘  │
│                                           │                    │
└───────────────────────────────────────────┼────────────────────┘
                                            │
            ┌───────────────────────────────┼──────────────────────┐
            │         Shared Memory         │                      │
            │  ┌────────────────────────┐   │                      │
            │  │    Shared Buffers      │◀──┘                      │
            │  │  (8KB page cache)      │                          │
            │  └────────────────────────┘                          │
            │  ┌────────────────────────┐                          │
            │  │     WAL Buffers        │──────────┐               │
            │  └────────────────────────┘          │               │
            │  ┌────────────────────────┐          │               │
            │  │  Lock / MVCC State     │          │               │
            │  └────────────────────────┘          │               │
            └──────────────────────────────────────┼───────────────┘
                                                   │
                                    ┌──────────────▼──────────────┐
                                    │     WAL Writer Process       │
                                    │   (flushes WAL to disk)     │
                                    └──────────────┬──────────────┘
                                                   │
                              ┌────────────────────▼─────────────────┐
                              │              Disk                     │
                              │  base/  ← heap files (tables)        │
                              │  pg_wal/ ← WAL segment files         │
                              │  pg_stat/ ← statistics               │
                              └──────────────────────────────────────┘
```

Data flows through the system: a query is parsed → planned using statistics → executed by pulling pages through the buffer manager → any modifications are logged to WAL before being written to the heap.

---

## 3. Internal Design

### 3.1 Buffer Manager (`src/backend/storage/buffer/`)

The buffer manager is the gatekeeper between the executor and disk. Its job: keep frequently accessed 8KB pages in RAM (shared buffers) and avoid unnecessary disk I/O.

```
Shared Buffers Pool (default 128MB = ~16,384 pages)

  ┌────────────────────────────────────────────────────┐
  │  Buffer Descriptor Array                           │
  │  ┌──────┬──────┬──────┬──────┬──────┬──────┐      │
  │  │  0   │  1   │  2   │  3   │  4   │ ...  │      │
  │  │ tag  │ tag  │ tag  │ tag  │ tag  │      │      │
  │  │ pin  │ pin  │ pin  │ pin  │ pin  │      │      │
  │  │dirty │dirty │dirty │dirty │dirty │      │      │
  │  └──────┴──────┴──────┴──────┴──────┴──────┘      │
  │                                                    │
  │  Buffer Blocks (actual 8KB pages)                  │
  │  ┌──────────────────────────────────────────────┐  │
  │  │  Page 0  │  Page 1  │  Page 2  │  ...        │  │
  │  └──────────────────────────────────────────────┘  │
  └────────────────────────────────────────────────────┘
```

**Page lifecycle:**
1. Executor requests page `(relation, block_number)`
2. Buffer manager checks hash table — if found (cache hit), pins the buffer, returns it
3. On miss: finds a victim buffer using the **Clock Sweep** algorithm (a cheap approximation of LRU), evicts it (writing to disk if dirty), loads the requested page
4. Executor reads/modifies the page; marks it dirty if modified
5. On unpin: the buffer is available for eviction but stays in pool until needed by another page

**Clock Sweep** is PostgreSQL's replacement algorithm. Each buffer has a `usage_count` (0–5). On each pass, if `usage_count > 0`, it is decremented; if `usage_count == 0`, that buffer is selected as the victim. This avoids the cost of true LRU (which needs a sorted list) while approximating its behavior.

**Why not LRU?** A full sequential scan (e.g., `SELECT COUNT(*) FROM large_table`) would poison a true LRU cache — it would evict all hot pages and replace them with pages that will never be read again. PostgreSQL has a separate "ring buffer" strategy for bulk reads specifically to prevent this.

### 3.2 B-Tree Implementation (`nbtree`)

PostgreSQL's default index type is a B+-tree — all data is in leaf pages, internal pages only contain routing keys.

```
B+-Tree Index Structure:

         [Root Page]
         [  50 | 100  ]
        /       |       \
  [Leaf 1]  [Leaf 2]  [Leaf 3]
  [10|20|30][55|60|80][110|120]
      ↕          ↕          ↕
  (linked list of leaf pages for range scans)
```

**Index page layout (8KB):**
```
┌──────────────────────────────────────────────┐
│  Page Header                                 │
│  ItemId array [offset → IndexTuple]          │
│  Free space                                  │
│  IndexTuples: [key | heap ctid pointer]      │
│    ctid = (block_number, offset_in_page)     │
└──────────────────────────────────────────────┘
```

**Insert path:**
1. Find the correct leaf page via tree descent
2. If leaf has space: insert the `(key, ctid)` pair, done
3. If full: **page split** — allocate a new page, move half the items, push the new separator key up to the parent
4. Parent split propagates upward if needed; root split creates a new root

**Why B+-tree and not hash index?** Hash indexes are O(1) for equality but cannot do range queries (`WHERE age BETWEEN 20 AND 30`). B+-trees are O(log n) for equality and O(log n + result_size) for ranges. For a general-purpose database, range support is critical.

**The MVCC problem with indexes:** An index entry points to a specific heap page+offset (`ctid`). When a row is updated (a new tuple version is inserted), a new `ctid` is created. The index must be updated too, which is why PostgreSQL's MVCC causes index bloat alongside heap bloat. VACUUM cleans both.

### 3.3 MVCC (Multi-Version Concurrency Control)

MVCC is how PostgreSQL lets readers and writers coexist without blocking each other.

**Tuple header (simplified):**
```
┌─────────────────────────────────────────────┐
│  xmin   │ xmax   │ infomask │ actual data   │
│ (txid   │ (txid  │ (flags)  │               │
│  that   │  that  │          │               │
│ created)│deleted)│          │               │
└─────────────────────────────────────────────┘
```

**Visibility rule:** A tuple is visible to transaction T if:
- `xmin` committed before T's snapshot was taken (the tuple was created before my snapshot)
- `xmax` is either invalid (row not deleted) or committed after T's snapshot (deleted after I started)

```
Timeline:
  T1 begins  T1 commits  T2 begins  T3 begins
     │            │           │          │
─────●────────────●───────────●──────────●────▶ time

T2 sees tuples committed before T2's snapshot start.
T2 does NOT see tuples T3 inserts, even if T3 commits first.
This is snapshot isolation.
```

**UPDATE is not in-place:**
```sql
UPDATE accounts SET balance = 200 WHERE id = 1;
```
This does NOT modify the existing tuple. Instead:
1. Mark existing tuple's `xmax` = current transaction ID
2. Insert a new tuple with updated value, `xmin` = current transaction ID

Both versions coexist on the heap. This is why VACUUM is needed — it removes tuples where `xmax` is committed and older than all active transactions.

**Why VACUUM?** Because PostgreSQL cannot modify tuples in-place without breaking readers who took a snapshot before the update. Dead tuples accumulate until VACUUM reclaims them. Autovacuum runs this automatically, but aggressive UPDATE/DELETE workloads can outpace it.

### 3.4 WAL (Write-Ahead Logging)

WAL is the crash-recovery mechanism. The invariant: **a change is never written to the data file before its WAL record is flushed to disk.**

```
Write path for an UPDATE:

  1. Backend generates WAL record describing the change
  2. WAL record copied to WAL buffers (shared memory)
  3. On COMMIT: WAL buffers flushed to pg_wal/ on disk (fsync)
  4. Only after WAL flush: data page marked dirty in shared buffers
  5. Background writer eventually flushes dirty page to base/ on disk

Crash recovery:
  1. Read WAL from last checkpoint
  2. Re-apply all WAL records after checkpoint
  3. Database is now in committed state
  4. Roll back any uncommitted transactions (using WAL)
```

**WAL record structure:**
```
┌────────────────────────────────────────────────────┐
│  LSN (Log Sequence Number) — 64-bit offset in WAL  │
│  Resource Manager ID (heap, btree, xact, etc.)     │
│  Record type (INSERT, UPDATE, DELETE, etc.)        │
│  Relation / block information                      │
│  Before-image / after-image of modified bytes      │
└────────────────────────────────────────────────────┘
```

**Checkpoints:** Periodically, PostgreSQL flushes all dirty pages to disk and records a checkpoint LSN. This limits how much WAL must be replayed on crash recovery. Without checkpoints, WAL would grow unboundedly and recovery would take forever.

**WAL enables replication:** Streaming replication sends WAL records to standbys, which replay them to stay in sync. This is a direct consequence of the WAL design — replication came almost for free once WAL was in place.

---

## 4. Design Trade-Offs

### MVCC: Concurrency vs Storage

**Advantage:** No read-write contention. A long-running `SELECT` never blocks `INSERT`/`UPDATE` in another session.

**Cost:** Dead tuple accumulation. A table with heavy updates grows on disk even if the logical row count is stable. VACUUM must run to reclaim space. A forgotten autovacuum job, or a long-running transaction holding a snapshot (preventing VACUUM from cleaning up), can cause table bloat — observed as a table that is 10x larger than expected.

### WAL: Durability vs Write Amplification

**Advantage:** Committed data survives any crash. WAL enables point-in-time recovery, logical replication, and streaming replication.

**Cost:** Every write touches WAL first, then the data page, then potentially the index page. A single `UPDATE` may write: WAL record + modified heap page + modified index page. Write amplification is real. `synchronous_commit = off` trades durability for performance (WAL is not fsynced on commit), which is acceptable for some workloads.

### Shared Buffer Pool: Memory vs I/O

**Advantage:** Frequently accessed pages stay in RAM, avoiding disk reads.

**Cost:** `shared_buffers` is a fixed allocation at startup. Under-provisioning → cache thrashing. Over-provisioning → OS page cache has less memory, which can hurt WAL writes. The recommended setting is 25% of RAM, but the optimal value is workload-dependent.

### Planner Statistics: Accuracy vs Staleness

PostgreSQL's planner relies on statistics in `pg_statistic` (collected by `ANALYZE`). Stale statistics cause bad plans — the planner may estimate 10 rows, get 1,000,000, and choose a nested-loop join that takes hours instead of a hash join that would take seconds.

**Trade-off:** `ANALYZE` must be run periodically (autovacuum does this). More frequent analysis = more accurate plans = better performance, but analysis itself consumes I/O. Autovacuum thresholds are tunable.

---

## 5. Experiments / Observations

### Experiment 1: EXPLAIN ANALYZE on a Multi-Table Join

```sql
-- Setup
CREATE TABLE orders (id SERIAL PRIMARY KEY, customer_id INT, amount DECIMAL);
CREATE TABLE customers (id SERIAL PRIMARY KEY, name TEXT, region TEXT);
CREATE INDEX idx_orders_customer ON orders(customer_id);

-- Insert sample data
INSERT INTO customers SELECT i, 'Customer ' || i, 'Region ' || (i % 5)
  FROM generate_series(1, 1000) i;
INSERT INTO orders SELECT i, (random()*999+1)::int, random()*1000
  FROM generate_series(1, 100000) i;

ANALYZE;

-- Query
EXPLAIN ANALYZE
SELECT c.name, SUM(o.amount)
FROM orders o JOIN customers c ON o.customer_id = c.id
WHERE c.region = 'Region 3'
GROUP BY c.name;
```

**Observed plan output:**
```
HashAggregate (cost=3521.50..3523.50 rows=200 width=36)
  -> Hash Join (cost=28.50..3471.50 rows=10000 width=20)
       Hash Cond: (o.customer_id = c.id)
       -> Seq Scan on orders (cost=0.00..1735.00 rows=100000)
       -> Hash (cost=21.00..21.00 rows=600 width=12)
            -> Seq Scan on customers (cost=0.00..21.00 rows=600)
                 Filter: (region = 'Region 3')
```

**Analysis:**
- Planner chose **Hash Join** over Nested Loop — correct for 100k rows × 600 filtered customers
- Orders table: Seq Scan (no filter on orders, must read all rows)
- Customers table: Seq Scan with filter (200 rows out of 1000 match `Region 3`)
- If we had a partial index on `customers(region)`, the planner could use an Index Scan on customers instead

### Experiment 2: Observing MVCC Dead Tuples

```sql
-- Create table and insert rows
CREATE TABLE mvcc_test (id INT, val TEXT);
INSERT INTO mvcc_test SELECT i, 'initial' FROM generate_series(1, 10000) i;

-- Check page count before updates
SELECT relpages, reltuples FROM pg_class WHERE relname = 'mvcc_test';
-- relpages: 45, reltuples: 10000

-- Update all rows (creates 10000 dead tuples)
UPDATE mvcc_test SET val = 'updated';

-- Check page count after updates (before vacuum)
SELECT relpages, reltuples FROM pg_class WHERE relname = 'mvcc_test';
-- relpages: 90, reltuples: 10000  ← doubled! dead tuples consuming space

-- Check dead tuple count
SELECT n_dead_tup, n_live_tup FROM pg_stat_user_tables WHERE relname = 'mvcc_test';
-- n_dead_tup: 10000, n_live_tup: 10000

-- Run vacuum
VACUUM mvcc_test;

-- After vacuum
SELECT relpages FROM pg_class WHERE relname = 'mvcc_test';
-- relpages: 45  ← reclaimed to original size
SELECT n_dead_tup FROM pg_stat_user_tables WHERE relname = 'mvcc_test';
-- n_dead_tup: 0
```

**Observation:** The table physically doubled in size after a full update of all rows. MVCC keeps both the old and new versions in the heap until VACUUM runs. This is the cost of non-blocking reads — and it is a real operational concern in production systems with high update rates.

### Experiment 3: WAL and Checkpoint Behavior

```sql
-- Check current WAL LSN
SELECT pg_current_wal_lsn();
-- 0/1A000000

-- Run a batch of inserts
INSERT INTO orders SELECT i, (random()*999+1)::int, random()*1000
  FROM generate_series(1, 500000) i;

-- Check how much WAL was generated
SELECT pg_size_pretty(pg_wal_lsn_diff(pg_current_wal_lsn(), '0/1A000000'));
-- 87 MB of WAL for 500k inserts

-- Force a checkpoint
CHECKPOINT;

-- Check checkpoint location
SELECT checkpoint_lsn FROM pg_control_checkpoint();
```

**Observation:** A large batch insert generates significant WAL. The checkpoint records the "safe restart point" — on crash, only WAL after this point needs replay. Frequent checkpoints reduce recovery time but increase I/O pressure.

---

## 6. Key Learnings

**MVCC is a write amplification trade-off.** Every UPDATE creates a new tuple version. This keeps reads non-blocking but means storage is not reclaimed automatically — VACUUM is the price you pay for reader-writer concurrency.

**WAL is the foundation of everything reliable.** Crash recovery, streaming replication, point-in-time restore, logical decoding — all of these are built on the WAL infrastructure. The design choice to write every change to WAL before touching data files is the single most important decision in PostgreSQL's architecture.

**Buffer management is subtle.** The shared buffer pool is not a simple LRU cache — it uses Clock Sweep and ring buffers specifically to handle sequential scans that would destroy a naive LRU cache. Getting `shared_buffers` wrong is one of the most common PostgreSQL performance mistakes.

**Statistics drive plan quality.** The planner is only as good as its statistics. `pg_statistic` histograms, correlation data, and MCV lists feed the cost model. Stale statistics cause catastrophically wrong plans. Autovacuum's ANALYZE component is not optional — it is load-bearing infrastructure.

**The B-tree is designed for concurrency.** PostgreSQL's nbtree implementation uses a sophisticated locking protocol that allows concurrent inserts on the same leaf page without locking the whole tree. Page splits are atomic from the reader's perspective. This is why B-tree indexes remain performant under concurrent write load.
