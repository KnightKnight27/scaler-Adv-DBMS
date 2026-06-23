# Topic 2: PostgreSQL Internal Architecture

## 1. Problem Background

PostgreSQL is designed with a primary focus on data integrity, high concurrency, and extensibility. The system was developed as an object-relational database management system (ORDBMS) at UC Berkeley. Its core internal architecture is optimized to support:
*   **Data Integrity (ACID compliance)**: Using Write-Ahead Logging (WAL) and strict MVCC to guarantee that transactions are atomic, consistent, isolated, and durable, even in the event of system crashes.
*   **Extensibility**: Creating modular subsystems (like the table access method, index access methods, and data type system) that allow developers to plug in custom B-Trees, GiST, or GIN indexes without recompiling the database core.
*   **Concurrent Multi-User Workloads**: Ensuring that readers and writers do not block each other by using multi-version concurrency control.

---

## 2. Architecture Overview

PostgreSQL's internal architecture is highly modular. The query processing engine is structured as a pipeline, and memory is divided into shared and private regions to optimize concurrent execution.

```mermaid
graph TD
    Client[Client Process] -->|SQL Query| Parser[Parser]
    Parser -->|Query Tree| Analyzer[Analyzer / Rewriter]
    Analyzer -->|Rewritten Query Tree| Planner[Cost-Based Planner]
    Planner -->|Execution Plan| Executor[Executor]
    
    subgraph Shared Buffer Pool (Memory Cache)
        BufferPages[Cached Data Pages]
    end
    
    subgraph Disk Storage
        Heap[(Heap Relation Files)]
        WAL[(WAL Files)]
    end
    
    Executor <--> BufferPages
    BufferPages <-->|Dirty Pages Written| Heap
    Executor -->|Append Log Records| WALBuffers[WAL Buffer Pool]
    WALBuffers -->|Flush| WAL
```

1.  **Parser**: Generates a parse tree from the raw SQL text.
2.  **Analyzer/Rewriter**: Performs semantic analysis (validating table names, column types) and applies rewriting rules (such as expanding views into subqueries).
3.  **Planner/Optimizer**: The brains of PostgreSQL. It evaluates multiple join orders, scan types (Index vs. Seq Scan), and join strategies (Nested Loop, Hash, Merge Joins), using cost estimates based on table statistics to select the cheapest plan.
4.  **Executor**: Executes the plan nodes recursively, fetching data blocks from the **Shared Buffer Pool** or writing WAL records, and returns tuples to the client.

---

## 3. Internal Design

### 3.1. Buffer Manager
Located in `src/backend/storage/buffer/`, the Buffer Manager is responsible for caching disk pages (8KB blocks) in memory to avoid expensive disk I/O.

*   **Shared Buffers**: A large shared memory array composed of buffer descriptors and buffer pages.
*   **Page Caching**: When the executor requests a page:
    1.  It checks the **Buffer Lookup HashTable** (mapping table/index file OIDs and block numbers to buffer IDs).
    2.  If it is a cache hit, the page is pinned (pin count incremented) and returned.
    3.  If it is a cache miss, the page is read from the OS disk cache/storage, loaded into a free buffer, pinned, and returned.
*   **Buffer Replacement (Clock Sweep)**: PostgreSQL uses a **Clock Sweep** (approximated LRU) algorithm to find a victim page when a cache miss occurs and no free buffers exist:
    *   A sweep hand iterates through the buffer descriptors.
    *   If a buffer's pin count is 0, its usage count is checked.
    *   If the usage count is > 0, the usage count is decremented by 1, and the hand moves to the next buffer.
    *   If the usage count is 0, this buffer is selected as the victim. If it is dirty (modified), it is scheduled to be written to disk before being replaced.
*   **Page Reads and Writes**:
    *   *BgWriter*: A background process that scans the buffer pool, identifies dirty buffers with low usage counts, writes them to disk, and marks them clean.
    *   *Checkpointer*: Periodically flushes all dirty pages to disk, establishing a safe point in the WAL for recovery.

---

### 3.2. B-Tree Implementation (`nbtree`)
Located in `src/backend/access/nbtree/`, this implements the standard Lehman & Yao B-Tree algorithm.

