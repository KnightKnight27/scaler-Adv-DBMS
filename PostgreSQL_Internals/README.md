# PostgreSQL Internal Architecture Analysis

## Author Information

| Field | Details |
|---------|---------|
| Name | Tanmay Mittal |
| Roll Number | 24BCS10491 |

## Overview

This assignment explores PostgreSQL internal architecture with a focus on:

* Buffer Manager
* B-Tree Index Implementation
* MVCC (Multi-Version Concurrency Control)
* WAL (Write Ahead Logging)
* Query Planning and Statistics

A practical analysis was performed using `EXPLAIN ANALYZE` on a multi-table join query.

## Overview

This assignment explores PostgreSQL internal architecture with a focus on:

* Buffer Manager
* B-Tree Index Implementation
* MVCC (Multi-Version Concurrency Control)
* WAL (Write Ahead Logging)
* Query Planning and Statistics

A practical analysis was performed using `EXPLAIN ANALYZE` on a multi-table join query.

---

# 1. Buffer Manager

## Purpose

The PostgreSQL Buffer Manager is responsible for moving pages between disk storage and memory.

Source Location:

```text
src/backend/storage/buffer/
```

PostgreSQL stores table data in fixed-size pages (8 KB). Frequently accessed pages are loaded into Shared Buffers to reduce expensive disk I/O operations.

---

## Shared Buffers

Shared Buffers are a memory cache used by all PostgreSQL backend processes.

Data Flow:

```text
Disk
 ↓
Shared Buffer Cache
 ↓
Query Execution
```

When a query requests data:

1. PostgreSQL checks Shared Buffers.
2. If the page is present, it is used directly.
3. Otherwise, the page is loaded from disk.

---

## Page Caching

The execution plan showed:

```text
Buffers: shared hit=2
```

This indicates that required pages were already present in memory and no additional disk reads were necessary.

This demonstrates successful page caching by the Buffer Manager.

---

## Buffer Replacement

When Shared Buffers become full, PostgreSQL uses a clock-sweep replacement algorithm.

Frequently accessed pages remain in memory while less frequently used pages are replaced.

---

## Page Reads and Writes

### Read Path

```text
Query
 ↓
Buffer Cache Check
 ↓
Page Found?
 ├─ Yes → Use Cached Page
 └─ No → Read From Disk
```

### Write Path

```text
Update
 ↓
Modify Buffer Page
 ↓
Mark Page Dirty
 ↓
Checkpoint
 ↓
Write To Disk
```

---

# 2. B-Tree Index Implementation

Source Location:

```text
src/backend/access/nbtree/
```

An index was created:

```sql
CREATE INDEX idx_emp_dept
ON employees(dept_id);
```

---

## Index Structure

PostgreSQL B-Tree indexes contain:

* Root Pages
* Internal Pages
* Leaf Pages

Search complexity:

```text
O(log n)
```

---

## Insert Operations

When a new indexed row is inserted:

1. PostgreSQL navigates from root to leaf.
2. The key is inserted into the appropriate leaf page.
3. Parent nodes are updated if required.

---

## Page Splits

If a leaf page becomes full:

```text
Before:
[10 20 30 40]

Insert 50

After:
[10 20]
[30 40 50]
```

The tree remains balanced after the split.

---

# 3. MVCC (Multi-Version Concurrency Control)

MVCC allows concurrent readers and writers without blocking.

---

## Heap Tuple Versioning

Instead of modifying rows in place, PostgreSQL creates new tuple versions.

Each tuple stores:

```text
xmin
xmax
```

---

## MVCC Observation

Query:

```sql
SELECT xmin, xmax, *
FROM employees
LIMIT 5;
```

Output:

```text
xmin | xmax
-----+-----
754  | 0
754  | 0
754  | 0
754  | 0
754  | 0
```

Interpretation:

* `xmin = 754` indicates the transaction that created the rows.
* `xmax = 0` indicates that the rows have not been deleted or updated.

