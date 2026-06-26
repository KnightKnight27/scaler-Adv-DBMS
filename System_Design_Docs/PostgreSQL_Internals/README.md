# Topic 2: PostgreSQL Internal Architecture

This report explores the core subsystems of **PostgreSQL** (v18.4), focusing on the internal mechanisms of the **Buffer Manager**, **B-Tree Indexing**, **Multi-Version Concurrency Control (MVCC)**, and **Write-Ahead Logging (WAL)**. It combines source-code-level analysis with empirical observations from dynamic query execution.

---

## 1. Buffer Manager

The PostgreSQL Buffer Manager mediates between backend worker processes and disk storage. Its primary function is to cache frequently accessed page blocks (8 KB) in a shared memory pool (`shared_buffers`), minimizing slow disk I/O.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        BUFFER MANAGER DATA FLOW                        │
├────────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  Backend Worker ──► [Read Block] ──────────────────────────────────┐   │
│         │                                                          │   │
│         ├── (Hit) ──► return Shared Buffer Page                    │   │
│         │                                                          │   │
│         └── (Miss) ──► [Clock Sweep Evicts Victim]                 │   │
│                             │                                      ▼   │
│                      (Is Victim Dirty?)                     [Page Lookup]
│                             │                                      │   │
│                      ┌──────┴──────┐                               │   │
│                      ▼             ▼                               │   │
│                   [Write]       [Evict]                            │   │
│                      │             │                               │   │
│                      ▼             ▼                               ▼   │
│                 [Disk/WAL]   [Free Frame] ◄───────────────── [Read Disk]
└────────────────────────────────────────────────────────────────────────┘
```

### Shared Buffers & Page Caching
* **Implementation Location**: `src/backend/storage/buffer/bufmgr.c`, `buf_table.c`, and `freelist.c`.
* **Data Structures**:
  * **Buffer Descriptors (`BufferDesc`)**: Array in shared memory containing metadata for each buffer frame: tag (identifying the physical file block), state flags (dirty, valid, temp, pin count), and usage count.
  * **Buffer Hash Table (`SharedBufHash`)**: Maps a buffer tag (Database ID, Relation ID, Fork Number, Block Number) to the index of the buffer descriptor currently holding that block.
  * **Buffer Pool Blocks**: The actual array of 8 KB memory pages.

### Buffer Replacement: The Clock Sweep Algorithm
PostgreSQL uses an approximation of the Least Recently Used (LRU) policy known as **Clock Sweep**:
1. **Usage Count**: Each buffer descriptor carries a `usage_count` field (integer 0–5).
2. **The Sweep Hand**: A global index (`nextVictimBuffer`) acts as a "clock hand" sweeping through the buffer descriptor array.
3. **The Eviction Loop** (defined in `freelist.c` under `StrategyGetBuffer`):
   * The hand checks a frame. If the frame is currently **pinned** (active read/write locks, pin count > 0), the hand skips it.
   * If the frame is unpinned and its `usage_count > 0`, the count is decremented by 1, and the hand moves to the next frame.
   * If the frame is unpinned and its `usage_count == 0`, it is selected as the **victim frame**. The clock hand increments, and the loop terminates.
4. **Buffer Pinning**: When a query accesses a block, the backend increments the block descriptor's pin count. While pinned, the block cannot be evicted. The backend also increments `usage_count` (capped at 5).
5. **Sequential Scan Protection**: Large sequential scans could easily wipe out hot pages from the cache by reading hundreds of single-use blocks. To prevent this, sequential scans allocate a small **Buffer Access Strategy** ring buffer (typically 256 KB / 32 pages). The scan evicts pages *only within its own ring*, leaving the main shared buffer pool untouched.

### Page Read and Write Lifecycle
* **Page Reads**: The backend hashes the requested block tag. If it's a hit, the backend pins the buffer and returns the page pointer. If it's a miss, it finds a victim page via Clock Sweep. If the victim page is dirty, it is written to disk before being replaced.
* **Dirty Page Writes**: Modifications are written to shared buffers in memory, marking the page descriptor as `dirty`. Writing dirty blocks to disk is performed asynchronously by the `Checkpointer` and `Background Writer` processes, minimizing transaction commit latency.

---

## 2. B-Tree Implementation (`nbtree`)

The default index in PostgreSQL is a highly concurrent B-Tree implementation based on the Lehman & Yao algorithm, located in `src/backend/access/nbtree/`.

### Lehman & Yao High-Concurrency B-Tree
Traditional B-Tree implementations require locking an entire branch during page splits to maintain parent-child consistency. Under heavy write workloads, this creates severe lock contention. 

PostgreSQL resolves this by introducing the **Lehman & Yao** modifications:
* **Right-Links**: Each internal and leaf page contains a pointer to its immediate right sibling at the same level of the tree.
* **High Key**: Each page stores a "high key" representing the maximum key value allowed on that page.
* **Concurrent Splits**: When a leaf page splits, a new right-sibling page is created and linked. If a concurrent search lands on the original page but seeks a key higher than the high key, the search *simply follows the right-link* to the sibling page. This allows insertions and splits to occur without holding write locks on parent nodes, vastly increasing concurrency.

```
                  ┌──────────────┐
                  │ Parent Node  │
                  └──────┬───────┘
                         │
           ┌─────────────┴─────────────┐
           ▼                           ▼
    ┌──────────────┐ Right-Link ┌──────────────┐
    │  Left Leaf   ├───────────►│  Right Leaf  │
    │ High Key: 50 │            │              │
    └──────────────┘            └──────────────┘
