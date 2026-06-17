# PostgreSQL Internals: A Deep Technical Architecture Analysis

## 1. Problem Background
Traditional database architectures relying on strict two-phase locking (2PL) hit severe concurrency bottlenecks: readers block writers, and writers block readers. PostgreSQL was architected to solve this fundamental problem using a completely lock-free read path. To achieve this, it pioneered an Append-Only Multiversion Concurrency Control (MVCC) system, supported by a heavy process-per-connection model and complex shared memory management.

## 2. Architecture Overview
PostgreSQL is a multiprocess, shared-memory system. It deliberately avoids threads to ensure maximum crash isolation. The architecture is split into three layers:
1.  **Process Layer:** The `postmaster` daemon, backend worker processes, and background utility processes (bgwriter, autovacuum).
2.  **Shared Memory:** IPC mechanisms containing Shared Buffers, WAL Buffers, and the Lock Manager.
3.  **Storage Layer:** The physical heap tables, B-Tree indexes, and the Write-Ahead Log (WAL) on disk.

## 3. Shared Memory Architecture

```text
+-----------------------------------------------------------+
|                   POSTGRESQL INSTANCE                     |
|                                                           |
|  +----------------+    +----------------+                 |
|  | Backend PID 1  |    | Backend PID 2  |  (Query Exec)   |
|  +----------------+    +----------------+                 |
|          |                      |                         |
|==========|======================|=========================|
|          v      SHARED MEMORY   v                         |
|  +--------------------------------------+  +-----------+  |
|  |          Shared Buffer Pool          |  | Lock Mgr  |  |
|  +--------------------------------------+  +-----------+  |
|  +--------------------------------------+                 |
|  |              WAL Buffers             |                 |
|  +--------------------------------------+                 |
|===========================================================|
|          |                      |          +-----------+  |
|          v                      v          | BgWriter  |  |
|  +----------------+    +----------------+  +-----------+  |
|  | OS Page Cache  |    | OS Page Cache  |                 |
|  +----------------+    +----------------+                 |
+-----------------------------------------------------------+
```

## 4. Buffer Manager
PostgreSQL relies on "double-buffering". It manages its own `shared_buffers` but does not use `O_DIRECT`, meaning the OS kernel also caches the data in the OS Page Cache. The Buffer Manager is responsible for loading 8KB pages from disk into shared memory, pinning them, and marking them as "dirty" when modified.

## 5. Shared Buffers
`shared_buffers` is an array of 8KB slots. Every backend process maps this shared segment into its own virtual memory space, allowing lightning-fast data sharing without inter-process communication overhead during read access.

## 6. Buffer Replacement
When a backend needs to load a page but `shared_buffers` is full, it uses the **Clock-Sweep** algorithm. 
*   **Mechanism:** Imagine a clock hand sweeping across the buffer array. Each buffer has a usage count (0-5). If the hand hits a buffer with a count > 0, it decrements the count and moves on. If it hits 0, that buffer is evicted (and flushed to disk if dirty) to make room for the new page.

## 7. Page Reads and Writes

**Buffer Manager Flow Diagram:**
```text
[Backend Process] ---> Requests Page 42 from Relation X
       |
       v
[Buffer Manager] ----> Is Page 42 in Shared Buffers?
       |
    +--+--+
  [YES]  [NO]
    |      |
    |      v
    |    [Clock-Sweep] -> Find Victim Page -> Evict/Flush if Dirty
    |      |
    |      v
    |    [OS Cache] -> Request Page 42 (Hardware Read if not cached)
    |      |
    |      v
    |    Copy Page 42 into Shared Buffers
    |
    v
Pin Page 42 -> Read/Write -> Unpin (Mark Dirty if Written)
```

## 8. B-Tree Implementation
PostgreSQL implements the Lehman-Yao B-Tree algorithm. This allows the tree to handle concurrent inserts without locking the entire index during a page split, by utilizing horizontal "RightLinks" between leaf pages.

## 9. Index Page Layout