---

## Visibility Rules

A transaction sees:

* Committed rows
* Rows visible in its snapshot

A transaction does not see:

* Uncommitted changes from other transactions

This provides snapshot isolation.

---

# Why VACUUM Is Necessary

MVCC creates old row versions over time.

Without cleanup:

* Table size increases
* Query performance decreases
* Storage becomes inefficient

VACUUM removes dead tuples and maintains database performance.

---

## VACUUM Results

Command:

```sql
VACUUM VERBOSE employees;
```

Important Output:

```text
tuples: 0 removed, 10 remain
dead but not yet removable: 0
```

Interpretation:

* No dead tuples existed.
* The table is clean and requires no# PostgreSQL Internal Architecture Analysis

## Overview

This assignment explores PostgreSQL internal architecture with a focus on:

* Buffer Manager
* B-Tree Index Implementation
* MVCC (Multi-Version Concurrency Control)
* WAL (Write Ahead Logging)
* Query Planning and Statistics

A practical analysis was performed using `EXPLAIN ANALYZE` on a multi-table join query.

---

# 1. Buffer Manager

## Purpose

The PostgreSQL Buffer Manager is responsible for moving pages between disk storage and memory.

Source Location:

```text
src/backend/storage/buffer/
```

PostgreSQL stores table data in fixed-size pages (8 KB). Frequently accessed pages are loaded into Shared Buffers to reduce expensive disk I/O operations.

---

## Shared Buffers

Shared Buffers are a memory cache used by all PostgreSQL backend processes.

Data Flow:

```text
Disk
 ↓
Shared Buffer Cache
 ↓
Query Execution
```

When a query requests data:

1. PostgreSQL checks Shared Buffers.
2. If the page is present, it is used directly.
3. Otherwise, the page is loaded from disk.

---

## Page Caching

The execution plan showed:

```text
Buffers: shared hit=2
```

This indicates that required pages were already present in memory and no additional disk reads were necessary.

This demonstrates successful page caching by the Buffer Manager.

---

## Buffer Replacement

When Shared Buffers become full, PostgreSQL uses a clock-sweep replacement algorithm.

Frequently accessed pages remain in memory while less frequently used pages are replaced.

---

## Page Reads and Writes

### Read Path

```text
Query
 ↓
Buffer Cache Check
 ↓
Page Found?
 ├─ Yes → Use Cached Page
 └─ No → Read From Disk
```

### Write Path

```text
Update
 ↓
Modify Buffer Page
 ↓
Mark Page Dirty
 ↓
Checkpoint
 ↓
Write To Disk
```

---

# 2. B-Tree Index Implementation

Source Location:

```text
src/backend/access/nbtree/
```

An index was created:

```sql
CREATE INDEX idx_emp_dept
ON employees(dept_id);
```

---

## Index Structure

PostgreSQL B-Tree indexes contain:

* Root Pages
* Internal Pages
* Leaf Pages

Search complexity:

```text
O(log n)
```

---

## Insert Operations

When a new indexed row is inserted:

1. PostgreSQL navigates from root to leaf.
2. The key is inserted into the appropriate leaf page.
3. Parent nodes are updated if required.

---

## Page Splits

If a leaf page becomes full:

```text
Before:
[10 20 30 40]

Insert 50

After:
[10 20]
[30 40 50]
```

The tree remains balanced after the split.

---

# 3. MVCC (Multi-Version Concurrency Control)

MVCC allows concurrent readers and writers without blocking.

---

## Heap Tuple Versioning

Instead of modifying rows in place, PostgreSQL creates new tuple versions.

Each tuple stores:

```text
xmin
xmax
```

---

## MVCC Observation

Query:

```sql
SELECT xmin, xmax, *
FROM employees
LIMIT 5;
```

Output:

```text
xmin | xmax
-----+-----
754  | 0
754  | 0
754  | 0
754  | 0
754  | 0
```

