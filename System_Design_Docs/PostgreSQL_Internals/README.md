# PostgreSQL Internal Architecture

## 1. Problem Background

### Why PostgreSQL Was Created

PostgreSQL descends from the POSTGRES project at UC Berkeley, led by Michael Stonebraker beginning in 1986. Its direct predecessor, INGRES, had proven that relational databases were viable, but its design had hardened around assumptions that did not hold for the workloads researchers and enterprises were encountering: complex object types, rule systems, and especially multi-user transactional workloads where many clients needed to read and write the same data simultaneously without corrupting each other's view of it.

The core problem POSTGRES set out to solve was: how do you build a database that is simultaneously correct under concurrent access, recoverable after a crash, and fast enough to be useful? These three goals are in direct tension. Making writes durable requires I/O. Making concurrent access correct requires coordination. Making both fast requires hiding latency through caching and batching. The entire internal architecture of PostgreSQL is a set of engineering decisions about how to navigate those tensions.

### Goals of PostgreSQL

- **Correctness under concurrency**: Many clients can read and write simultaneously. No client should see partially-applied writes from another, and no committed transaction should be lost.
- **Crash recovery**: If the server process dies mid-transaction — power failure, OOM kill, kernel panic — the database must return to a consistent state on restart without manual intervention.
- **Extensibility**: New data types, index types, operators, and procedural languages should be addable without modifying the core engine. This drove the catalog-based architecture.
- **SQL standards compliance**: PostgreSQL tracks the SQL standard closely, including transaction isolation levels defined in SQL:1992 and later.

### Why Sophisticated Storage and Transaction Systems Are Necessary

A naive database — write rows directly to a file, read them back — fails in all three dimensions above. If two writers update the same row at the same time, one update is silently lost. If the process dies while writing, the file is partially updated with no way to identify which parts are valid. If the file is large, every query reads the whole thing from disk.

The subsystems analyzed in this document — the buffer manager, B-tree indexes, MVCC, VACUUM, and WAL — are not optional complexity. Each one exists because the naive alternative produces incorrect or unacceptably slow behavior in real workloads.

---

## 2. Architecture Overview

### High-Level Architecture

```
  Client Application (psql, libpq, JDBC, etc.)
        |
        | TCP/IP or Unix domain socket
        v
+-------------------+
|    Postmaster     |  Listens on port 5432, forks backends
+-------------------+
        |
        | fork() per connection
        v
+--------------------------------------------------+
|               Backend Process                    |
|                                                  |
|  SQL Text                                        |
|    |                                             |
|    v                                             |
|  [ Parser ]  -- produces parse tree             |
|    |                                             |
|    v                                             |
|  [ Analyzer ] -- semantic analysis, type check  |
|    |                                             |
|    v                                             |
|  [ Rewriter ] -- apply rules / views            |
|    |                                             |
|    v                                             |
|  [ Planner / Optimizer ] -- cost-based plan     |
|    |                                             |
|    v                                             |
|  [ Executor ] -- executes plan nodes            |
|    |                                             |
+----|--------------------------------------------|
     | calls Buffer Manager API
     v
+--------------------------------------------------+
|              Shared Memory                       |
|                                                  |
|  +------------------+   +--------------------+  |
|  |   Buffer Pool    |   |    WAL Buffers     |  |
|  |  (shared_buffers)|   |                    |  |
|  +------------------+   +--------------------+  |
|  +------------------+   +--------------------+  |
|  |   Lock Table     |   |  Commit Log (clog) |  |
|  +------------------+   +--------------------+  |
|                                                  |
+--------------------------------------------------+
     |                          |
     | dirty pages              | WAL records
     v                          v
+----------+              +-----------+
| Data     |              |  pg_wal/  |
| Files    |              |  (WAL     |
| base/    |              |  segments)|
+----------+              +-----------+
     ^
     | background I/O
+----+-------+   +-----------+   +-----------+
| bgwriter   |   | WAL writer|   |checkpointer|
+------------+   +-----------+   +-----------+
     ^
+-----------+
|autovacuum |
+-----------+
```

### Query Execution Flow

