# MySQL InnoDB Storage Engine Analysis

## Objective

The objective of this project is to study the internal architecture of the MySQL InnoDB Storage Engine and compare its design with PostgreSQL.

The study focuses on:

* Clustered Indexes
* Primary Key Storage
* Secondary Indexes
* Buffer Pool
* Undo Logs
* Redo Logs
* Row-Level Locking
* Gap Locks
* Transaction Processing
* Comparison with PostgreSQL

---

# 1. InnoDB Architecture Overview

InnoDB is the default storage engine used by MySQL and provides:

* ACID-compliant transactions
* MVCC (Multi-Version Concurrency Control)
* Row-level locking
* Crash recovery
* High-performance indexing

Architecture Overview:

```text
Client Query
     ↓
SQL Layer
     ↓
InnoDB Storage Engine
     ├── Buffer Pool
     ├── Clustered Index
     ├── Secondary Indexes
     ├── Undo Logs
     ├── Redo Logs
     └── Lock Manager
```

---

# 2. Clustered Indexes

## Definition

A clustered index stores actual table rows inside the leaf pages of the primary key B-Tree.

Table used:

```sql
CREATE TABLE employees (
    id INT PRIMARY KEY,
    name VARCHAR(50),
    department VARCHAR(50),
    salary INT
) ENGINE=InnoDB;
```

Since `id` is the primary key, InnoDB automatically creates a clustered index.

Structure:

```text
PRIMARY KEY B-TREE

1 → Alice
2 → Bob
3 → Charlie
4 → David
5 → Eva
```

The row data is stored directly with the primary key entries.

---

## Advantages of Clustered Indexes

1. Fast primary-key lookups.
2. Fewer disk accesses.
3. Better cache locality.
4. Efficient range scans.

---

## Experimental Verification

Query:

```sql
EXPLAIN
SELECT *
FROM employees
WHERE id = 3;
```

Output:

```text
key = PRIMARY
type = const
rows = 1
```

Analysis:

* PostgreSQL estimated a single-row lookup.
* The PRIMARY clustered index was used.
* Only one row needed to be examined.

This demonstrates the efficiency of clustered storage.

---

# 3. Secondary Indexes

A secondary index was created:

```sql
CREATE INDEX idx_department
ON employees(department);
```

Unlike clustered indexes, secondary indexes do not store complete rows.

Instead they store:

```text
Indexed Value
+
Primary Key
```

Example:

```text
IT        → 1
IT        → 2
HR        → 3
Finance   → 4
Marketing → 5
```

---

## Experimental Verification

Query:

```sql
EXPLAIN
SELECT *
FROM employees
WHERE department='IT';
```

Output:

```text
key = idx_department
type = ref
rows = 2
```

Analysis:

* MySQL selected the secondary index.
* Two matching rows were expected.
* After locating matching primary keys, InnoDB performs clustered index lookups to retrieve full rows.

This demonstrates how secondary indexes work in InnoDB.

---

# 4. Buffer Pool

The Buffer Pool is InnoDB's memory cache.

Purpose:

* Cache table pages
* Cache index pages
* Reduce disk I/O
* Improve query performance

Data Flow:

```text
Disk
 ↓
Buffer Pool
 ↓
Query Execution
```

---

## Experimental Observation

From:

```sql
SHOW ENGINE INNODB STATUS\G
```

Output:

```text
Buffer pool size   8192
Free buffers       7034
Database pages     1154
Modified db pages  0
```

Interpretation:

* Buffer Pool contains 8192 pages.
* 1154 pages currently hold database content.
* No dirty pages existed during observation.
* Most buffers were available because the workload was small.

---

# 5. Undo Logs

Undo logs store previous versions of modified rows.

Purpose:

1. Rollback support.
2. MVCC implementation.
3. Consistent reads.
4. Recovery from transaction failures.

Example:

```sql
UPDATE employees
SET salary=60000
WHERE id=1;
```

Undo Log stores:

```text
Previous Salary = 50000
```

If rollback occurs:

```sql
ROLLBACK;
```

The old value can be restored.

---

# 6. Redo Logs

Redo logs record changes before modified pages are written to disk.

