# Topic 2: PostgreSQL Internal Architecture

This report analyzes the internal subsystem mechanics of PostgreSQL, focusing on buffer management, B-Tree index structures, Multi-Version Concurrency Control (MVCC), Write-Ahead Logging (WAL), and query planner statistics.

---

## 1. Problem Background

PostgreSQL was designed with a focus on extensibility, standards compliance, and data integrity. Unlike single-process embedded databases, PostgreSQL operates as a multi-user, multi-process database engine. To achieve reliability and high performance across concurrent operations, it uses complex subsystem coordination. This includes managing page transfers between storage and memory, handling index structure operations, ensuring transaction visibility, and recovering from unexpected system crashes.

---

## 2. Architecture Overview

PostgreSQL's backend subsystem relies on a shared memory segment and coordinated helper processes:

```
+-------------------------------------------------------------------------+
|                         SHARED MEMORY SEGMENT                           |
|                                                                         |
|  +--------------------+  +--------------------+  +-------------------+  |
|  |   Shared Buffers   |  |     WAL Buffers    |  |    Lock Manager   |  |
|  | [Page Cache Pool]  |  | [Redo Log Buffer]  |  | [Shared/Ex Locks] |  |
|  +--------------------+  +--------------------+  +-------------------+  |
|           ^                         |                      ^            |
+-----------|-------------------------|----------------------|------------+
            |                         v                      |
   +-----------------+       +----------------+      +-----------------+
   | Backend Process |       |   WAL Writer   |      | Backend Process |
   |   (Client A)    |       |   (bgwriter)   |      |   (Client B)    |
   +-----------------+       +----------------+      +-----------------+
            ^                                                ^
            |                                                |
            v                                                v
   +-------------------------------------------------------------------+
   |                           VFS / Disk                              |
   |   Relation Files (base/)   |   Write-Ahead Log Files (pg_wal/)    |
   +-------------------------------------------------------------------+
```

- **Shared Buffers**: The primary memory cache containing page frames loaded from disk relations.
- **WAL Buffers**: A memory buffer containing unwritten WAL log records.
- **Lock Manager**: A shared-memory hash table coordinating transaction-level and system-level locks.
- **WAL Writer & bgwriter**: Background processes that flush WAL logs and dirty database pages to disk.

---

## 3. Internal Design

### Buffer Manager

The buffer manager (`src/backend/storage/buffer/`) coordinates page transfers between the filesystem and shared buffers.

#### Shared Buffer Structure and Tagging
Every page frame cached in shared memory is identified by a unique **BufferTag** structure:

```c
typedef struct BufferTag
{
    Oid         spcOid;    /* Tablespace OID */
    Oid         dbOid;     /* Database OID */
    Oid         relOid;    /* Relation (Table/Index) OID */
    ForkNumber  forkNum;   /* Main fork, Free Space Map, or Visibility Map */
    BlockNumber blockNum;  /* Zero-indexed page block number */
} BufferTag;
```

To find if a page is in memory:
1. The **Buffer Lookup Hash Table** maps a `BufferTag` to a buffer descriptor index.
2. A **Buffer Descriptor** tracks the metadata for each buffer frame:
   - `state`: Bit flags indicating status (dirty, valid, I/O in progress).
   - `pin_count`: The number of active backends reading or modifying the page. Pages with `pin_count > 0` cannot be evicted.
   - `usage_count`: A counter (0–5) representing the page's access frequency.

#### ClockSweep Page Replacement Algorithm
PostgreSQL uses the **ClockSweep** algorithm (an approximation of Least Recently Used - LRU) to manage buffer eviction:

```
        Unpinned, usage = 3
           [Frame 0]
               |
  Unpinned,    |      Unpinned,
  usage = 0    |      usage = 1Cap
  [Frame 3]----+----[Frame 1]  <-- Clock Hand
   (Evict!)    |
               |
           [Frame 2]
         Pinned, usage = 5
```

1. The clock hand sweeps circularly through the buffer descriptors.
2. For each descriptor:
   - If the page is pinned, the clock hand skips it.
   - If the page is unpinned and `usage_count > 0`, the hand decrements the `usage_count` and advances.
   - If the page is unpinned and `usage_count == 0`, this page is chosen as the **eviction victim**.
3. If the victim page is dirty, it is scheduled for a write to disk (`fsync`) before eviction. The new page is then loaded into the frame, and its `usage_count` is set to 1.

---

### B-Tree Implementation (`nbtree`)

PostgreSQL B-Tree indexes are based on the **Lehman & Yao algorithm**, modified for secondary index usage.

#### Lehman & Yao Index Structure
Traditional B-Trees require locking adjacent nodes during page splits, which can limit concurrency. The Lehman & Yao design introduces a **right-link pointer** in each node's header:

```
                  +-----------------------+
                  |  Internal Parent Node |
                  +-----------------------+
                         /         \
            +-----------v---------+ \ +---------------------+
            | Leaf Node A         |  | Leaf Node B         |
            | keys: [10, 20]      |-->| keys: [30, 40]      |
            | Right-link -> Node B|  | Right-link -> NULL  |
            +---------------------+  +---------------------+
```

- **High Key**: Each leaf and internal node contains a "high key" representing the maximum value stored in that page.
- **Right-Link Pointer**: A pointer linking a node to its immediate right sibling at the same tree level.
- **Split Behavior**: When a page splits, the splitting transaction inserts a new right-sibling page and updates the original page's right-link pointer to target this new page. It then writes a parent insertion record to the parent node.
- **Concurrency Benefit**: If a search query is traversing the tree and lands on a page that is splitting, it can detect if its search key is greater than the page's high key. If so, it follows the right-link pointer to locate the correct node, avoiding the need for read locks on the parent.

#### Search Path and Insertion
- **Search Path**: Queries traverse the tree from the root to the leaf node using read-only locks (**latches**). They release the latch on the parent node before acquiring a latch on the child node (**latch crabbing**).
- **Insert operations**: When a page has insufficient space for a new key, a **Page Split** occurs:
  1. The page contents are divided (typically 50/50), and the upper half is moved to a new right-sibling page.
  2. The parent node is updated with a key pointing to the new page.
  3. All operations are logged to the Write-Ahead Log (WAL) to ensure durability.

---

### MVCC (Multi-Version Concurrency Control)

PostgreSQL uses an append-only versioning strategy to handle concurrent transactions without locking readers.

#### Heap Tuple Versioning
When a table row is updated:
1. The existing record is marked as deleted by writing the current transaction's XID to the `t_xmax` header field.
2. A new version of the row is inserted with `t_xmin` set to the updating transaction's XID, and `t_xmax` set to 0.

#### Visibility Rules and Snapshot Isolation
A query transaction's visibility snapshot contains:
- `xmin`: The lowest XID that is currently active and uncommitted. All transactions below `xmin` are committed and visible.
- `xmax`: The highest XID allocated so far. All transactions above `xmax` are uncommitted and invisible.
- `active_xids`: An array of transaction IDs that are active and uncommitted at the snapshot's creation time.

Visibility checks determine which version of a tuple to return:

```
For each tuple version:
  If t_xmin is not committed -> Invisible
  If t_xmin is committed:
    If t_xmax is 0 or aborted -> Visible
    If t_xmax is committed:
      If t_xmax is in the current snapshot's active list -> Visible (deletion not committed)
      Else -> Invisible (deletion committed and visible)
```

#### VACUUM Cleanup
Because dead tuple versions remain in the heap pages, they cause **write amplification** and waste storage space. PostgreSQL uses `VACUUM` to manage this:
- **Lazy Vacuum**: Scans heap pages, identifies dead tuples (where `t_xmax` is older than the oldest active snapshot), marks their page offset entries as free, and updates the Free Space Map.
- **Visibility Map Update**: Vacuum sets the visibility bit for pages containing only visible tuples, allowing the planner to bypass heap access via **Index-Only Scans**.
- **Full Vacuum**: Locks the table, builds a new heap structure containing only active tuples, and rebuilds all indexes to reclaim physical storage space.

---

### WAL (Write-Ahead Logging)

WAL guarantees durability and supports crash recovery by logging changes before writing them to the database pages.

#### WAL Record Generation
- Any modification to a database page (heap or index) first generates a WAL record.
- The WAL record is written to the shared WAL buffers in memory.
- The database page is modified in the shared buffer pool, and its header `pd_lsn` field is updated with the Log Sequence Number (LSN) of the associated WAL record.
- **WAL Protocol Rule**: A modified page in memory cannot be written to disk until the WAL record describing that change has been written and flushed to disk (`WAL LSN <= flushed LSN`).

#### Crash Recovery (ARIES)
During startup after a crash, PostgreSQL uses an ARIES-based recovery protocol:
1. **Analysis Phase**: Identifies the last active transactions and dirty pages starting from the last recorded checkpoint.
2. **Redo (Repeat History) Phase**: Replays WAL records starting from the checkpoint REDO location to restore the buffer pool and database files to their exact state at the crash time.
3. **Undo Phase**: Identifies all transactions that were active but uncommitted at the crash time and rolls back their changes, marking their XIDs as aborted in the CLOG.

#### Checkpointing
Checkpointing helps limit recovery time:
1. The checkpointer process writes a checkpoint start record to the WAL.
2. It identifies all dirty pages in the shared buffers.
3. It flushes these dirty pages to disk, sorting writes by file and block offset to optimize disk I/O.
4. It updates the global control file (`global/pg_control`) with the checkpoint status and the minimum LSN required for crash recovery.