**B-Tree Page Diagram:**
```text
+-------------------------------------------------------------+
| Page Header (LSN, RightLink pointer to next sibling page)   |
|-------------------------------------------------------------|
| Item Pointer 1  | Offset: 8000, Size: 16                    |
| Item Pointer 2  | Offset: 7984, Size: 16                    |
| ...             |                                           |
|-------------------------------------------------------------|
|                         FREE SPACE                          |
|-------------------------------------------------------------|
| Index Tuple 2   | Key: "Smith", CTID: (Block 5, Tuple 2)    |
| Index Tuple 1   | Key: "Adams", CTID: (Block 2, Tuple 1)    |
+-------------------------------------------------------------+
```
*   `CTID` is the physical disk pointer linking the index to the exact heap data tuple.

## 10. Search Operations
A search transverses the root to the leaf. Because of MVCC, the index *only* knows if a key exists; it does not know if the key is visible to the current transaction. After finding the `CTID`, Postgres must fetch the heap tuple to check visibility rules.

## 11. Insert Operations
Inserts find the correct leaf node via binary search. If there is space, the `(Key, CTID)` tuple is inserted in sorted order.

## 12. Page Splits
If an insert encounters a full page, it splits the page in half. 
*   *Architectural Detail:* A RightLink is temporarily established from the old page to the new page. If a concurrent reader is looking for a key that just moved to the new page, the RightLink safely guides them there without requiring massive read/write locks.

## 13. MVCC Architecture
PostgreSQL does not use Undo Logs. It uses an **Append-Only** architecture. When a row is updated, a completely new row is inserted into the data page, and the old row is marked as expired. 

## 14. xmin/xmax
Every row tuple contains two hidden metadata fields:
*   `xmin`: The Transaction ID (XID) that inserted/created the row.
*   `xmax`: The XID that deleted/updated the row (0 if still valid).

