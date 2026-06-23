# PostgreSQL Internal Architecture

## 1. Problem Background

Handling concurrent multi-user database access requires solving several hard engineering problems. The database must cache data in limited RAM, index rows for fast access, prevent readers and writers from blocking each other, and guarantee that data is never lost in a crash. PostgreSQL uses a set of specialized internal subsystems to solve these problems while maintaining data integrity and performance.

---

## 2. Architecture Overview

PostgreSQL's internal architecture is structured in layers, handling a query from parser down to the storage media:

```text
+-----------------------+
|  Client Application   |
+-----------------------+
            | SQL Query
            v
+-----------------------+
|  Query Execution &    |
|        Planner        |
+-----------------------+
            | Execution Plan
            v
+-----------------------+
|  Concurrency Control  |
|       (MVCC)          |
+-----------------------+
            | Tuple Visibility
            v
+-----------------------+      +-----------------------+
|    Buffer Manager     | ---> |   Write-Ahead Log     |
|   (Shared Buffers)    |      |     (WAL files)       |
+-----------------------+      +-----------------------+
            |                             |
            v                             v
+-----------------------+       +-------------------+
| Heap & B-Tree Storage | ----> |   Physical Disk   |
+-----------------------+       +-------------------+
```

*   **Query Planner**: Receives the query, estimates page costs, and decides whether to use a sequential scan or index lookup.
*   **Concurrency Control**: Applies MVCC rules to show the transaction a consistent snapshot of data.
*   **Buffer Manager**: Handles caching of 8KB data blocks in Shared Buffers.
*   **Write-Ahead Log (WAL)**: Records changes sequentially for durability before they are flushed to the main data files.

---

## 3. Internal Design

### 3.1 Buffer Manager
The Buffer Manager maintains frequently accessed data pages in Shared Buffers to minimize disk I/O. Instead of a strict Least Recently Used (LRU) eviction policy, PostgreSQL uses the **Clock Sweep algorithm**:
*   A pointer sweeps through buffer descriptors.
*   If a page has a usage count greater than zero, the count is decremented.
*   If the usage count is zero and the page is unpinned, it becomes a candidate for eviction.
*   This design avoids lock contention on a global LRU list when multiple client backends access the cache.

### 3.2 B-Tree Implementation
PostgreSQL uses high-concurrency B-Tree indexes (based on Lehman & Yao's algorithm) for fast lookups:
*   The index is a balanced tree of index pages.
*   Interior nodes guide search paths, while leaf nodes contain index keys and **Tuple IDs (TIDs)**.
*   TIDs consist of a block number and an offset, pointing directly to the row's location in the heap files.

### 3.3 Multi-Version Concurrency Control (MVCC)
PostgreSQL implements MVCC by retaining multiple versions of a row (tuple) in the heap:
*   Every tuple header contains two transaction IDs: `xmin` (creating transaction) and `xmax` (deleting/updating transaction).
*   When a transaction reads, it receives a snapshot containing active transaction IDs. It determines tuple visibility by comparing `xmin` and `xmax` against this snapshot.
*   This allows readers to fetch data without blocking writers, and writers to modify rows without blocking readers.

### 3.4 Write-Ahead Logging (WAL)
To guarantee durability without flushing large 8KB data pages on every transaction commit, PostgreSQL uses WAL:
*   Changes are written sequentially to WAL buffers and flushed to disk (`pg_wal/`) when a transaction commits.
*   In a crash, PostgreSQL reads WAL files starting from the last **Checkpoint** and replays them to rebuild memory state.

---

## 4. Design Trade-Offs

*   **MVCC Bloat vs. Concurrency**: Keeping old tuple versions in the heap avoids reader-writer locks, but causes storage bloat. To reclaim this space, a background **Vacuum** process must constantly scan and clean up dead tuples, which adds CPU and I/O overhead.
*   **Clock Sweep vs. LRU Cache**: The Clock Sweep algorithm sacrifices cache accuracy compared to a strict LRU cache, but it completely eliminates the CPU bottleneck of updating a linked list on every single read request.
*   **WAL vs. Simple Direct Writes**: Writing WAL records sequentially is very fast but increases recovery time. During a crash restart, the system must replay WAL records, delaying startup.

---

## 5. Experiments & Observations

### 5.1 Estimating Caching with Query Plans
We can use `EXPLAIN (ANALYZE, BUFFERS)` to observe the Buffer Manager:

```sql
EXPLAIN (ANALYZE, BUFFERS) 
SELECT * FROM orders WHERE order_id = 9520;
```

**Observation:**
The output shows `Buffers: shared hit=3 read=0`. This indicates that the query planner traversed a B-Tree index using 3 shared buffers that were already cached in RAM, avoiding disk reads entirely.

### 5.2 Vacuum Monitoring
To observe MVCC cleanup, we can check active vacuums:

```sql
SELECT phase, heap_blks_scanned, heap_blks_vacuumed 
FROM pg_stat_progress_vacuum;
```

**Observation:**
This shows the progress of the autovacuum worker as it cleans up dead tuples and returns free space to the page layout.

---

## 6. Key Learnings

1.  **System-Wide Interconnection**: Subsystems in PostgreSQL do not work in isolation. MVCC requires a vacuum process, WAL is required to protect the buffer manager, and the query planner relies on page statistics to pick scan paths.
2.  **Locks vs. Versioning**: MVCC trades disk space and background cleaning complexity in order to eliminate reader-writer lock bottlenecks.
3.  **Algorithmic Trade-offs**: Clock sweep is preferred over LRU because cache eviction in databases is bounded by CPU lock contention rather than just cache hit ratios.