```
       [ Root Node: (Key 50, Pointer to Right) ]
                    /          \
     [ Internal Page ]        [ Internal Page ]
         /      \                  /      \
    [ Leaf A ] -> [ Leaf B ] -> [ Leaf C ] -> [ Leaf D ]
     (TID ptrs)    (TID ptrs)    (TID ptrs)    (TID ptrs)
```

*   **Index Structure**: A balanced tree where internal nodes contain keys and child pointers, and leaf nodes contain index keys and **TIDs** (pointing to the Heap).
*   **Index Page Layout**: Like heap pages, index pages are 8KB, containing line pointers and index tuples. Additionally, they contain a **Special Space** at the end of the page, storing right-links and leaf markers.
*   **Search Path**: Recursively traverses down the tree. Thanks to the **Lehman & Yao** algorithm, leaf pages are linked from left to right (**right-links**).
*   **Insert Operations & Concurrent Page Splits**:
    *   When an insert makes a page exceed its 8KB capacity, it must split.
    *   Traditional B-Trees lock the parent node to split the child, causing severe bottlenecks.
    *   The **Lehman & Yao** algorithm allows the child to split into a left and right page, introducing a right-link from the left page to the new right page.
    *   If a concurrent search reaches the left page and is looking for a key that has moved, it simply follows the right-link to the right page *without* needing to re-traverse from the parent. The parent key insertion is deferred and done asynchronously, preventing write locks from bubbling up the tree.

---

### 3.3. MVCC (Multi-Version Concurrency Control)
PostgreSQL implements MVCC by maintaining multiple versions of a single row in the Heap.

*   **Heap Tuple Versioning**: Every row header includes:
    *   `t_xmin`: The transaction ID (XID) that inserted the row.
    *   `t_xmax`: The XID that deleted or updated the row (for active rows, `t_xmax` is 0).
    *   `t_cid`: Command identifier within the transaction.
*   **Visibility Rules**: When a query starts, it takes a **Snapshot** listing active transaction IDs. A row is visible to a transaction if:
    *   `t_xmin` is committed and was committed *before* the snapshot was taken.
    *   `t_xmax` is either 0, aborted, or represents a transaction that was not yet committed at the time of the snapshot.
*   **Why VACUUM is Necessary**:
    *   Since updates and deletes do not overwrite data but instead write new tuple versions, old "dead tuples" accumulate in the heap (known as **bloat**).
    *   `VACUUM` scans heap pages, identifies dead tuples that are no longer visible to any active transaction snapshot, removes them, and updates the Free Space Map (FSM) to allow page reuse.
    *   `VACUUM` also prevents **Transaction ID Wraparound**, where the 32-bit transaction counter wraps around, making old transactions appear in the future. It does this by freezing older tuples (setting a special freeze flag in the tuple header).

---

### 3.4. Write-Ahead Logging (WAL)
WAL guarantees the **Durability** of transactions by ensuring changes are written to an append-only log file before they are written to the database heap.

*   **WAL Records**: Every physical write (page modifications, index splits, tuple inserts) is recorded sequentially in the WAL buffer.
*   **Durability Guarantees (Write-Ahead Rule)**: A transaction is only marked committed when its corresponding WAL records are flushed to disk. The actual dirty database pages in the Shared Buffer pool can be written to disk later.
*   **Crash Recovery**:
    *   If the database crashes, the pages on disk may be inconsistent (some transactions committed in memory but pages not written to disk; some uncommitted transactions written to disk).
    *   On startup, PostgreSQL reads the WAL from the last **Checkpoint**.
    *   **REDO Phase**: It replays all WAL records forward from the checkpoint, applying changes to pages.
    *   **UNDO Phase**: Unlike databases with undo logs, PostgreSQL does not need to rollback uncommitted changes. Since MVCC visibility rules automatically hide rows from aborted/uncommitted transactions, it simply marks the uncommitted transactions as aborted in the transaction status map (`pg_xact`).

---

## 4. Design Trade-Offs

1.  **Append-Only Heap updates vs. Undo logs**:
    *   *Trade-off*: Postgres puts the burden of history on the Heap (causing table bloat), whereas engines like InnoDB put history in a separate Undo log.
    *   *Advantage*: Rolls back are instant in Postgres (just mark transaction aborted in `pg_xact`). In-place updates in Postgres don't need complex undo reconstruction.
    *   *Limitation*: Postgres requires a continuous `VACUUM` daemon to clean up dead rows. If vacuuming falls behind, query performance degrades due to table bloat.
