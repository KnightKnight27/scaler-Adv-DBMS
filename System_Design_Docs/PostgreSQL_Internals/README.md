# PostgreSQL Internal Architecture

## 1. Problem Background

PostgreSQL is one of the most widely used open-source relational database management systems. It is designed to provide ACID-compliant transactions, high concurrency, reliability, extensibility, and scalability for modern applications.

Unlike lightweight embedded databases, PostgreSQL is built for multi-user environments where thousands of transactions may execute simultaneously while maintaining correctness and durability. To achieve this, PostgreSQL combines several sophisticated internal components including:

* Buffer Manager
* Multi-Version Concurrency Control (MVCC)
* B-Tree Indexes
* Write Ahead Logging (WAL)
* Query Planner and Optimizer
* Background Maintenance Processes

Understanding how these components interact helps explain PostgreSQL's behavior under real workloads and the engineering trade-offs made during its design.

---

# 2. Architecture Overview

## High-Level Architecture

```text
+----------------------+
| Client Applications  |
+----------------------+
            |
            v
+----------------------+
| PostgreSQL Server    |
+----------------------+
            |
  +---------+----------+
  |                    |
  v                    v
Query Planner     Buffer Manager
  |                    |
  +---------+----------+
            |
            v
      Storage Engine
            |
            v
        Data Files
            |
            v
      Write Ahead Log
```

### Main Components

| Component       | Responsibility                     |
| --------------- | ---------------------------------- |
| Query Planner   | Determines execution strategy      |
| Buffer Manager  | Caches disk pages in memory        |
| Storage Manager | Reads/writes table and index pages |
| WAL             | Ensures durability                 |
| MVCC            | Provides concurrency               |
| VACUUM          | Cleans obsolete tuple versions     |

---

# 3. Buffer Manager

Source Location:

```text
src/backend/storage/buffer/
```

The Buffer Manager acts as an intermediary between disk storage and query execution.

Instead of reading data directly from disk every time, PostgreSQL loads pages into shared memory buffers and reuses them whenever possible.

## Shared Buffers

Shared Buffers are a memory region accessible by all PostgreSQL backend processes.

Workflow:

```text
Query
  |
  v
Buffer Lookup
  |
  +---- Hit ----> Return Page
  |
  +---- Miss ---> Read From Disk
                    |
                    v
             Store In Buffer
```

### Why Shared Buffers Exist

Disk I/O is significantly slower than memory access.

By keeping frequently accessed pages in memory, PostgreSQL reduces disk reads and improves performance.

---

## Page Reads and Writes

Each PostgreSQL page is typically:

```text
8 KB
```

When a query accesses a row:

1. PostgreSQL identifies the page containing the row.
2. Buffer Manager checks whether the page already exists in shared buffers.
3. If not present, page is loaded from disk.
4. Query operates on the in-memory copy.

Modified pages become:

```text
Dirty Pages
```

Dirty pages are written to disk later by background processes.

---

## Buffer Replacement

When shared buffers become full, PostgreSQL must evict pages.

Instead of pure LRU, PostgreSQL uses a clock-sweep algorithm.

Benefits:

* Lower overhead
* Better scalability
* Efficient page reuse

---

## Buffer Manager Observation

Query:

```sql
SELECT
    blks_read,
    blks_hit
FROM pg_stat_database;
```

Interpretation:

* blks_read = pages loaded from disk
* blks_hit = pages served directly from memory

A high cache hit ratio indicates efficient buffer utilization.

---

# 4. B-Tree Implementation

Source Location:

```text
src/backend/access/nbtree/
```

PostgreSQL's default index structure is the B-Tree.

B-Trees provide:

* O(log n) lookups
* Efficient range queries
* Balanced tree structure

---

## B-Tree Structure

```text
             [50]
           /      \
      [20,30]   [70,90]
```

Internal nodes contain search keys.

Leaf nodes contain:

```text
Key -> Tuple Pointer (TID)
```

---

## Experiment

Created Index:

```sql
CREATE INDEX idx_orders_amount
ON orders(amount);
```

Query:

```sql
EXPLAIN ANALYZE
SELECT
    c.city,
    COUNT(*),
    AVG(o.amount)
FROM customers c
JOIN orders o
ON c.customer_id=o.customer_id
WHERE o.amount > 3000
GROUP BY c.city;
```

Execution Plan:

```text
Bitmap Index Scan on idx_orders_amount
  ->
Bitmap Heap Scan on orders
```

### Analysis

PostgreSQL first traversed the B-Tree index and identified matching tuple locations.

It then fetched the corresponding heap pages.

This demonstrates an important PostgreSQL design decision:

Indexes store tuple references rather than actual row contents.

Advantages:

* Smaller indexes
* Faster index maintenance

Trade-off:

* Requires heap lookup after index lookup

---

## Page Splits

When a B-Tree page becomes full:

```text
Before:

[10 20 30 40 50]

After:

[10 20]
      |
      v
[30]
      |
      v
[40 50]
```

PostgreSQL splits pages while maintaining tree balance.

This guarantees logarithmic search complexity.

---

# 5. Multi-Version Concurrency Control (MVCC)

MVCC is PostgreSQL's core concurrency mechanism.

Instead of overwriting rows, PostgreSQL creates new tuple versions.

---

## Heap Tuple Versioning

Each row contains hidden metadata:

```text
xmin
xmax
```

Where:

* xmin = creating transaction
* xmax = deleting/updating transaction

---

## CTID Experiment

Before Update:

```text
ctid = (0,1)
city = Chennai
```

Update:

```sql
UPDATE customers
SET city='Pune'
WHERE customer_id=1;
```

After Update:

```text
ctid = (69,120)
city = Pune
```

Another Update:

```sql
UPDATE customers
SET city='Pune'
WHERE customer_id=1;
```

Result:

```text
ctid = (69,121)
```

### Observation

CTID changed even though the row remained logically the same.

This demonstrates that PostgreSQL created a new tuple version rather than modifying the existing tuple in place.

---

## Snapshot Isolation Experiment

Transaction 1:

```sql
BEGIN;

UPDATE customers
SET city='Kolkata'
WHERE customer_id=1;
```

Transaction 2:

```sql
SELECT *
FROM customers
WHERE customer_id=1;
```

Output:

```text
customer_id = 1
city = Chennai
```

### Analysis

The second transaction observed the previously committed version of the row.

Readers were not blocked by writers.

This behavior is enabled by MVCC.

---

## Why VACUUM Is Necessary

Old tuple versions remain on disk.

Example:

```text
Version 1
Version 2
Version 3
```

Eventually old versions become obsolete.

VACUUM:

* Reclaims storage
* Prevents table bloat
* Updates visibility information

Without VACUUM:

* Table size increases
* Query performance degrades

---

# 6. Write Ahead Logging (WAL)

WAL Source Components:

```text
src/backend/access/transam/
```

PostgreSQL follows the WAL protocol.

Principle:

```text
Log First
Data Later
```

---

## WAL Workflow

```text
Transaction
     |
     v
Generate WAL Record
     |
     v
Flush WAL
     |
     v
Commit
     |
     v
Write Data Pages Later
```

---

## Why WAL Exists

Without WAL:

```text
Crash
  |
  v
Lost Updates
```

With WAL:

```text
Crash
  |
  v
Replay WAL
  |
  v
Restore Consistency
```

---

## Durability Guarantee

A transaction is considered committed only after its WAL records reach persistent storage.

This satisfies the "D" in ACID.

---

## Checkpointing

Continuously replaying all WAL records would be expensive.

PostgreSQL periodically creates checkpoints.

Checkpoint:

```text
Flush Dirty Pages
Save Consistent State
```

Recovery begins from the latest checkpoint rather than the beginning of the WAL stream.

---

# 7. Query Planning and Statistics

One of PostgreSQL's most sophisticated components is its cost-based query planner.

Planner decisions rely heavily on statistics collected through:

```sql
ANALYZE;
```

---

## Statistics Collected

Query:

```sql
SELECT
    attname,
    n_distinct
FROM pg_stats
WHERE tablename='customers';
```

Output:

```text
customer_id : -1
name        : -1
city        : 4
```

Observation:

Planner knows there are only four city values.

---

Orders Table Statistics:

```text
customer_id : 9854
amount      : 5001
order_date  : 366
```

These statistics allow PostgreSQL to estimate cardinalities accurately.

---

# 8. Query Plan Analysis

Query:

```sql
SELECT
    c.city,
    COUNT(*) AS total_orders,
    AVG(o.amount)
FROM customers c
JOIN orders o
ON c.customer_id=o.customer_id
WHERE o.amount > 3000
GROUP BY c.city;
```

---

## Execution Plan

```text
HashAggregate
  -> Hash Join
       -> Bitmap Heap Scan
            -> Bitmap Index Scan
       -> Hash
            -> Seq Scan
```

---

## Planner Decisions

### Bitmap Index Scan

Planner used:

```text
idx_orders_amount
```

because the filter:

```sql
amount > 3000
```

is selective.

---

### Bitmap Heap Scan

Rows matching the index condition were fetched from heap pages.

---

### Hash Join

Planner selected Hash Join instead of Nested Loop Join.

Reason:

```text
customers = 10,000 rows
orders > 3000 = ~40,000 rows
```

Hash Join has lower cost for large joins.

---

### Hash Aggregate

Used to group results by city.

Only four groups existed.

---

## Planner Estimate Accuracy

Estimated:

```text
39865 rows
```

Actual:

```text
40181 rows
```

Difference:

```text
316 rows
```

Error:

```text
0.79%
```

This demonstrates highly accurate statistics.

---

# 9. Design Trade-Offs

| Feature            | Benefit                | Cost                   |
| ------------------ | ---------------------- | ---------------------- |
| MVCC               | High concurrency       | Requires VACUUM        |
| Shared Buffers     | Faster access          | Memory usage           |
| WAL                | Durability             | Additional writes      |
| B-Tree Indexes     | Fast lookups           | Maintenance overhead   |
| Cost-Based Planner | Better execution plans | Statistics maintenance |

---

# 10. Key Learnings

1. PostgreSQL relies heavily on memory through the Buffer Manager to reduce disk I/O.

2. B-Tree indexes accelerate lookups but require additional heap access because indexes store tuple references rather than complete rows.

3. MVCC enables readers and writers to operate concurrently without blocking each other.

4. CTID changes confirmed that PostgreSQL creates new tuple versions during updates.

5. VACUUM is essential because MVCC generates obsolete tuple versions.

6. WAL guarantees durability by recording modifications before data pages are written.

7. Query planning depends heavily on statistics collected through ANALYZE and stored in pg_statistic.

8. The execution plan demonstrated PostgreSQL's ability to select efficient operators such as Bitmap Scans and Hash Joins based on estimated costs.

9. PostgreSQL's architecture prioritizes correctness, concurrency, and reliability, even at the cost of additional storage and implementation complexity.

# References

1. PostgreSQL Official Documentation
2. PostgreSQL Source Code (`src/backend/storage/buffer`)
3. PostgreSQL Source Code (`src/backend/access/nbtree`)
4. PostgreSQL Internals Documentation
5. Database System Concepts – Silberschatz, Korth, Sudarshan