## 15. Visibility Rules
To determine if a row is visible, Postgres evaluates:
1. Is `xmin` committed and older than my transaction?
2. Is `xmax` 0 (not deleted) or an uncommitted transaction (deleted in the future, so I shouldn't see it)?
*   *Reasoning:* This mathematically guarantees readers only see data that was fully committed before their query began, without acquiring a single read lock.

## 16. Snapshot Isolation

**MVCC Lifecycle Diagram:**
```text
Time ---> 
Tx 100: INSERT (id=1, val="A")  -> Tuple 1 [xmin:100, xmax:0]
Tx 101: UPDATE val="B"          -> Tuple 1 [xmin:100, xmax:101] (Dead to new readers)
                                -> Tuple 2 [xmin:101, xmax:0]   (Live to new readers)
Tx 102: SELECT (Snapshot: 101)  -> Reads Tuple 2
```

## 17. VACUUM
*   **Why VACUUM exists:** Because of Append-Only MVCC, `Tuple 1` above remains physically on disk forever, causing table bloat.
*   `VACUUM` sequentially scans tables, finds dead tuples where `xmax` is older than all currently running transactions, and marks that physical space as reusable in the Free Space Map (FSM). 

## 18. WAL Architecture
The Write-Ahead Log (WAL) guarantees durability (the 'D' in ACID). No dirty data page can be written to the heap on disk until the WAL record describing that change is `fsync`'d to the disk first.

## 19. WAL Records
A WAL record contains the physical binary diff of what changed on the 8KB page.

## 20. Checkpoints
To prevent the WAL from growing infinitely, the `checkpointer` background process periodically flushes *all* dirty pages from `shared_buffers` to the heap data files. Once completed, older WAL files can be safely deleted.

## 21. Crash Recovery

**WAL Flow Diagram (Crash Recovery):**
```text
[Crash Occurs] -> RAM is lost. 
       |
[Restart] ------> 1. Locate last valid Checkpoint in pg_control
       |
       v
2. Read WAL records sequentially from Checkpoint LSN
       |
       v
3. Apply binary changes directly to Heap/Index pages (Redo)
       |
       v
4. Database state restored to exact moment of crash
```

## 22. Query Planner
The planner turns a SQL string into an Execution Tree (Seq Scan, Hash Join, etc.). It is cost-based, heavily penalizing Random I/O (index scans) in favor of Sequential I/O (table scans) when retrieving large percentages of a table.

## 23. Statistics Collection
The planner cannot make decisions without knowing the shape of the data. The `ANALYZE` command samples the table and builds statistical histograms.

## 24. pg_statistic
This system catalog stores the Most Common Values (MCV) array, histogram bounds, and correlation metrics (how neatly ordered the physical disk is compared to the index). 

## 25. EXPLAIN ANALYZE Case Study

**Schema Creation:**
```sql
CREATE TABLE users (id SERIAL PRIMARY KEY, status VARCHAR);
CREATE TABLE orders (id SERIAL PRIMARY KEY, user_id INT, amount INT);
CREATE INDEX idx_user_status ON users(status);
```

**Query:**
```sql
EXPLAIN ANALYZE 
SELECT u.id, sum(o.amount) 
FROM users u JOIN orders o ON u.id = o.user_id 
WHERE u.status = 'ACTIVE' 
GROUP BY u.id;
```

**Execution Output:**
```text
HashAggregate  (cost=14500..15000 rows=50000 loops=1)
  -> Hash Join  (cost=5000..12000 rows=150000 loops=1)
       Hash Cond: (o.user_id = u.id)
       -> Seq Scan on orders o  (cost=0..4000 rows=500000 loops=1)
       -> Hash  (cost=2000..2000 rows=50000 loops=1)
            -> Bitmap Heap Scan on users u  (cost=100..2000 rows=50000 loops=1)
                 Recheck Cond: (status = 'ACTIVE'::text)
                 -> Bitmap Index Scan on idx_user_status
```

**Analysis:**
1.  **Planner Estimate:** The planner correctly estimated 50,000 ACTIVE users based on the `pg_statistic` MCV list. 
2.  **Architectural Decision:** Because it estimated 500,000 rows in `orders`, it correctly chose a `Seq Scan` on `orders`. Firing 500,000 random I/O index lookups would be catastrophic compared to a sequential hardware read.
3.  **Execution:** It used a `Bitmap Index Scan` to fetch the ACTIVE users, meaning it sorted the physical heap pointers in memory first to turn random I/O into sequential I/O before hitting the disk.

## 26. Design Trade-Offs

| Decision | Benefit | Consequence |
| :--- | :--- | :--- |
| **Append-Only MVCC** | Instant rollbacks; writers never block readers. | Extreme table bloat; `VACUUM` overhead is high. |
| **Process-per-Conn** | Fault isolation (a bad C-extension only kills one PID). | Massive memory overhead per connection. |
| **Double Buffering** | Synergy with Linux page cache; simpler DB code. | Wasted RAM (data duplicated in OS and PG). |

## 27. Experiments

### Experiment: Table Bloat & Autovacuum
**Goal:** Prove the physical disk penalty of Append-Only MVCC.
**Setup:** `CREATE TABLE t1 (val INT); INSERT INTO t1 SELECT generate_series(1, 1000000);` (Size: 35MB).
**Action:** `UPDATE t1 SET val = val + 1;`
**Observation:** The physical file size instantly doubles to 70MB. The old 1,000,000 tuples remain physically on disk. Background `autovacuum` wakes up 1 minute later, scans the 70MB file, and marks 35MB as "Free Space", but the file remains 70MB on disk until heavily compacted or dropped.

## 28. Key Learnings
1.  **Bloat is Inevitable:** PostgreSQL's update architecture guarantees table bloat. Tuning `autovacuum` is not optional; it is the most critical operational task.
2.  **Statistics are Everything:** If `pg_statistic` is outdated (because `ANALYZE` wasn't run), the planner will choose nested loop index scans over hash joins, destroying performance.
3.  **WAL is the Source of Truth:** Data files in PostgreSQL are technically just a performance optimization. The absolute truth of the database exists solely within the linear Write-Ahead Log.

## 29. References
1.  PostgreSQL Source Code: `src/backend/storage/buffer/bufmgr.c`
2.  PostgreSQL Source Code: `src/backend/access/nbtree/nbtree.c`
3.  PostgreSQL Documentation: Chapter 73. Database Physical Storage.
4.  InterDB: *The Internals of PostgreSQL* (Hironobu Suzuki).
