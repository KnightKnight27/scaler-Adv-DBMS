# PostgreSQL Internals: MVCC, WAL, and Query Planning

## 1. Heap Tuple Versioning & MVCC

### 1.1 The Core Problem: Concurrent Access Without Blocking

In a multi-user database, how do we allow readers and writers to coexist without blocking each other? PostgreSQL's answer is **Multi-Version Concurrency Control (MVCC)** — instead of locking rows during reads, PostgreSQL maintains multiple versions of each row, and each transaction sees a consistent snapshot of the database.

### 1.2 Tuple Structure: The Hidden System Columns

Every tuple (row) in PostgreSQL contains hidden system columns that drive MVCC:

```
┌─────────────────────────────────────────────────────────┐
│ Tuple Header (23+ bytes)                                │
├─────────────────────────────────────────────────────────┤
│ t_xmin (4 bytes)    │ Inserting transaction ID          │
│ t_xmax (4 bytes)    │ Deleting transaction ID (0 = live)│
│ t_cid (4 bytes)     │ Command ID within transaction     │
│ t_ctid (6 bytes)    │ Current TID (block, offset)         │
│ t_infomask2 (2 bytes) │ Number of attributes + flags    │
│ t_infomask (2 bytes)  │ Status flags (hint bits)        │
│ t_hoff (1 byte)     │ Offset to user data               │
├─────────────────────────────────────────────────────────┤
│ Null Bitmap (variable)                                  │
├─────────────────────────────────────────────────────────┤
│ User Data Columns                                       │
└─────────────────────────────────────────────────────────┘
```

**Key Fields:**

- **`xmin`**: The transaction ID that created this tuple version. If the inserting transaction committed before our snapshot, the tuple is potentially visible.
- **`xmax`**: The transaction ID that deleted this tuple version. If xmax is 0, the tuple is live. If xmax committed before our snapshot, the tuple is invisible (deleted).
- **`ctid`**: Physical location `(block_number, offset)`. When a tuple is updated, `ctid` points to the newer version, forming a version chain.
- **`cmin`/`cmax`**: Command IDs track visibility within a single transaction. If you insert a row and then query it within the same transaction, `cmin` ensures you see it.

### 1.3 How INSERT, UPDATE, and DELETE Work

**INSERT:**
```
Transaction 100: INSERT INTO users VALUES ('Alice', 25);

Resulting tuple:
  xmin = 100, xmax = 0, data = ('Alice', 25)
```

**UPDATE:**
```
Transaction 101: UPDATE users SET age = 26 WHERE name = 'Alice';

Step 1: Mark old tuple as deleted
  Old tuple: xmin = 100, xmax = 101, data = ('Alice', 25)

Step 2: Create new tuple version
  New tuple: xmin = 101, xmax = 0, data = ('Alice', 26)
  New tuple's ctid points to itself (it's the latest version)
  Old tuple's ctid points to new tuple
```

**DELETE:**
```
Transaction 102: DELETE FROM users WHERE name = 'Alice';

Result:
  Tuple: xmin = 101, xmax = 102, data = ('Alice', 26)
  (Tuple remains physically until VACUUM reclaims it)
```

### 1.4 Transaction Snapshots

A **snapshot** is a data structure that determines which tuple versions are visible to a transaction:

```
Snapshot Structure:
  xmin:    Lowest active transaction ID when snapshot was taken
  xmax:    Next transaction ID to be assigned (all >= xmax are "future")
  xip[]:   List of active (in-progress) transaction IDs
```

**Example:**
```
Global State:
  Committed: 1, 2, 3, 5, 7
  Active: 8, 11, 12, 14
  Next XID: 16

Snapshot for a new transaction:
  xmin = 8   (lowest active)
  xmax = 16  (next to assign)
  xip = [8, 11, 12, 14]
```

### 1.5 Visibility Rules

A tuple is **visible** to a snapshot if andn only if:

1. **Creation Rule**: The tuple's `xmin` is visible:
   - `xmin` < `snapshot.xmin` → committed before snapshot → VISIBLE
   - `xmin` is in `xip[]` → still active → INVISIBLE
   - `xmin` >= `snapshot.xmax` → future transaction → INVISIBLE
   - `xmin` between `snapshot.xmin` and `snapshot.xmax` but not in `xip[]` → committed → VISIBLE