```
Client SQL string
    │
    ▼
Parser        → raw parse tree (nodes, not yet type-checked)
    │
    ▼
Analyzer      → query tree (columns resolved, types checked against pg_catalog)
    │
    ▼
Rewriter      → query tree after view expansion and rule application
    │
    ▼
Planner       → plan tree (seq scan / index scan / hash join / etc., with cost estimates)
    │
    ▼
Executor      → iterates plan nodes (pull model: each node calls child.GetNext())
    │
    ▼
Buffer Mgr    → serves pages from shared_buffers or fetches from disk
    │
    ▼
Heap / Index  → physical rows read from 8KB pages
    │
    ▼
Result tuples → sent back over the wire to the client
```

The executor uses a **pull model** (also called the Volcano model): the top plan node calls `GetNext()` on its child, which calls `GetNext()` on its child, recursively, until a leaf node reads pages from the buffer manager. This makes the executor easy to compose — any node type can sit above or below any other — but can be inefficient for large vectorized operations, which is why projects like DuckDB use a push/morsel-driven model instead. PostgreSQL has not adopted vectorized execution in its core engine as of this writing.

---

## 3. Internal Design

### Buffer Manager

#### What It Does

Every table and index in PostgreSQL is stored in files of 8KB pages under `base/<database OID>/`. No executor node ever reads directly from disk. Instead, every page access goes through the buffer manager, which maintains the **shared buffer pool** — a region of shared memory (`shared_buffers`, default 128MB, production systems typically 25% of RAM) that holds recently-used pages.

#### Page Reads

When the executor needs a page, it calls `ReadBuffer(relation, block_number)`. The buffer manager computes a hash of `(relation OID, block number)` and looks up the buffer pool hash table. Two outcomes:

- **Buffer hit**: the page is already in the pool. The buffer manager increments the page's usage count, pins it (so the replacement policy won't evict it while in use), and returns the buffer ID.
- **Buffer miss**: the page is not in the pool. The buffer manager must evict a victim buffer, write the victim to disk if dirty, read the needed page from disk into that buffer slot, then return the buffer ID.

#### Page Writes and Dirty Pages

Writes happen in-place inside the buffer pool. The executor modifies the in-memory page (the buffer). The buffer is marked **dirty**. The buffer manager does not immediately write it to disk — that would serialize every write to I/O latency. Instead, dirty pages accumulate in the pool. They are written to disk by:
- The **bgwriter**, which proactively flushes dirty pages in the background to keep clean buffers available.
- The **checkpointer**, which flushes all dirty pages at checkpoint intervals.
- The buffer manager itself, if it must evict a dirty buffer to serve a miss and no clean buffer is available.

A critical invariant governs dirty page writes: **a dirty page can only be written to disk after the WAL records describing its changes have been flushed to disk**. This is the WAL-before-data rule, covered in the WAL section. Violating it would make crash recovery impossible.

#### Buffer Replacement Strategy

When a buffer miss occurs and no empty buffer slot exists, the buffer manager selects a victim using a **clock-sweep** algorithm. Each buffer has a usage counter. The clock hand sweeps through buffers in circular order; if a buffer's usage count is greater than zero, it decrements the counter and moves on; if the count is zero and the buffer is not pinned, it selects that buffer as the victim.

Clock-sweep is O(n) in the worst case but typically fast in practice because most workloads have a hot working set that maintains non-zero usage counts. LRU would require an ordered data structure with O(log n) updates on every access — too expensive under high concurrency.

#### Why Buffer Management Is Critical

Without a buffer pool, every page access would require an OS `read()` call, even for pages accessed microseconds earlier. A single OLTP query touching a B-tree index traverses 3–5 pages from root to leaf. If those pages are hot (in the buffer pool), the traversal is memory latency (~100ns). If they are cold (disk miss), it is disk latency (~1ms for SSD, ~10ms for HDD) per page — a 10,000x to 100,000x difference. Buffer management is the single largest contributor to PostgreSQL's query performance on repeated workloads.

---

### B-Tree Implementation

#### Why B-Trees as the Default Index

