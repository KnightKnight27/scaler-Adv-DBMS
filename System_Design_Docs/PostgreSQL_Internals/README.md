# Topic 2: PostgreSQL Internal Architecture

## 1. Problem Background

PostgreSQL is built around a process-based model that prioritizes data integrity and extensibility. The system was designed from its academic roots at UC Berkeley to handle complex relational schemas and allow users to define their own data types and index methods. 

To achieve this safely, PostgreSQL is designed around two core assumptions:
*   **Software will crash, and hardware will fail**: The database must recover to a consistent state without losing committed data, even if power is cut instantly. This is handled by the Write-Ahead Log (WAL) and strict ACID compliance rules.
*   **Reads should never block writes, and writes should never block reads**: In multi-user systems, transactions should not stall waiting for locks. PostgreSQL addresses this by using Multi-Version Concurrency Control (MVCC) rather than traditional database-level or table-level locking.

---

## 2. Architecture Overview

PostgreSQL processes queries through a multi-stage pipeline, relying on a shared memory segment for caching and synchronization.

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

When a SQL query arrives, the parser checks syntax and produces a parse tree. The analyzer validates the tables, columns, and functions against the system catalog, and the rewriter applies rules (like expanding views). 

The query tree then goes to the Cost-Based Planner. The planner uses statistical metadata collected from tables to estimate the CPU and I/O cost of different strategies (e.g. sequential scans vs index scans). 

Once the cheapest path is chosen, the executor runs the plan nodes, fetching page blocks through the Shared Buffer Pool and appending transaction logs to the WAL.

---

## 3. Internal Design

### 3.1. Buffer Manager (`src/backend/storage/buffer/`)
The buffer manager coordinates how 8KB database pages move between the disk and RAM. It maintains a fixed-size cache called Shared Buffers. 

*   **Lookup HashTable**: To find a page, the engine hashes the page's tag (relation OID, fork number, and block number). If the page is in the hash table, it increments the buffer's pin count and returns a pointer.
*   **Clock Sweep Eviction**: If the page is not cached, the engine must evict an existing page to make room. It does this using a Clock Sweep algorithm:
    *   A sweep pointer moves sequentially through the buffer descriptors.
    *   If a descriptor has a pin count of 0 (no active query is reading it) and its usage count is greater than 0, the pointer decrements the usage count and moves to the next buffer.
    *   If both the pin count and usage count are 0, this page is selected as the "victim".
    *   If the victim page has been modified (is dirty), the backend writes it to disk before reloading the new page.
*   **Flushing**: To prevent queries from stalling during eviction, background processes (`bgwriter` and `checkpointer`) periodically scan the buffers, write dirty pages to disk, and mark them clean.

---

### 3.2. B-Tree Implementation (`nbtree`)
PostgreSQL B-Tree indexes use the Lehman & Yao algorithm, which allows concurrent reads and writes without locking large sections of the index.

```
       [ Root Node: (Key 50, Pointer to Right) ]
                    /          \
     [ Internal Page ]        [ Internal Page ]
         /      \                  /      \
    [ Leaf A ] -> [ Leaf B ] -> [ Leaf C ] -> [ Leaf D ]
     (TID ptrs)    (TID ptrs)    (TID ptrs)    (TID ptrs)
```

In a standard B-Tree, when an insert causes a leaf page to split, you must lock the parent node to add the new split key. This creates a severe write bottleneck at the top of the tree. 

The Lehman & Yao algorithm solves this by linking sibling pages at the same level from left to right using **right-links**. When a page splits, the engine creates the new sibling page and sets a right-link from the original page to the new one. 

If a concurrent read transaction lands on the original page looking for a key that just moved to the new split page, it simply follows the right-link to retrieve the data. This allows parent updates to be deferred and done asynchronously, reducing write contention.

---

### 3.3. MVCC Visibility and Storage Reclamation
PostgreSQL implements MVCC by storing multiple versions of rows directly inside the main table (the Heap).

Every row header contains two fields:
*   `xmin`: The transaction ID that created this row version.
*   `xmax`: The transaction ID that deleted or replaced this row version (0 if the row is still active).

When a transaction updates a row, PostgreSQL does not modify it in place. Instead, it marks the existing row's `xmax` with the current transaction ID and inserts a brand new version of the row with its `xmin` set to the transaction ID. 

A query determines visibility by looking at its active snapshot: if a row's `xmin` is committed and its `xmax` is either uncommitted or has not started, the row is visible.

**The Need for VACUUM**:
Because old versions of updated or deleted rows remain in the Heap, tables accumulate "dead tuples" (bloat). The `VACUUM` process scans pages, removes dead tuples that are no longer visible to any active snapshot, and marks the space as free in the Free Space Map (FSM) so new inserts can reuse it. 

Additionally, `VACUUM` freezes old transaction IDs to prevent transaction ID wraparound, which would cause the 32-bit transaction counter to loop and make historic data appear invisible.

---

### 3.4. Write-Ahead Logging (WAL) and Recovery
PostgreSQL writes all modifications to an append-only Write-Ahead Log (WAL) before updating the tables on disk.