Interpretation:

* `xmin = 754` indicates the transaction that created the rows.
* `xmax = 0` indicates that the rows have not been deleted or updated.

---

## Visibility Rules

A transaction sees:

* Committed rows
* Rows visible in its snapshot

A transaction does not see:

* Uncommitted changes from other transactions

This provides snapshot isolation.

---

# Why VACUUM Is Necessary

MVCC creates old row versions over time.

Without cleanup:

* Table size increases
* Query performance decreases
* Storage becomes inefficient

VACUUM removes dead tuples and maintains database performance.

---

## VACUUM Results

Command:

```sql
VACUUM VERBOSE employees;
```

Important Output:

```text
tuples: 0 removed, 10 remain
dead but not yet removable: 0
```

Interpretation:

* No dead tuples existed.
* The table is clean and requires no space reclamation.

---

# 4. Write Ahead Logging (WAL)

WAL ensures durability and crash recovery.

PostgreSQL writes changes to WAL before writing modified pages to disk.

Location:

```text
pg_wal/
```

---

## WAL Position

Command:

```sql
SELECT pg_current_wal_lsn();
```

Output:

```text
0/2F8CC70
```

This Log Sequence Number (LSN) identifies the current WAL position.

---

## Durability Guarantee

PostgreSQL follows the WAL rule:

```text
WAL Flush
    ↓
COMMIT
```

A transaction is considered committed only after WAL records are safely written.

---

## Crash Recovery

If a crash occurs:

```text
Crash
 ↓
Read WAL
 ↓
Replay Changes
 ↓
Recover Database
```

Committed transactions are preserved.

---

## Checkpointing

Dirty pages are periodically written to disk during checkpoints.

Benefits:

* Reduced recovery time
* Improved durability
* Efficient disk writes

---

# 5. Query Planning and Statistics

The PostgreSQL planner uses collected statistics to estimate costs and choose efficient execution plans.

Statistics are stored in:

```text
pg_statistic
```

and exposed through:

```text
pg_stats
```

---

## Statistics Collected

Query:

```sql
SELECT attname, n_distinct
FROM pg_stats
WHERE tablename='employees';
```

Output:

```text
emp_id   | -1
emp_name | -1
dept_id  | -0.5
salary   | -1
```

Interpretation:

* Negative values represent a fraction of total rows.
* PostgreSQL uses these values to estimate selectivity and expected row counts.

---

# EXPLAIN ANALYZE Results

Query:

```sql
SELECT
    e.emp_name,
    d.dept_name,
    e.salary
FROM employees e
JOIN departments d
ON e.dept_id = d.dept_id
WHERE e.salary > 50000;
```

Execution Plan:

```text
Hash Join
 ├── Seq Scan on employees
 └── Seq Scan on departments
```

---

## Chosen Execution Plan

PostgreSQL selected a Hash Join because:

* The tables are very small.
* Building a hash table is inexpensive.
* Sequential scans are cheaper than index lookups for small datasets.

---

## Planner Estimates

Estimated rows:

```text
8 rows
```

Actual rows:

```text
7 rows
```

The estimate is very close to reality, indicating accurate planner statistics.

---

## Execution Statistics

```text
Planning Time: 0.669 ms
Execution Time: 1.277 ms
```

The query executed very quickly due to:

* Small dataset size
* Efficient hash join
* Cached pages in shared buffers

---

## Buffer Usage

```text
Buffers: shared hit=2
```

Interpretation:

* Required pages were already present in memory.
* No additional disk access was required.

This demonstrates effective operation of the Buffer Manager.

---

# Conclusion

This study explored key PostgreSQL internal components.

Key observations:

1. The Buffer Manager efficiently cached pages in Shared Buffers, reducing disk I/O.
2. PostgreSQL uses B-Tree indexes to provide efficient logarithmic-time searches.
3. MVCC enables concurrent transactions using tuple versioning with xmin and xmax metadata.
4. VACUUM is necessary to remove dead tuples and prevent table bloat.
5. WAL guarantees durability and enables crash recovery.
6. Query planning relies heavily on statistics stored in pg_statistic.
7. The planner selected an efficient Hash Join strategy and produced estimates close to actual execution results.