PostgreSQL defaults to B-Tree indexes for almost all single-column and multi-column index use cases. The reason is that B-Trees support equality, range queries, ORDER BY, and LIKE prefix patterns — all with O(log n) cost per operation and predictable page access patterns. Alternative structures like hash indexes support only equality and are not WAL-logged until PostgreSQL 10. GiST and GIN handle geometric and full-text cases but have higher overhead.

#### B-Tree Page Structure

PostgreSQL's B-Tree implementation (in `src/backend/access/nbtree/`) represents the tree as a collection of 8KB pages, each of which is either a root, an internal (upper), or a leaf page.

```
                    [ Root Page ]
                   /      |      \
          [Internal]  [Internal]  [Internal]
          /     \       /    \       /    \
       [Leaf] [Leaf] [Leaf] [Leaf] [Leaf] [Leaf]
         ↕       ↕      ↕      ↕      ↕      ↕
      (linked list across all leaf pages)
```

Each page contains:
- A page header (LSN, page type flags, upper/lower free space offsets).
- An item ID array (line pointers) at the top of the page, growing downward.
- Index tuples stored from the bottom up, growing upward.
- Free space in the middle between the two.

**Leaf pages** hold `(key value, heap TID)` pairs. The heap TID is the physical location of the row in the heap file — `(block number, slot number)`. Following a leaf entry requires a separate heap page fetch to get the actual row data.

**Internal pages** hold separator keys and child page pointers. A separator key `k` at position `i` means all keys in child `i` are `< k`.

All leaf pages are linked in a doubly-linked list. This allows range scans to proceed without returning to upper levels once the starting leaf is found.

#### Search Path

To find rows where `column = value`:
1. Read the root page (usually cached in the buffer pool).
2. Binary-search the separator keys to find the correct child pointer.
3. Follow the child pointer to the next level. Repeat until a leaf page is reached.
4. Binary-search the leaf for the target key.
5. Follow the heap TID to the heap page to fetch the actual tuple.

For a table with 10 million rows and a B-tree of height 3, this is 3 index page reads + 1 heap page read = 4 total page accesses. The first 3 pages (root and upper levels) will almost always be in the buffer pool after the first few queries.

#### Inserts and Page Splits

Insertion adds a new `(key, TID)` pair to the appropriate leaf page. If the leaf page has sufficient free space, the tuple is inserted in-place and a WAL record is written.

If the leaf page is full, a **page split** occurs:
1. A new leaf page is allocated.
2. The existing tuples are redistributed between the old and new pages (typically 50/50, but PostgreSQL uses a 90/10 split for append-heavy monotonically-increasing keys to avoid wasting space).
3. A separator key is inserted into the parent internal page pointing to the new leaf.
4. If the parent is also full, the split propagates upward.
5. If the root splits, a new root is allocated and the tree height increases by one.

Page splits require multiple WAL records and careful locking — the parent must be locked while the split completes. This is the reason bulk-loading data via `COPY` is faster than individual `INSERT` statements: `COPY` can use the `pg_dump`-style fast-path which sorts data and builds B-tree pages bottom-up, avoiding splits entirely.

---

### MVCC (Multi-Version Concurrency Control)

#### The Concurrency Problem

Without MVCC, allowing concurrent reads and writes requires either:
1. **Read locks**: readers block writers, writers block readers. Throughput collapses under any mixed workload.
2. **No isolation**: readers see partial writes from in-flight transactions. Results are incorrect.

PostgreSQL's answer is MVCC: instead of updating a row in place, it creates a new version of the row and keeps the old version alive until no active transaction can see it. Readers always see a consistent snapshot of the database as of a point in time, with zero locking against writers.

#### Heap Tuple Headers

Every row in a PostgreSQL heap page has a `HeapTupleHeader` prepended to the actual column data. The two fields central to MVCC are:

| Field | Type | Meaning |
|---|---|---|
| `xmin` | TransactionId (uint32) | XID of the transaction that inserted this row version |
| `xmax` | TransactionId (uint32) | XID of the transaction that deleted or updated this row version (0 if still live) |
| `ctid` | ItemPointer | Physical location of the current (newest) version of this row |