2.  **Process Model vs. Threaded Model**:
    *   *Trade-off*: Operating a process per connection consumes more RAM (~10MB baseline per process) compared to threads.
    *   *Advantage*: Isolation. A crashed process (e.g., due to a bad extension) only kills that client connection, not the entire database server.
    *   *Limitation*: High connection counts degrade performance due to process context switching. Needs an external connection pooler (like PgBouncer).

---

## 5. Experiments / Observations

To evaluate how the PostgreSQL optimizer uses statistics to choose execution plans, we ran an experiment on a local PostgreSQL instance.

### 5.1. Setup
We populated a database named `scaler_lab` with:
*   `students` table: 5,000 rows (GPA uniformly distributed from 2.00 to 4.00).
*   `courses` table: 100 rows (25 courses in 'Computer Science', 25 in 'Mathematics', 25 in 'Physics', 25 in 'Electrical Engineering').
*   `enrollments` table: 25,000 rows (each student enrolled in exactly 5 courses).

After running `ANALYZE` on the tables, we executed the following multi-table join:

```sql
EXPLAIN (ANALYZE, BUFFERS, COSTS, VERBOSE)
SELECT s.student_id, s.first_name, s.last_name, s.gpa, c.course_name, c.department, e.grade
FROM students s
JOIN enrollments e ON s.student_id = e.student_id
JOIN courses c ON e.course_id = c.course_id
WHERE s.gpa > 3.80 AND c.department = 'Computer Science'
ORDER BY s.gpa DESC, s.student_id ASC;
```

### 5.2. Chosen Execution Plan Output
```text
 Sort  (cost=624.86..626.42 rows=625 width=58) (actual time=2.577..2.587 rows=600 loops=1)
   Output: s.student_id, s.first_name, s.last_name, s.gpa, c.course_name, c.department, e.grade
   Sort Key: s.gpa DESC, s.student_id
   Sort Method: quicksort  Memory: 76kB
   Buffers: shared hit=203
   ->  Hash Join  (cost=113.31..595.83 rows=625 width=58) (actual time=0.503..1.992 rows=600 loops=1)
         Output: s.student_id, s.first_name, s.last_name, s.gpa, c.course_name, c.department, e.grade
         Inner Unique: true
         Hash Cond: (e.course_id = c.course_id)
         Buffers: shared hit=203
         ->  Hash Join  (cost=110.75..586.43 rows=2500 width=38) (actual time=0.251..1.650 rows=2400 loops=1)
               Output: s.student_id, s.first_name, s.last_name, s.gpa, e.grade, e.course_id
               Inner Unique: true
               Hash Cond: (e.student_id = s.student_id)
               Buffers: shared hit=202
               ->  Seq Scan on public.enrollments e  (cost=0.00..410.00 rows=25000 width=11) (actual time=0.002..0.609 rows=25000 loops=1)
                     Output: e.enrollment_id, e.student_id, e.course_id, e.grade, e.enrollment_date
                     Buffers: shared hit=160
               ->  Hash  (cost=104.50..104.50 rows=500 width=31) (actual time=0.235..0.235 rows=480 loops=1)
                     Output: s.student_id, s.first_name, s.last_name, s.gpa
                     Buckets: 1024  Batches: 1  Memory Usage: 39kB
                     Buffers: shared hit=42
                     ->  Seq Scan on public.students s  (cost=0.00..104.50 rows=500 width=31) (actual time=0.008..0.209 rows=480 loops=1)
                           Output: s.student_id, s.first_name, s.last_name, s.gpa
                           Filter: (s.gpa > 3.80)
                           Rows Removed by Filter: 4520
                           Buffers: shared hit=42
         ->  Hash  (cost=2.25..2.25 rows=25 width=28) (actual time=0.250..0.250 rows=25 loops=1)
               Output: c.course_name, c.department, c.course_id
               Buckets: 1024  Batches: 1  Memory Usage: 10kB
               Buffers: shared hit=1
               ->  Seq Scan on public.courses c  (cost=0.00..2.25 rows=25 width=28) (actual time=0.003..0.008 rows=25 loops=1)
                     Output: c.course_name, c.department, c.course_id
                     Filter: ((c.department)::text = 'Computer Science'::text)
                     Rows Removed by Filter: 75
                     Buffers: shared hit=1
 Planning:
   Buffers: shared hit=109
 Planning Time: 0.214 ms
 Execution Time: 2.607 ms
```