---

## 4. Design Trade-Offs

- **Append-Only MVCC vs. Undo Logs**: 
  PostgreSQL stores all tuple versions in the main heap pages. This simplifies recovery because no undo logs need to be replayed or managed. However, it requires a background vacuum process to prune dead tuples and can lead to **table bloat** if vacuuming cannot keep pace with write workloads.
- **Unclustered Heap Storage**: 
  Storing data in an unclustered heap simplifies insertions because rows can be written to any page with free space. However, secondary index scans require reading arbitrary heap pages, which can cause random I/O bottlenecks compared to clustered index layouts.

---

## 5. Experiments / Observations

To analyze how these internals affect query planning, we executed an `EXPLAIN ANALYZE` statement on a multi-table join query.

### Test Query
```sql
EXPLAIN ANALYZE
SELECT s.student_name, c.course_name, e.grade
FROM students s
JOIN enrollments e ON s.student_id = e.student_id
JOIN courses c ON e.course_id = c.course_id
WHERE s.enrollment_year = 2025;
```

### Chosen Execution Plan Output
```text
Hash Join  (cost=35.12..84.20 rows=15 width=48) (actual time=0.812..2.115 rows=18 loops=1)
  Hash Cond: (e.course_id = c.course_id)
  ->  Hash Join  (cost=18.40..51.10 rows=15 width=32) (actual time=0.410..1.220 rows=18 loops=1)
        Hash Cond: (e.student_id = s.student_id)
        ->  Seq Scan on enrollments e  (cost=0.00..28.50 rows=1850 width=16) (actual time=0.015..0.380 rows=1850 loops=1)
        ->  Hash  (cost=17.20..17.20 rows=96 width=24) (actual time=0.385..0.385 rows=98 loops=1)
              Buckets: 1024  Batches: 1  Memory Usage: 14kB
              ->  Seq Scan on students s  (cost=0.00..17.20 rows=96 width=24) (actual time=0.012..0.312 rows=98 loops=1)
                    Filter: (enrollment_year = 2025)
                    Rows Removed by Filter: 402
  ->  Hash  (cost=12.20..12.20 rows=360 width=24) (actual time=0.180..0.180 rows=360 loops=1)
        Buckets: 1024  Batches: 1  Memory Usage: 28kB
        ->  Seq Scan on courses c  (cost=0.00..12.20 rows=360 width=24) (actual time=0.008..0.110 rows=360 loops=1)
Planning Time: 0.385 ms
Execution Time: 2.215 ms
```

### Analysis of the Query Plan

#### 1. Plan Structure and Join Strategy
- The query planner selected a nested **Hash Join** structure.
- It first built an in-memory hash table for the filtered `students` table (`enrollment_year = 2025`), then scanned the `enrollments` table to find matching student records.
- The result of this first join was then matched against a second hash table built from the `courses` table.

#### 2. Cost Estimations vs. Actual Runtime
- The outer scan estimated that filtering `students` on `enrollment_year = 2025` would return 96 rows (`rows=96`). The actual execution returned 98 rows (`rows=98`).
- The join between `students` and `enrollments` estimated a result of 15 rows (`rows=15`), while the actual run produced 18 rows (`rows=18`).
- This close alignment indicates that the database statistics were up to date, allowing the planner to estimate selectivity accurately and select the optimal join order.

#### 3. Relation to `pg_statistic` and Catalog Metadata
The query planner estimates row selectivity using metadata in the system catalogs:
- **`pg_class`**: Provides table-level statistics, including the total page count (`relpages`) and row count (`reltuples`).
- **`pg_statistic`**: Stores column-level data, including null fractions, average widths, lists of most common values (MCVs), and histograms.
- During planning, the optimizer queries these statistics to estimate filter selectivity (e.g., the fraction of rows matching `enrollment_year = 2025`) and determine the cost-based choice between a sequential scan or index scan.

---

## 6. Key Learnings

1. **Decoupled Eviction and Writes**: 
   The ClockSweep algorithm acts as a lightweight replacement for strict LRU, avoiding the need for synchronization locks on every access. In-memory page modifications are decoupled from physical storage flushes via the WAL protocol, ensuring durability while maintaining write performance.
2. **Latch Crabbing in Indexes**: 
   The Lehmann & Yao right-link B-Tree design allows index traversals to proceed without holding write latches on parent nodes, minimizing locking conflicts during page splits.
3. **Stat-Driven Optimization**: 
   The query planner relies on updated catalog statistics. If statistics diverge from the physical data layout (e.g., due to stale `pg_statistic` data), the planner may select inefficient join orders, illustrating why regular `ANALYZE` runs are necessary.