```

### Index Page Layout
B-Tree pages match the standard 8 KB PostgreSQL page layout but include index-specific structures:
* **Special Area**: Located at the end of the block. It stores the `BTPageOpaqueData` struct, containing right-sibling/left-sibling page numbers, level in the tree, and page flags (leaf, root, meta).
* **Index Tuples (`IndexTupleData`)**: Contain the index key columns and the physical Heap Tuple ID (TID: block number and offset) pointing to the actual row in the heap table.

### Search Path, Insert, and Page Split Walkthrough
1. **Search Path**:
   * The search begins at the Root page (fetched from the Index Meta Page).
   * It performs binary search on the page keys to locate the appropriate child page pointer.
   * It traverses downward level-by-level, reading blocks into shared buffers.
   * If a page split occurred concurrently, it follows the page's right-link if the target key is greater than the page's high key.
2. **Insert Operation**:
   * The engine descends to the target leaf page and acquires an `EXCLUSIVE` lock on the block.
   * If space exists, the index tuple is inserted.
3. **Page Split**:
   * If the page is full, a split is triggered.
   * A new page is allocated. Half of the keys are moved to the new page.
   * The left page's right-link is updated to point to the new right page.
   * The split is registered by inserting the split key and right-link pointer in the parent node. If the parent is full, it splits recursively.

---

## 3. Multi-Version Concurrency Control (MVCC)

PostgreSQL implements MVCC at the heap level, allowing readers to read a consistent snapshot of the database without blocking writers.

### Heap Tuple Versioning
Each row version is stored directly as a physical tuple in the heap. The `HeapTupleHeaderData` contains the following MVCC fields:
* `t_xmin`: The Transaction ID (XID) of the transaction that inserted the row version.
* `t_xmax`: The Transaction ID of the transaction that deleted or updated the row version (set to 0 if active/live).
* `t_cid`: Command Identifier (sequence number of statements within the transaction).
* `t_infomask`: Status flags (e.g., transaction committed, aborted, or active).

```
   ┌────────────────────────────────────────────────────────┐
   │                       HEAP TUPLE                       │
   ├───────────────────┬───────────────────┬────────────────┤
   │    t_xmin: 101    │    t_xmax: 105    │   User Data    │
   │   (Created by)    │   (Deleted by)    │  (Columns...)  │
   └───────────────────┴───────────────────┴────────────────┘