### 5.3. Analysis of Execution Plan and Statistics
1.  **Join Strategy and Ordering**:
    *   The planner selects a **Hash Join** strategy.
    *   First, it scans the `students` table, filtering for `gpa > 3.80`. It loads these 480 filtered rows (estimated: 500) into an in-memory hash table.
    *   Then, it performs a **Sequential Scan** on `enrollments` (25,000 rows) and probes the hash table using the join condition `e.student_id = s.student_id`. This hash join produces 2,400 rows (estimated: 2,500).
    *   Next, it builds a hash table on the filtered `courses` table (where `department = 'Computer Science'`), producing 25 rows (estimated: 25).
    *   Finally, it joins the 2,400 enrollment results with the course hash table on `e.course_id = c.course_id`, resulting in 600 final rows (estimated: 625).
2.  **Estimate Accuracy via Catalog Statistics**:
    *   *Why did the planner estimate exactly 500 rows for the student filter `gpa > 3.80`?*
        We queried `pg_stats` for the `students` table:
        ```sql
        SELECT n_distinct, histogram_bounds FROM pg_stats WHERE tablename = 'students' AND attname = 'gpa';
        ```
        The catalog showed `n_distinct = 201` distinct GPA values, ranging from `2.00` to `4.00`. The histogram bounds were uniformly spread. Since the planner knows that GPAs are distributed uniformly, the condition `gpa > 3.80` (values from 3.81 to 4.00) represents exactly `20 / 201 ≈ 10%` of the dataset. 10% of 5,000 total rows yields an estimate of **500 rows**. The actual scan returned **480 rows**, showing high estimate precision.
    *   *Why did the planner estimate exactly 25 rows for the courses filter `department = 'Computer Science'`?*
        We queried `pg_stats` for the `courses` table:
        ```sql
        SELECT most_common_vals, most_common_freqs FROM pg_stats WHERE tablename = 'courses' AND attname = 'department';
        ```
        The catalog returned:
        *   `most_common_vals`: `{"Computer Science", "Electrical Engineering", "Mathematics", "Physics"}`
        *   `most_common_freqs`: `{0.25, 0.25, 0.25, 0.25}`
        This indicates that each department occurs exactly 25% of the time. For `department = 'Computer Science'`, the selectivity is `0.25`. For a total of 100 rows, the estimated row count is `100 * 0.25 = 25`. The actual scan returned exactly **25 rows**.
3.  **Buffer Allocation and I/O efficiency**:
    *   The plan shows `Buffers: shared hit=203`. All 8KB pages needed for the scan were located in the **Shared Buffer Cache** (hence `hit`, with zero read I/O from disk).
    *   `Seq Scan on enrollments` read **160 pages** (160 blocks * 8KB = 1.28 MB) to scan all 25,000 records.
    *   `Seq Scan on students` read **42 pages** (42 blocks * 8KB = 336 KB) to scan all 5,000 records.
    *   `Seq Scan on courses` read **1 page** (8KB) to scan all 100 records.
    *   Total data pages read: `160 + 42 + 1 = 203` pages.

---

## 6. Key Learnings

1.  **Accuracy of Cost-Based Optimization**: The cost-based planner relies heavily on the statistics collected in `pg_statistic` (exposed via the user-friendly `pg_stats` view). By maintaining histograms and frequency lists, the planner accurately predicts query selectivity, selecting the optimal join order (building small hash tables first to minimize probing costs).
2.  **Clock Sweep Cache Behavior**: Caching performance relies on the Clock Sweep algorithm's usage counters. For sequential scans, pages are loaded into shared buffers. If pages are only read once (e.g. during a bulk scan of `enrollments`), usage counts remain low, allowing the sweep hand to quickly reclaim these pages and prevent cache thrashing.
3.  **MVCC Costs**: MVCC provides excellent concurrency (no locks between readers and writers) but makes table size management complex. A large bulk insert/update operation creates massive quantities of dead tuples that must be cleaned by the autovacuum launcher to avoid degrading sequential scans (by reading pages full of dead tuples).