Purpose:

1. Durability.
2. Crash recovery.
3. Faster commits.
4. Recovery after system failures.

---

## Experimental Observation

From InnoDB Status:

```text
Log sequence number      174862975
Log written up to        174862975
Log flushed up to        174862975
Last checkpoint at       174862975
```

Interpretation:

* All log records were written successfully.
* Log records were flushed to disk.
* Checkpoint progress matched the current log position.

This indicates a healthy redo logging system.

---

# Why InnoDB Needs Both Undo and Redo Logs

Undo Logs:

```text
Go Backward
```

Used for:

* Rollback
* MVCC visibility

Redo Logs:

```text
Go Forward
```

Used for:

* Crash recovery
* Durability

Both are necessary because they solve different problems.

---

# 7. Row-Level Locking

InnoDB supports row-level locks.

Example:

```sql
BEGIN;

UPDATE employees
SET salary = 65000
WHERE id = 1;
```

Only the selected row is locked.

Advantages:

* High concurrency
* Reduced blocking
* Better scalability

---

# 8. Gap Locks

Gap locks protect ranges between index entries.

Example:

```sql
SELECT *
FROM employees
WHERE id BETWEEN 5 AND 10
FOR UPDATE;
```

InnoDB locks:

```text
Existing Rows
+
Gaps Between Rows
```

Purpose:

* Prevent phantom reads.
* Maintain transaction consistency.

---

# 9. Transaction Processing

Transaction Lifecycle:

```text
BEGIN
 ↓
Read Data
 ↓
Modify Data
 ↓
Undo Log Generated
 ↓
Redo Log Generated
 ↓
COMMIT
```

This process ensures ACID compliance.

---

# 10. InnoDB Monitor Observations

Using:

```sql
SHOW ENGINE INNODB STATUS\G
```

Important observations:

### Transactions

```text
History list length 6
```

This indicates some undo history exists and can be used for MVCC visibility.

### Row Operations

```text
Number of rows inserted 5
updated 0
deleted 0
read 1
```

These values match the experimental workload.

### Buffer Pool

```text
Buffer pool size 8192
Database pages 1154
```

Pages were successfully cached in memory.

### Logging

```text
Log flushed up to 174862975
```

Confirms successful redo log flushing.

---

# 11. PostgreSQL vs InnoDB

| Feature                | PostgreSQL               | InnoDB             |
| ---------------------- | ------------------------ | ------------------ |
| Storage Layout         | Heap Storage             | Clustered Storage  |
| Updates                | Append New Tuple Version | In-place Update    |
| MVCC                   | Tuple Versioning         | Undo Log Based     |
| Cleanup                | VACUUM                   | Purge Thread       |
| Primary Key Storage    | Separate Heap            | Clustered Index    |
| Secondary Index Lookup | Direct Heap Pointer      | Primary Key Lookup |
| Recovery               | WAL                      | Redo Logs          |
| Rollback               | Visibility Rules         | Undo Logs          |

---

# Why PostgreSQL Chose a Different MVCC Model

PostgreSQL stores multiple tuple versions directly in the table.

Advantages:

* Simple visibility checks.
* Strong snapshot isolation.
* No dependency on undo chains.

Disadvantages:

* Table bloat.
* Requires VACUUM cleanup.

InnoDB stores older versions in Undo Logs.

Advantages:

* Smaller tables.
* Less storage bloat.

Disadvantages:

* More complex MVCC implementation.
* Longer undo chains under heavy updates.

---

# Conclusion

This study explored the MySQL InnoDB Storage Engine and compared it with PostgreSQL.

Key findings:

1. Clustered indexes improve lookup performance because rows are stored directly within the primary key B-Tree.
2. Secondary indexes store indexed values along with primary key references.
3. The Buffer Pool reduces disk I/O by caching data and index pages.
4. Undo logs provide rollback functionality and support MVCC.
5. Redo logs guarantee durability and enable crash recovery.
6. Row-level locking improves concurrency while gap locks prevent phantom reads.
7. PostgreSQL and InnoDB implement MVCC differently, each with unique trade-offs in performance, storage usage, and maintenance requirements.