```

### Visibility Rules & Snapshot Isolation
When a transaction starts a query, it takes a **snapshot** containing:
* `xmin` (Lowest active XID): All transactions below this are committed and visible.
* `xmax` (Highest allocated XID): All transactions above this are uncommitted/future and invisible.
* `active_list` (Array of XIDs): Transactions between `xmin` and `xmax` that were active when the snapshot was taken.

#### Visibility Evaluation:
1. **Inserted Row (`t_xmin`)**:
   * If `t_xmin` has aborted, the row is invisible.
   * If `t_xmin` is active in our snapshot, the row is invisible.
   * If `t_xmin` committed before our snapshot, the row is visible (subject to deletion checks).
2. **Deleted Row (`t_xmax`)**:
   * If `t_xmax` is 0, or has aborted, the row is visible.
   * If `t_xmax` is active in our snapshot, or committed after our snapshot started, the deletion is invisible to us, so the row is visible.
   * If `t_xmax` committed before our snapshot, the deletion is visible, so the row is invisible.

### The Necessity of VACUUM
Because updates create new tuple versions instead of overwriting data in-place, the heap accumulates expired tuple versions (known as **dead tuples**).
* **Space Bloat**: Without cleanup, tables would grow indefinitely, slowing down sequential scans.
* **XID Wraparound**: Transaction IDs are 32-bit integers. After $2^{32}$ transactions, the ID wraps around to 3. If this happens, old transactions would appear to be in the "future" and their rows would suddenly become invisible.
* **VACUUM Functions**:
  1. Reclaims space by removing dead tuples and updating the Free Space Map.
  2. Freezes old transaction IDs (marking them as frozen via `t_infomask`), preventing XID wraparound.
  3. Updates the visibility map, allowing index-only scans to bypass heap checks for entirely frozen pages.

---

## 4. Write-Ahead Logging (WAL)

WAL guarantees durability and enables crash recovery by ensuring that all changes are recorded sequentially to non-volatile storage before they are applied to the database files.

### WAL Records & Buffer Synchronization
* When a page is modified, a WAL record describing the change (e.g., insert tuple, split index page) is created and appended to the **WAL Buffers** in shared memory.
* The modified data page in `shared_buffers` is updated in memory and stamped with the Log Sequence Number (LSN) of the corresponding WAL record.
* **Write-Ahead Rule**: PostgreSQL enforces that a dirty data page *cannot* be written to disk until the WAL record up to the page's LSN has been flushed to the physical WAL files (`pg_wal/`).

### Durability & Checkpointing
* **Commit Durability**: When a transaction commits, the `walwriter` process issues a synchronous write (`fsync`) of the WAL buffers containing the commit record. The transaction is now durable, even if the data blocks are still dirty in memory.
* **Checkpointing**: Writing data pages to disk is expensive. To balance write performance and recovery time, the **Checkpointer** process periodically performs a checkpoint:
  1. It flushes all dirty data pages from `shared_buffers` to the filesystem.
  2. It writes a special `CHECKPOINT` record to the WAL, indicating that all data pages up to this LSN are safely on disk.
  3. The log files prior to the checkpoint can now be recycled or archived.

### Crash Recovery
If the server crashes (e.g., power loss):
1. The engine restarts and reads the control file to find the last checkpoint LSN.
2. It starts scanning the WAL files from the checkpoint LSN forward (REDO phase).
3. For each WAL record, it checks the target data page LSN:
   * If the page LSN is less than the WAL record LSN, the change was not written to disk before the crash; the engine reapplies the change (redo) and updates the page LSN.
   * If the page LSN is greater than or equal to the WAL record LSN, the change was already written; it skips it.
4. Because MVCC visibility rules dictate what is visible, unfinished transactions are naturally ignored (treated as aborted/not committed); thus, PostgreSQL **requires no Undo phase** during recovery.

---

## 5. Join Query execution plan & Statistics Analysis

We executed a schema setup and a join query on a test database to observe query planning behavior:

```sql
CREATE TABLE users (id SERIAL PRIMARY KEY, name TEXT);
CREATE TABLE orders (id SERIAL PRIMARY KEY, user_id INT REFERENCES users(id), amount NUMERIC);
INSERT INTO users (name) VALUES ('Alice'), ('Bob'), ('Charlie');
INSERT INTO orders (user_id, amount) VALUES (1, 99.99), (1, 14.50), (2, 45.00);
ANALYZE users;
ANALYZE orders;
EXPLAIN (ANALYZE, BUFFERS) SELECT u.name, o.amount FROM users u JOIN orders o ON u.id = o.user_id;
```

### The Execution Plan
The query planner returned the following plan:

```
                                                   QUERY PLAN