*   **The Write-Ahead Rule**: To guarantee durability, a transaction cannot return "success" to a client until its WAL records are flushed to disk. The actual dirty table pages in the shared buffers can remain in memory and be written to disk later.
*   **Crash Recovery**: If the system crashes, the database will contain inconsistent pages on disk. During startup, PostgreSQL runs recovery:
    1.  It reads the last Checkpoint record in the WAL (a known point where all memory changes were synced to disk).
    2.  It replays all WAL records forward from that checkpoint (REDO phase), modifying the table pages to match the logged changes.
    3.  It does not need an UNDO phase to roll back uncommitted changes. Since MVCC visibility rules check the transaction status map (`pg_xact`), any row versions written by transactions that did not commit remain automatically invisible.

---

## 4. Design Trade-Offs

### Heap Storage vs. Rollback Segments
PostgreSQL stores historical row versions in the main heap table.
*   **Advantage**: Aborting a transaction is almost instantaneous. The engine simply marks the transaction as aborted in `pg_xact`. No rollback work or page rewrites are needed.
*   **Limitation**: Updates cause table bloat and write amplification. If a table is updated frequently, `VACUUM` must run continuously. If vacuuming falls behind, queries must read pages full of dead tuples, slowing down scans.

### Process-Per-Connection vs. Threads
PostgreSQL spawns a separate OS process for each client connection.
*   **Advantage**: Process isolation. If one connection encounters a critical error or crashes a custom C extension, it does not crash the entire database server.
*   **Limitation**: Memory usage. Each process consumes about 10MB of RAM, and OS context switching degrades performance when there are thousands of active connections. Running a connection pooler like PgBouncer is necessary in production.

---

## 5. Experiments / Observations

We set up an experiment on our local PostgreSQL instance to analyze how the planner uses statistics to choose join paths.

### 5.1. Experimental Setup
We created a database named `scaler_lab` and loaded three tables:
*   `students` (5,000 rows, GPA uniformly distributed from 2.00 to 4.00).
*   `courses` (100 rows, with 25 courses in 'Computer Science').
*   `enrollments` (25,000 rows, mapping each student to 5 courses).

We ran `ANALYZE` to update the catalog statistics and executed this query:

```sql
EXPLAIN (ANALYZE, BUFFERS, COSTS, VERBOSE)
SELECT s.student_id, s.first_name, s.last_name, s.gpa, c.course_name, c.department, e.grade
FROM students s
JOIN enrollments e ON s.student_id = e.student_id
JOIN courses c ON e.course_id = c.course_id
WHERE s.gpa > 3.80 AND c.department = 'Computer Science'
ORDER BY s.gpa DESC, s.student_id ASC;
```

### 5.2. Execution Plan Output
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
1.  **Join Strategy and Build Selection**:
    *   The planner uses a **Hash Join**. It scans `students` first and filters for `gpa > 3.80`, yielding 480 rows. It builds an in-memory hash table on this filtered student set.
    *   It then does a sequential scan on `enrollments` (25,000 rows) and probes the student hash table using the join key `e.student_id = s.student_id`. This hash join produces 2,400 rows.
    *   Next, it scans `courses` and filters for `department = 'Computer Science'` (yielding 25 rows) and builds a second hash table. It joins the 2,400 enrollment results with the course hash table to produce the final 600 rows.
2.  **Estimate Accuracy via Catalog Statistics**:
    *   *How did the planner predict exactly 500 rows for `gpa > 3.80`?*
        We queried `pg_stats` for the `students.gpa` column and found `n_distinct = 201` distinct values uniformly ranging from `2.00` to `4.00`. The planner calculates selectivity as the fraction of values in the filtered range: `(4.00 - 3.80) / (4.00 - 2.00) = 0.10` (or 10%). 10% of the 5,000 total student rows is exactly **500 rows**. The actual scan returned **480 rows**, showing high estimate precision.
    *   *How did it predict 25 rows for the courses filter?*
        We checked `pg_stats` for the `courses.department` column. The statistics showed four distinct departments, each with a frequency of exactly 25% (`most_common_freqs = {0.25, 0.25, 0.25, 0.25}`). The planner calculated the selectivity for 'Computer Science' as `0.25`, resulting in an estimate of `100 * 0.25 = 25` rows. The actual scan returned exactly **25 rows**.
3.  **Buffer Allocation and Shared Hits**:
    *   The plan shows `Buffers: shared hit=203`, meaning all data pages were already cached in the Shared Buffer pool, resulting in zero physical disk I/O.
    *   The scan on `enrollments` read **160 pages** (160 blocks * 8KB = 1.28 MB) to scan all 25,000 records.
    *   The scan on `students` read **42 pages** (42 blocks * 8KB = 336 KB) to scan all 5,000 records.
    *   The scan on `courses` read **1 page** (8KB) to scan all 100 records.
    *   Total pages read: `160 + 42 + 1 = 203` pages.

---

## 6. Key Learnings

1.  **Query Planner Statistics**: The cost-based planner is highly dependent on statistics generated by `ANALYZE`. By maintaining accurate histograms and frequency lists, the planner makes precise selectivity estimations, allowing it to select the most efficient join ordering.
2.  **Lehman & Yao Concurrency**: B-Tree indexes in PostgreSQL avoid parent node write locks during splits by linking sibling pages horizontally. This allows concurrent readers to follow right-links to locate keys that migrated to new pages during active splits.
3.  **Buffer Caching Mechanics**: The Clock Sweep algorithm prevents cache pollution. For large sequential scans (like scanning 25,000 enrollments), pages are loaded with low usage counts, allowing the clock hand to quickly reclaim them once read, keeping the cache free for frequently accessed pages.