An UPDATE in PostgreSQL is not an in-place modification. It is an INSERT of a new row version (with the new column values and `xmin` = current XID) plus a "deletion" of the old version (setting `xmax` = current XID on the old tuple). Both versions exist in the heap simultaneously.

#### Tuple Version Example

```
Transaction 100: INSERT INTO t VALUES ('alice');
Transaction 101: UPDATE t SET name = 'alice_updated' WHERE name = 'alice';
Transaction 102: SELECT * FROM t;  -- concurrent read
```

After these operations, the heap page for table `t` contains:

```
Slot 1:  xmin=100, xmax=101, name='alice'         ← old version, deleted by txn 101
Slot 2:  xmin=101, xmax=0,   name='alice_updated' ← new version, still live
```

- A transaction with snapshot taken **before** txn 101 commits sees slot 1 (xmin=100 committed, xmax=101 not yet committed from that snapshot's perspective).
- A transaction with snapshot taken **after** txn 101 commits sees slot 2 (xmin=101 committed, xmax=0 means not deleted).
- Transaction 102, if its snapshot is taken mid-update, sees slot 1 still — it is invisible to the write in progress.

#### Visibility Rules

A tuple with (`xmin`, `xmax`) is visible to a snapshot `S` if:
1. `xmin` committed before `S` was taken (xmin is in the snapshot's "committed" set), **and**
2. Either `xmax = 0` (not deleted) or `xmax` had NOT committed before `S` was taken.

The commit status of each XID is stored in **pg_xact** (formerly pg_clog): a bitmask file where each 2-bit pair records whether a transaction ID committed, aborted, or is in-progress. Checking visibility requires reading this bitmask, which is why pg_xact pages are themselves cached in shared memory.

#### Snapshots and Snapshot Isolation

When a backend begins a transaction (or, in READ COMMITTED mode, each statement), it takes a **snapshot**: a record of:
- `xmin`: the oldest XID still active at snapshot time.
- `xmax`: the next XID to be assigned (no transaction with this or higher XID has started yet).
- `xip`: the list of XIDs that were active (in-progress) at snapshot time.

A transaction's XID is "visible as committed" to a snapshot if the XID < snapshot.xmin (committed before the oldest active transaction), or if the XID is not in `xip` and is < snapshot.xmax (committed and finished before the snapshot was taken).

This mechanism provides **Snapshot Isolation**: each transaction sees the database as it was at a fixed point, even as other transactions commit. PostgreSQL's REPEATABLE READ and SERIALIZABLE isolation levels use per-transaction snapshots. READ COMMITTED uses per-statement snapshots, which allows seeing committed changes from other transactions mid-query.

#### Why MVCC Improves Concurrency

The critical property is that **readers never acquire row locks**. A `SELECT` on a large table does not block any concurrent `INSERT` or `UPDATE`. The writer does not need to wait for readers to finish. This eliminates the read-write lock contention that dominates throughput in pessimistic locking systems. The cost — keeping dead tuple versions alive in the heap — is paid by VACUUM.

---

### VACUUM

#### Dead Tuples and Storage Bloat

Every UPDATE leaves a dead tuple (the old version with `xmax` set). Every DELETE leaves a dead tuple. These tuples remain in heap pages because they might still be visible to some active snapshot. Once no active transaction can see a dead tuple, it is eligible for reclamation — but it stays in the page until VACUUM processes that page.

Without VACUUM:
1. Heap pages fill with dead tuples. New inserts must find space elsewhere, causing the heap to grow indefinitely even if the logical row count stays constant.
2. Sequential scans read every page including those full of dead tuples, wasting I/O.
3. The transaction ID counter wraps around at 2^32 (about 4 billion transactions). Without periodic freezing of old XIDs, PostgreSQL would be unable to determine visibility correctly — a condition called **XID wraparound**, which PostgreSQL's autovacuum aggressively prevents.

#### What VACUUM Does

`VACUUM` on a table scans heap pages and, for each dead tuple that is no longer visible to any active transaction:
1. Clears the tuple's storage, making the space available for future inserts on the same page (but does NOT return pages to the OS — for that, `VACUUM FULL` is required, which rewrites the entire table).
2. Updates the **Free Space Map (FSM)** so the buffer manager knows which pages have space for future inserts.
3. Updates the **Visibility Map (VM)**, marking pages where all tuples are known-visible. These pages can be skipped by future VACUUM runs and enable **index-only scans** (where PostgreSQL can skip the heap fetch because it knows all tuples on the page are visible).
4. **Freezes** old tuples: replaces `xmin` with a special `FrozenTransactionId` value, marking them permanently visible and safe from XID wraparound.

#### Autovacuum

The **autovacuum daemon** monitors `pg_stat_user_tables` and triggers VACUUM when the number of dead tuples in a table exceeds a threshold:

```
autovacuum threshold = autovacuum_vacuum_threshold + autovacuum_vacuum_scale_factor * reltuples
```

Default: threshold = 50 + 0.2 × table_size. A table with 1 million rows triggers autovacuum after ~200,050 dead tuples accumulate. Autovacuum also triggers ANALYZE (statistics collection) separately.

The reason autovacuum is a background daemon rather than something that happens inline is that VACUUM is I/O-intensive and should not block foreground query latency. However, autovacuum can be delayed or fall behind on very high write-throughput tables, leading to bloat. Tuning autovacuum aggressiveness (`autovacuum_cost_delay`, scale factors, worker count) is a real operational concern in write-heavy PostgreSQL deployments.

---

### Write-Ahead Logging (WAL)

#### The Durability Problem

Consider what happens if PostgreSQL modifies a page in the buffer pool (in-memory) and the process crashes before writing that page to disk. On restart, the on-disk page is stale — it reflects the state before the committed transaction. The committed data is lost. This violates durability, one of the ACID guarantees.

The naive fix — write every modified page to disk synchronously before acknowledging a commit — is correct but unacceptably slow. A single transaction might dirty dozens of pages scattered across the heap and multiple index files. Writing each with an `fsync()` would serialize all commit latency to random I/O, which could be hundreds of milliseconds per transaction.

#### WAL Design

PostgreSQL's solution is to write a compact, sequential log record describing what changed **before** writing the modified data page. This log is the Write-Ahead Log (WAL), stored in `pg_wal/` as a sequence of 16MB segment files.

The WAL record for a change is much smaller than the full page:
- For an INSERT into a heap page: the new tuple data + its location.
- For a B-Tree insert: the key + page offset.
- For a page split: the new page contents + changes to the parent.

Writing a small sequential WAL record is fast. The data page write can be deferred — it may happen seconds later via bgwriter or checkpointer. On commit, PostgreSQL only needs to flush the WAL buffer to disk (via `fsync` or `fdatasync`), not the data pages.

```
Timeline of a write transaction:

  BEGIN
    │
    ├─ Modify page in buffer pool (in-memory)
    ├─ Write WAL record to WAL buffer (in-memory)
    │
  COMMIT
    │
    ├─ Flush WAL buffer to disk (fsync on pg_wal segment)  ← durable point
    │
    └─ Return success to client
    
  (data page may still be dirty in buffer pool for seconds)
```

The invariant is: **a data page is never written to disk unless all WAL records describing its changes have already been written to disk.** This is enforced in the buffer manager — before evicting a dirty buffer, it checks that the buffer's LSN (Log Sequence Number, the WAL position of the last change to that page) has been flushed.

#### Checkpoints

WAL segments accumulate indefinitely unless the database periodically **checkpoints**: flushes all dirty buffer pool pages to disk and records the checkpoint LSN in the WAL. After a checkpoint, all WAL segments older than that LSN are no longer needed for crash recovery and can be recycled.

On crash restart, PostgreSQL's startup process:
1. Reads the control file to find the last checkpoint LSN.
2. Replays all WAL records from the checkpoint LSN forward, re-applying every change to data pages.
3. Aborts any transaction that was in-flight at crash time (they have no COMMIT record in the WAL).
4. The database is now consistent. Normal operation resumes.

**Group commit** is a latency-throughput trade-off: if many transactions commit within a short window, PostgreSQL can batch their WAL flushes into a single `fsync`, amortizing the I/O cost. `synchronous_commit = off` allows PostgreSQL to acknowledge commits before WAL is flushed — trading durability risk (up to ~1 `wal_writer_delay` of committed data loss on crash) for much lower commit latency.

---

## 4. Design Trade-Offs

### MVCC

| Advantage | Disadvantage |
|---|---|
| Readers never block writers | Dead tuple accumulation requires VACUUM |
| Writers never block readers | XID wraparound requires periodic freezing |
| Snapshot isolation is cheap | Long-running transactions prevent VACUUM, causing bloat |
| Predictable read latency | Index entries for dead tuples waste space; HOT updates mitigate this |

The decision to use MVCC over lock-based concurrency was made because read-write contention is the dominant bottleneck in OLTP workloads. The cost of VACUUM is paid infrequently and in the background. The cost of read-write locks would be paid on every query.

### WAL Overhead vs. Durability

| Concern | Detail |
|---|---|
| WAL write amplification | Each change is written twice: once to WAL, once (eventually) to the data file |
| WAL I/O is sequential | WAL writes are always appended — sequential I/O is 10–100x faster than random I/O on spinning disks |
| `fsync` latency | Every commit requires at least one `fsync` unless group commit batches it |
| `synchronous_commit=off` | Eliminates per-commit fsync but risks up to ~200ms of committed data on crash |
| `full_page_writes=on` | After a checkpoint, the first modification to any page writes the entire page to WAL, doubling WAL volume but enabling recovery from partial page writes (torn pages) |

WAL write amplification is real but acceptable because: (a) sequential WAL I/O is much cheaper than random data file I/O, (b) data file writes are deferred and batched, and (c) the WAL stream also serves replication — the same records written for durability are shipped to standby servers.

### Buffer Manager Trade-offs

| Trade-off | Detail |
|---|---|
| `shared_buffers` too small | Frequent buffer misses; high disk I/O even for hot data |
| `shared_buffers` too large | OS page cache is starved; PostgreSQL and OS double-cache the same pages |
| Clock-sweep is approximate | A hot page that was recently accessed but then not accessed briefly may be evicted |
| No NUMA awareness | On multi-socket servers, backends on one socket may access shared memory on the other socket's NUMA node — higher latency |

### Index Maintenance Costs

| Scenario | Cost |
|---|---|
| INSERT into indexed table | B-tree insert per index; potential page split |
| UPDATE of indexed column | Old index entry remains (dead), new entry inserted; VACUUM cleans dead entries |
| UPDATE of non-indexed column | If HOT conditions apply, no index entry is written (Heap Only Tuple) |
| VACUUM after heavy update | Must scan all index pages to remove dead entries (index vacuum) |

The HOT (Heap Only Tuple) optimization is significant: if a row is updated, the new tuple fits on the same heap page, and no indexed column changed, PostgreSQL does not insert a new index entry. The old index entry points to a chain of heap tuples on the same page. This reduces index bloat and avoids index writes entirely for updates to non-key columns.

### VACUUM Benefits and Drawbacks

| Benefit | Drawback |
|---|---|
| Reclaims dead tuple space | Requires I/O proportional to table size |
| Enables index-only scans via visibility map | Can be disruptive at scale if autovacuum falls behind |
| Prevents XID wraparound | `VACUUM FULL` locks the table and rewrites it — high downtime cost |
| Updates planner statistics (ANALYZE) | Aggressive autovacuum competes with foreground queries for I/O |

---

## 5. Experiments / Observations

### EXPLAIN ANALYZE Example

Consider a realistic query joining two tables:

```sql
EXPLAIN (ANALYZE, BUFFERS, FORMAT TEXT)
SELECT o.id, o.amount, c.name
FROM orders o
JOIN customers c ON o.customer_id = c.id
WHERE c.region = 'APAC'
  AND o.created_at >= '2024-01-01';
```

A plausible execution plan on a medium-sized dataset:

```
Hash Join  (cost=1240.50..8900.20 rows=3200 width=36)
           (actual time=18.4..142.7 rows=3147 loops=1)
  Hash Cond: (o.customer_id = c.id)
  Buffers: shared hit=2840 read=190
  ->  Index Scan using orders_created_at_idx on orders o
        (cost=0.43..6200.10 rows=48000 width=20)
        (actual time=0.1..98.3 rows=47918 loops=1)
        Index Cond: (created_at >= '2024-01-01')
        Buffers: shared hit=2100 read=180
  ->  Hash  (cost=910.00..910.00 rows=2640 width=24)
            (actual time=16.9..16.9 rows=2618 loops=1)
        Buckets: 4096  Batches: 1  Memory Usage: 198kB
        Buffers: shared hit=740 read=10
        ->  Seq Scan on customers c
              (cost=0.00..910.00 rows=2640 width=24)
              (actual time=0.05..14.2 rows=2618 loops=1)
              Filter: (region = 'APAC')
              Rows Removed by Filter: 7382
              Buffers: shared hit=740 read=10
```

**Reading the plan:**

- The planner chose **Hash Join** over Nested Loop or Merge Join. This is the right call when neither input is small enough to fit in a single-pass nested loop and neither is pre-sorted on the join key. Hash Join builds a hash table on the smaller input (`customers` filtered to APAC, ~2600 rows), then probes it for each row from the larger input (`orders`).

- The `orders` table is accessed via an **Index Scan** on `created_at`. The planner estimated 48,000 rows; actual was 47,918 — excellent cardinality estimation because `created_at` has a histogram in `pg_statistic`.

- `customers` uses a **Seq Scan** despite having 10,000 rows. The planner decided a full table scan was cheaper than using an index on `region` because `APAC` represents ~26% of customers — a fraction high enough that random heap fetches via an index scan would cost more than reading the heap sequentially.

- `Buffers: shared hit=2840 read=190` shows that 190 pages were not in the buffer pool and required disk reads. The 2840 hits required no disk I/O. On repeated execution, `read` would drop to near zero as pages warm up.

- `actual time` vs `cost`: The planner's cost estimates are in abstract units (proportional to page reads), not milliseconds. The ratio between estimated and actual row counts indicates cardinality estimation quality. A large discrepancy (e.g., estimated 100, actual 50,000) would indicate stale statistics or a predicate with a correlated column not captured in the statistics.

---

### Statistics and Cardinality Estimation

PostgreSQL stores per-column statistics in `pg_statistic`, populated by `ANALYZE`. The planner uses these statistics to estimate how many rows each plan node will produce — **cardinality estimation** — which drives join order selection, join algorithm selection, and index-vs-seqscan decisions.

```sql
-- View statistics for a column
SELECT attname, n_distinct, correlation
FROM pg_stats
WHERE tablename = 'orders' AND attname = 'customer_id';
```

Key statistics stored:
- `n_distinct`: estimated number of distinct values. Negative means a fraction of total rows (e.g., -0.1 means ~10% of rows are distinct).
- `most_common_vals` / `most_common_freqs`: top-N values and their frequencies, as arrays. Used for equality predicate estimation.
- `histogram_bounds`: bucket boundaries for range predicate estimation. A predicate `created_at >= '2024-01-01'` is estimated by computing what fraction of the histogram lies above that bound.
- `correlation`: how well physical row order correlates with column sort order. Correlation = 1.0 means rows are physically sorted by this column. The planner uses this to decide whether an index scan (which does random heap fetches) will outperform a sequential scan — high correlation makes index scans cheaper because heap fetches cluster in sequential pages.

**Why statistics matter:** The planner is a cost-based optimizer. It generates multiple candidate plans and picks the cheapest by estimated cost. If cardinality estimates are wrong, the planner may select a slow plan that looked cheap on paper. For example: underestimating the number of matching rows leads the planner to choose Nested Loop when Hash Join would be far faster, because Nested Loop cost scales quadratically with input size.

`ANALYZE` collects statistics by sampling a fraction of the table (controlled by `default_statistics_target`, default 100, which controls the number of histogram buckets and most-common-values entries). For highly skewed columns, increasing `statistics_target` for that column improves estimate quality at the cost of more analysis time and larger `pg_statistic` entries.

---

## 6. Key Learnings

### How Pages Move Through PostgreSQL

The lifecycle of a heap page during a write transaction:
1. Backend calls buffer manager for the target page.
2. Buffer manager finds it in the pool (hit) or reads from disk into a free buffer slot (miss).
3. Backend modifies the page in-memory (the buffer), writes a WAL record to the WAL buffer.
4. At commit, WAL buffer is flushed to disk (`pg_wal/`). Data page remains dirty in the buffer pool.
5. bgwriter or checkpointer eventually writes the dirty page to its data file on disk.
6. Until the dirty page is written, crash recovery would replay the WAL record to reconstruct the change.

This two-phase write (WAL first, data file second) is what makes PostgreSQL both fast (deferred data I/O) and durable (WAL on disk before commit acknowledgment).

### How MVCC Works Internally

MVCC uses per-tuple `xmin`/`xmax` fields and per-transaction snapshots to implement isolation without read locks. Reads apply a visibility function to every tuple: is `xmin` committed before my snapshot? Is `xmax` either 0 or committed after my snapshot? If yes to both, the tuple is visible. Writers append new tuple versions; they do not modify existing ones. This means concurrent reads and writes never conflict at the row level.

### Why WAL Exists

WAL exists because durable writes and fast commits are otherwise mutually exclusive. Writing data pages synchronously on every commit is too slow (random I/O, many pages per transaction). Writing nothing and acknowledging commits is fast but loses data on crash. WAL threads the needle: write a small, sequential, easily-synced log record for every change, defer the data page write, and reconstruct any missing data page writes from the log on recovery. Sequential log I/O is orders of magnitude faster than random data file I/O.

### Why VACUUM Is Required

VACUUM is the structural consequence of the MVCC design choice. MVCC never modifies rows in place — it accumulates versions. Old versions must be cleaned up eventually or the heap grows without bound. VACUUM is the process that does that cleanup. It cannot be avoided; it can only be tuned. The alternative — not using MVCC — would require read-write locks and dramatically lower concurrent throughput.

### Architectural Lessons

- **Shared memory is a coordination surface.** Every backend accesses the same buffer pool and WAL buffers, which enables resource sharing but requires careful locking. LWLocks on buffer pool hash chains can become contention points at very high concurrency — this is one reason PostgreSQL connection count is bounded in practice.
- **Sequential I/O is always preferred where possible.** WAL writes are sequential. B-tree pages at the upper levels stay cache-hot. Seq Scan outperforms index scan when a large fraction of the table is selected. PostgreSQL's I/O patterns are designed around this preference at every layer.
- **Every design decision has a cost that surfaces elsewhere.** MVCC improves concurrency but requires VACUUM. WAL improves durability without sacrificing write throughput but doubles I/O volume. Deferred dirty page writes improve commit latency but require crash recovery infrastructure. Understanding PostgreSQL's internals means understanding these chains of consequence.

---

## References

1. Stonebraker, M., & Rowe, L. (1987). *The design of POSTGRES*. Proceedings of ACM SIGMOD, pp. 340–355.
2. PostgreSQL Global Development Group. *System Catalog* — pg_statistic, pg_stat_user_tables. https://www.postgresql.org/docs/current/catalogs.html
3. PostgreSQL Global Development Group. *Storage File Layout*. https://www.postgresql.org/docs/current/storage-file-layout.html
4. PostgreSQL Global Development Group. *WAL Internals*. https://www.postgresql.org/docs/current/wal-internals.html
5. PostgreSQL Global Development Group. *MVCC*. https://www.postgresql.org/docs/current/mvcc.html
6. PostgreSQL Global Development Group. *Routine Vacuuming*. https://www.postgresql.org/docs/current/routine-vacuuming.html
7. Momjian, B. (2001). *PostgreSQL: Introduction and Concepts*. Addison-Wesley. (Chapter on MVCC and locking)
8. PostgreSQL source: `src/backend/storage/buffer/bufmgr.c` — buffer manager implementation.
9. PostgreSQL source: `src/backend/access/nbtree/` — B-tree index implementation.
10. Hellerstein, J., Stonebraker, M., & Hamilton, J. (2007). *Architecture of a Database System*. Foundations and Trends in Databases, Vol. 1, No. 2.