2. **Deletion Rule**: The tuple's `xmax` is NOT visible:
   - `xmax` = 0 → not deleted → VISIBLE
   - `xmax` < `snapshot.xmin` → committed deletion → INVISIBLE
   - `xmax` is in `xip[]` → deletion in progress → VISIBLE (deleter hasn't committed)
   - `xmax` >= `snapshot.xmax` → future deletion → VISIBLE

**In pseudocode:**
```python
def is_visible(tuple, snapshot):
    # Check if creator is visible
    if not xmin_is_committed(tuple.xmin, snapshot):
        return False

    # Check if deleter is visible
    if tuple.xmax == 0:
        return True  # Not deleted
    if xmax_is_committed(tuple.xmax, snapshot):
        return False  # Deletion committed
    return True  # Deletion not committed or in progress
```

### 1.6 Isolation Levels and Snapshot Timing

| Isolation Level | Snapshot Timing | Behavior |
|----------------|----------------|----------|
| **READ COMMITTED** | Per statement | Sees commits from other transactions between statements |
| **REPEATABLE READ** | Transaction start | Same snapshot for entire transaction; detects anomalies |
| **SERIALIZABLE** | Transaction start | Full serializable isolation with predicate locking |

**READ COMMITTED Example:**
```sql
-- Transaction A (XID: 100)
BEGIN;
SELECT * FROM users;  -- Snapshot 1: sees rows committed before this statement
-- Transaction B commits an INSERT
SELECT * FROM users;  -- Snapshot 2: sees the new row (non-repeatable read)
COMMIT;
```

**REPEATABLE READ Example:**
```sql
-- Transaction A (XID: 100)
BEGIN ISOLATION LEVEL REPEATABLE READ;
SELECT * FROM users;  -- Snapshot 1: fixed for entire transaction
-- Transaction B commits an INSERT
SELECT * FROM users;  -- Same snapshot: does NOT see the new row
COMMIT;
```

### 1.7 Why VACUUM is Necessary

Over time, UPDATE and DELETE operations create **dead tuples** (invisible to all active transactions) that consume disk space. **VACUUM** reclaims this space:

**VACUUM (Standard):**
- Scans heap pages to identify dead tuples
- Removes index entries pointing to dead tuples
- Marks line pointers as `LP_UNUSED` for reuse
- Does NOT return space to OS (page remains allocated)

**VACUUM FULL:**
- Rewrites the entire table file, compacting live tuples
- Returns freed space to the operating system
- Requires exclusive table lock (blocking)

**Autovacuum:**
- Background daemon that automatically vacuums tables
- Triggered when dead tuple count exceeds threshold
- Configurable via `autovacuum_vacuum_threshold` and `autovacuum_vacuum_scale_factor`

**Freeze:**
- Transaction IDs are 32-bit integers that wrap around after ~4 billion
- VACUUM "freezes" old tuples by setting special flags so they remain visible after wraparound
- Prevents transaction ID wraparound failures

**The Bloat Problem:**
Without regular VACUUM, tables can grow indefinitely ("bloat"), especially on high-churn tables. This is PostgreSQL's biggest operational challenge compared to databases that do in-place updates.

---

## 2. WAL (Write-Ahead Logging)

### 2.1 The Durability Dilemma

The fundamental problem: How do we guarantee that committed transactions survive a crash, while also achieving good performance?

**Naive Approach (slow):**
```
1. Modify data page in memory
2. Write data page to disk (random I/O, ~10ms)
3. Return COMMIT to client
```
Problems: Random writes are slow, partial page writes corrupt data, no batching possible.

**WAL Solution (fast):**
```
1. Modify data page in memory
2. Write change to WAL (sequential I/O, ~0.1ms)
3. fsync WAL to disk
4. Return COMMIT to client
5. Later: Background writer flushes dirty pages to disk
```

### 2.2 WAL Architecture

**WAL Files:**
- Stored in `pg_wal/` (formerly `pg_xlog/`)
- Fixed-size segments (default 16MB)
- Sequential append-only structure
- Named by timeline and LSN (Log Sequence Number)

**WAL Record Structure:**
```
┌─────────────────────────────────────┐
│ WAL Record Header (24 bytes)        │
│  - xl_tot_len: Total record length    │
│  - xl_xid: Transaction ID           │
│  - xl_prev: Previous record offset    │
│  - xl_info: Flags and info            │
│  - xl_rmid: Resource manager ID       │
│  - CRC checksum                       │
├─────────────────────────────────────┤
│ Block References                      │
│  - relfilenode, block number, fork  │
├─────────────────────────────────────┤
│ Main Data (variable)                  │
│  - Old tuple image (for undo)         │
│  - New tuple image (for redo)         │
│  - Index changes                      │
├─────────────────────────────────────┤
│ Full Page Image (optional)            │
│  - Complete page copy after checkpoint│
└─────────────────────────────────────┘
```

### 2.3 The Write-Ahead Principle

**Critical Rule:** WAL records must be written and flushed to disk BEFORE the corresponding data page is written to disk.

This ensures that if a crash occurs:
- If COMMIT was returned: WAL contains the change → can replay
- If COMMIT was not returned: WAL may contain partial changes → will be rolled back

### 2.4 Checkpointing

**What is a Checkpoint?**
A checkpoint is a point in the WAL stream where all dirty buffers have been flushed to disk. It serves as the starting point for crash recovery.

**Checkpoint Process:**
1. Write checkpoint record to WAL
2. Flush all dirty shared buffers to disk
3. Write checkpoint metadata to `pg_control` file
4. Old WAL segments (before checkpoint) can be recycled

**Checkpoint Triggers:**
- `checkpoint_timeout` (default: 5 minutes)
- `max_wal_size` reached (default: 1GB)
- Manual `CHECKPOINT` command
- `pg_basebackup` or `pg_start_backup()`

**Checkpoint Tuning Trade-offs:**
| Setting | Frequent Checkpoints | Infrequent Checkpoints |
|---------|---------------------|----------------------|
| **Recovery Time** | Fast (less WAL to replay) | Slow (more WAL to replay) |
| **WAL Volume** | Higher (more FPW) | Lower |
| **I/O Spikes** | Smaller, more frequent | Larger, less frequent |
| **Disk Space** | Less WAL retained | More WAL retained |

### 2.5 Crash Recovery

**Recovery Process:**
```
1. Read pg_control → find last checkpoint LSN
2. Read checkpoint record → find redo point
3. Scan WAL from redo point to end
4. For each WAL record:
   a. Check if page needs replay (compare page LSN with WAL LSN)
   b. If page LSN < WAL LSN → apply change
   c. If page LSN >= WAL LSN → skip (already applied)
5. After replay, database is consistent
```

**Handling Torn Pages:**
If a crash occurs during a page write, the page may be partially written ("torn page"). PostgreSQL solves this with **Full Page Writes (FPW)**:
- After each checkpoint, the first modification to any page writes the ENTIRE page image to WAL
- During recovery, if a torn page is detected, restore from the FPW in WAL, then apply subsequent changes

### 2.6 WAL for Replication

**Streaming Replication:**
- Primary sends WAL records to standbys in real-time
- Standbys replay WAL to stay in sync
- Supports synchronous (zero data loss) and asynchronous (better performance) modes

**Logical Replication:**
- Decodes WAL into logical changes (row-level)
- Allows selective replication (specific tables)
- Cross-version and cross-platform replication

### 2.7 Group Commit

Writing WAL and calling `fsync()` for every transaction would be prohibitively slow. PostgreSQL uses **group commit**:

```
Time 0ms:   Transaction A commits → waits
Time 0.1ms: Transaction B commits → waits
Time 0.2ms: Transaction C commits → waits
Time 1ms:   WAL writer flushes all three records with one fsync()
Time 6ms:   fsync() returns → all three transactions return COMMIT
```

This amortizes the cost of fsync() across multiple transactions.

---

## 3. Query Planning & Statistics

### 3.1 The Query Planner's Job

Given a SQL query, the planner must:
1. Parse the query into a query tree
2. Rewrite the query (rules, views)
3. Generate possible execution plans
4. Estimate the cost of each plan
5. Choose the cheapest plan

### 3.2 Statistics Collection

PostgreSQL collects statistics via the `ANALYZE` command (or autovacuum):

**Table-Level Statistics (pg_class):**
- `reltuples`: Estimated number of live rows
- `relpages`: Number of disk pages
- `relallvisible`: Pages visible to all transactions (for index-only scans)

**Column-Level Statistics (pg_statistic / pg_stats):**

| Statistic | Description | Use Case |
|-----------|-------------|----------|
| `null_frac` | Fraction of NULL values | `IS NULL` selectivity |
| `avg_width` | Average column width | Memory estimation |
| `n_distinct` | Number of distinct values | Equality selectivity |
| `most_common_vals` | Most frequent values | `WHERE col = 'x'` estimates |
| `most_common_freqs` | Frequencies of MCVs | Selectivity calculation |
| `histogram_bounds` | Equal-population buckets | Range condition estimates |
| `correlation` | Physical vs logical ordering | Index scan cost estimation |

**How ANALYZE Works:**
1. Sample ~30,000 rows (configurable via `default_statistics_target`)
2. Count NULLs → `null_frac`
3. Calculate average width → `avg_width`
4. Count distinct values → `n_distinct`
5. Identify most common values → `most_common_vals` + `most_common_freqs`
6. Build histogram from remaining values → `histogram_bounds`
7. Compute correlation between physical order and value order → `correlation`

### 3.3 Cost Model

PostgreSQL uses a cost-based model with these parameters:

```
seq_page_cost = 1.0        -- Cost of sequential page read
random_page_cost = 4.0     -- Cost of random page read (index)
cpu_tuple_cost = 0.01      -- Cost of processing one tuple
cpu_index_tuple_cost = 0.005 -- Cost of processing index tuple
cpu_operator_cost = 0.0025 -- Cost of operator evaluation
effective_cache_size = 4GB -- Expected cache size for cost estimation
```

**Example Cost Calculation:**
```sql
SELECT * FROM orders WHERE customer_id = 123;

Option A: Sequential Scan
  Cost = relpages * seq_page_cost + reltuples * cpu_tuple_cost
       = 1000 * 1.0 + 100000 * 0.01 = 2000

Option B: Index Scan
  Cost = index_pages * random_page_cost + matching_tuples * (cpu_tuple_cost + random_page_cost)
       = 3 * 4.0 + 50 * (0.01 + 4.0) = 12 + 200.5 = 212.5

Winner: Index Scan (212.5 < 2000)
```

### 3.4 EXPLAIN ANALYZE

`EXPLAIN ANALYZE` shows both the planner's estimates and actual execution statistics:

```sql
EXPLAIN (ANALYZE, BUFFERS, FORMAT JSON)
SELECT o.order_id, c.name, p.product_name
FROM orders o
JOIN customers c ON o.customer_id = c.customer_id
JOIN order_items oi ON o.order_id = oi.order_id
JOIN products p ON oi.product_id = p.product_id
WHERE o.order_date > '2024-01-01'
  AND c.country = 'USA';
```

**Output Interpretation:**
```
Nested Loop  (cost=0.29..1234.56 rows=100 width=64) (actual time=0.123..45.678 rows=89 loops=1)
  ->  Index Scan using idx_orders_date on orders o
        (cost=0.29..100.00 rows=50 width=16)
        (actual time=0.050..2.300 rows=45 loops=1)
        Index Cond: (order_date > '2024-01-01')
  ->  Index Scan using idx_customers_pk on customers c
        (cost=0.29..8.30 rows=1 width=32)
        (actual time=0.200..0.500 rows=2 loops=45)
        Index Cond: (customer_id = o.customer_id)
        Filter: (country = 'USA')
        Rows Removed by Filter: 0
```

**Key Metrics:**
- **cost=startup..total**: Planner's cost estimate
- **actual time=first_row..total**: Actual execution time in milliseconds
- **rows**: Estimated vs actual row count
- **loops**: Number of times this node was executed
- **Buffers**: Shared hit (cache), read (disk), dirtied, written

**Diagnosing Plan Quality:**
- If `rows` estimate is off by >10x, statistics are stale → run `ANALYZE`
- If `actual time` >> `cost`, check `random_page_cost` setting
- High `Buffers: shared read` → not enough shared_buffers or poor locality

### 3.5 Join Planning

PostgreSQL supports multiple join algorithms:

**Nested Loop Join:**
- For each row in outer table, scan inner table
- Best when outer table is small and inner has an index
- Cost: `O(N * M)` without index, `O(N * log M)` with index

**Hash Join:**
- Build hash table from smaller relation, probe with larger
- Best for large tables without useful indexes
- Cost: `O(N + M)` memory and time
- Requires `work_mem` to fit hash table

**Merge Join:**
- Sort both inputs, then merge
- Best when inputs are already sorted or when sorting is cheap
- Cost: `O(N log N + M log M)`

**Join Order Optimization:**
For N tables, there are `N!` possible join orders. PostgreSQL uses:
- **GeQP (Genetic Query Optimizer)**: For >12 tables, uses genetic algorithm
- **Dynamic Programming**: For ≤12 tables, exhaustive search with pruning

### 3.6 The Role of pg_statistic

`pg_statistic` is the raw catalog table; `pg_stats` is the human-friendly view.

**How Statistics Affect Plans:**

1. **Selectivity Estimation:**
   ```sql
   WHERE status = 'pending'
   ```
   If `most_common_vals` shows 'pending' at 25% frequency → estimate 25% of rows

2. **Range Estimation:**
   ```sql
   WHERE age BETWEEN 18 AND 65
   ```
   Uses `histogram_bounds` to estimate what fraction of rows fall in the range

3. **Correlation & Index Scans:**
   - High correlation (near 1.0): Index scan reads sequential pages → cheap
   - Low correlation (near 0): Index scan causes random I/O → expensive
   - This is why `random_page_cost` matters!

4. **Join Size Estimation:**
   ```sql
   SELECT * FROM A JOIN B ON A.x = B.x
   ```
   Uses `n_distinct` to estimate join cardinality:
   ```
   estimated_rows = (rows_A * rows_B) / MAX(n_distinct_A, n_distinct_B)
   ```

**Statistics Quality Issues:**
- **Stale stats**: After bulk loads, `reltuples` may be wrong → run `ANALYZE`
- **Skewed data**: Histograms assume uniform distribution within buckets
- **Correlated columns**: Default stats don't capture column correlations → use `CREATE STATISTICS`
- **Data type limitations**: Some types (arrays, JSONB) have limited statistics

---

## 4. Buffer Manager

### 4.1 Shared Buffers

PostgreSQL's buffer cache sits between the query executor and the OS filesystem:

```
Query Executor
      │
      ▼
Shared Buffers (RAM)
      │ Cache Hit → Return data immediately
      │ Cache Miss
      ▼
OS Page Cache
      │
      ▼
Disk (Data Files)
```

**Buffer Descriptor:**
Each buffer has a descriptor containing:
- `buffer_id`: Index in the buffer array
- `tag`: Identifies which disk page this buffer holds (tablespace, database, relfilenode, fork, block)
- `usage_count`: Clock sweep counter (0-5)
- `pin_count`: Number of backends currently accessing this buffer
- `flags`: Dirty, valid, IO-in-progress

### 4.2 Page Replacement: Clock Sweep

When all buffers are full and a new page is needed:

1. **Clock Sweep Algorithm**: Scan buffers in circular order
2. Decrement `usage_count` of each buffer
3. First buffer with `usage_count = 0` and `pin_count = 0` is evicted
4. If dirty, write to disk first (or let background writer handle it)

**Why Clock Sweep?**
- Simple and lightweight (no heavy LRU list manipulation)
- Approximates LRU with minimal locking
- `usage_count` provides resistance against sequential scans flushing hot data

### 4.3 Buffer Access Patterns

**Sequential Scan:**
- Reads pages in order
- Uses `seq_page_cost` for cost estimation
- Can use **synchronization barriers** for parallel sequential scans

**Index Scan:**
- Reads index pages, then heap pages
- Random I/O pattern → higher cost
- **Index-Only Scan**: If all columns are in the index AND heap page is visible in VM, avoids heap access

**Bitmap Index Scan:**
- Scans index to build a bitmap of matching heap pages
- Then scans heap pages in physical order (sequential I/O)
- Best when index selectivity is moderate (many rows match)

---

## 5. Key Learnings & Architectural Insights

1. **MVCC is not free**: The ability to read without locking comes at the cost of storage bloat and the need for VACUUM. PostgreSQL chose this trade-off because read-heavy workloads dominate in practice.

2. **WAL is the source of truth**: Data files are merely a cache of the WAL. This inversion — log is primary, data files are secondary — is the key insight that enables both crash recovery and replication.

3. **Statistics are estimates, not facts**: The planner makes decisions based on sampled statistics. Stale or inaccurate statistics lead to catastrophic plan choices (e.g., nested loop over hash join).

4. **Correlation matters more than you think**: The physical ordering of data on disk (`correlation` statistic) can make an index scan 10x faster or 10x slower. This is why `CLUSTER` exists.

5. **The buffer manager is a cache of a cache**: PostgreSQL's shared buffers cache the OS page cache, which caches the disk. Double caching seems wasteful but provides control over eviction and dirty page management.

6. **Hint bits are a performance hack**: The `t_infomask` hint bits cache CLOG lookup results to avoid repeated visibility checks. This is a classic space-time trade-off.

---

## 6. References

- PostgreSQL Source Code: `src/backend/access/heap/heapam_visibility.c`
- "PostgreSQL MVCC Internals" - Dev.to: https://dev.to/headf1rst/postgresql-mvcc-internals-from-xminxmax-to-isolation-levels-2g6h
- "How Write-Ahead Logging Makes Databases Crash-Safe" - Medium: https://medium.com/@vinodbokare0588/how-write-ahead-logging-makes-databases-crash-safe-7d420a03fca5
- "PostgreSQL Statistics: Why queries run slow" - BoringSQL: https://boringsql.com/posts/postgresql-statistics/
- "The Query Planner" - Internals for Interns: https://internals-for-interns.com/posts/postgres-query-planner/