-----------------------------------------------------------------------------------------------------------------
 Hash Join  (cost=1.07..2.12 rows=3 width=12) (actual time=0.036..0.038 rows=3.00 loops=1)
   Hash Cond: (o.user_id = u.id)
   Buffers: shared hit=2
   ->  Seq Scan on orders o  (cost=0.00..1.03 rows=3 width=10) (actual time=0.007..0.007 rows=3.00 loops=1)
         Buffers: shared hit=1
   ->  Hash  (cost=1.03..1.03 rows=3 width=10) (actual time=0.015..0.015 rows=3.00 loops=1)
         Buckets: 1024  Batches: 1  Memory Usage: 9kB
         Buffers: shared hit=1
         ->  Seq Scan on users u  (cost=0.00..1.03 rows=3 width=10) (actual time=0.009..0.010 rows=3.00 loops=1)
               Buffers: shared hit=1
 Planning:
   Buffers: shared hit=174
 Planning Time: 0.534 ms
 Execution Time: 0.078 ms
```

### Analysis of Plan Components

#### 1. Hash Join Mechanism
* The planner chose a **Hash Join** because the dataset is small and can easily fit in memory.
* **Hash Phase**: It scans the inner table `users` (cost=0.00..1.03, rows=3), and constructs an in-memory hash table using the join key `u.id`. This phase took 0.015 ms, allocating 9 KB of memory.
* **Probe Phase**: It scans the outer table `orders` (cost=0.00..1.03, rows=3) and probes the hash table for matching `user_id` values, returning 3 rows in total.

#### 2. Buffers & Cache Hits
* **Buffers: shared hit=2**: The execution phase read exactly 2 data blocks from PostgreSQL shared buffers (1 block for `users` heap, 1 block for `orders` heap).
* **Planning Buffers: shared hit=174**: The planner read 174 metadata catalog blocks from shared buffers to analyze constraints, data types, and statistics. This showcases why query planning has non-trivial overhead compared to the execution of simple queries.

#### 3. Relation with `pg_statistic`
The query planner estimates the cost and row counts based on statistics stored in system catalogs, specifically `pg_statistic` (queried via `pg_stats` view):
* When we executed `ANALYZE users;` and `ANALYZE orders;`, PostgreSQL scanned the tables and updated statistics:
  * **Table Size**: Stored in `pg_class.reltuples` (rows = 3) and `pg_class.relpages` (pages = 1).
  * **Column Cardinality & Distribution**: Stored in `pg_statistic`, tracking null fractions, average widths, number of distinct values (`n_distinct`), and histograms of data distribution.
* Because `n_distinct` for `users.id` and `orders.user_id` matched, the planner predicted a join cardinality of exactly 3 rows (`rows=3`), matching the actual row count returned (`rows=3.00`). If table stats were missing or out of date, the planner would fall back to default selectivity heuristics, which can lead to suboptimal plans (e.g., choosing a Nested Loop join over a Hash Join on larger datasets).