The experiment demonstrates how PostgreSQL combines caching, indexing, MVCC, WAL, and query optimization to deliver reliable and high-performance database operations.
 space reclamation.

---

# 4. Write Ahead Logging (WAL)

WAL ensures durability and crash recovery.

PostgreSQL writes changes to WAL before writing modified pages to disk.

Location:

```text
pg_wal/
```

---

## WAL Position

Command:

```sql
SELECT pg_current_wal_lsn();
```

Output:

```text
0/2F8CC70
```

This Log Sequence Number (LSN) identifies the current WAL position.

---

## Durability Guarantee

PostgreSQL follows the WAL rule:

```text
WAL Flush
    ↓
COMMIT
```

A transaction is considered committed only after WAL records are safely written.

---

## Crash Recovery

If a crash occurs:

```text
Crash
 ↓
Read WAL
 ↓
Replay Changes
 ↓
Recover Database
```

Committed transactions are preserved.

---

## Checkpointing

Dirty pages are periodically written to disk during checkpoints.

Benefits:

* Reduced recovery time
* Improved durability
* Efficient disk writes

---

# 5. Query Planning and Statistics

The PostgreSQL planner uses collected statistics to estimate costs and choose efficient execution plans.

Statistics are stored in:

```text
pg_statistic
```

and exposed through:

```text
pg_stats
```

---

## Statistics Collected

Query:

```sql
SELECT attname, n_distinct
FROM pg_stats
WHERE tablename='employees';
```

Output:

```text
emp_id   | -1
emp_name | -1
dept_id  | -0.5
salary   | -1
```

Interpretation:

* Negative values represent a fraction of total rows.
* PostgreSQL uses these values to estimate selectivity and expected row counts.

---

# EXPLAIN ANALYZE Results

Query:

```sql
SELECT
    e.emp_name,
    d.dept_name,
    e.salary
FROM employees e
JOIN departments d
ON e.dept_id = d.dept_id
WHERE e.salary > 50000;
```

Execution Plan:

```text
Hash Join
 ├── Seq Scan on employees
 └── Seq Scan on departments
```

---

## Chosen Execution Plan

PostgreSQL selected a Hash Join because:

* The tables are very small.
* Building a hash table is inexpensive.
* Sequential scans are cheaper than index lookups for small datasets.

---

## Planner Estimates

Estimated rows:

```text
8 rows
```

Actual rows:

```text
7 rows
```

The estimate is very close to reality, indicating accurate planner statistics.

---

## Execution Statistics

```text
Planning Time: 0.669 ms
Execution Time: 1.277 ms
```

The query executed very quickly due to:

* Small dataset size
* Efficient hash join
* Cached pages in shared buffers

---

## Buffer Usage

```text
Buffers: shared hit=2
```

Interpretation:

* Required pages were already present in memory.
* No additional disk access was required.

This demonstrates effective operation of the Buffer Manager.

---

# Conclusion

This study explored key PostgreSQL internal components.

Key observations:

1. The Buffer Manager efficiently cached pages in Shared Buffers, reducing disk I/O.
2. PostgreSQL uses B-Tree indexes to provide efficient logarithmic-time searches.
3. MVCC enables concurrent transactions using tuple versioning with xmin and xmax metadata.
4. VACUUM is necessary to remove dead tuples and prevent table bloat.
5. WAL guarantees durability and enables crash recovery.
6. Query planning relies heavily on statistics stored in pg_statistic.
7. The planner selected an efficient Hash Join strategy and produced estimates close to actual execution results.

The experiment demonstrates how PostgreSQL combines caching, indexing, MVCC, WAL, and query optimization to deliver reliable and high-performance database operations.
