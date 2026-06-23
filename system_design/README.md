# Topic 3: MySQL / InnoDB Storage Engine

**Course:** Advanced Database Management Systems  
**Topic:** MySQL / InnoDB Storage Engine  
**Student:** Adam B Shohe
**Roll Number:** 24BCS10017

---

# 1. Introduction

InnoDB is the default storage engine of MySQL and is designed to provide high performance, reliability, crash recovery, and ACID-compliant transaction processing. It supports transactions, row-level locking, foreign keys, and Multi-Version Concurrency Control (MVCC).

Unlike PostgreSQL, which stores data using a heap structure and creates new tuple versions for updates, InnoDB uses clustered storage, undo logs, and redo logs to maintain consistency and recoverability.

---

# 2. InnoDB Architecture Overview

The major components of InnoDB include:

- Clustered indexes
- Primary key storage
- Secondary indexes
- Buffer pool
- Undo logs
- Redo logs
- Row-level locks
- Gap locks
- Transaction manager

These components work together to provide efficient data access, concurrency control, and crash recovery.

---

# 3. Clustered Indexes

## Definition

In InnoDB, the table data itself is physically stored in the primary key index. This structure is called a **clustered index**.

The rows are arranged on disk according to the primary key order.

### Example

```sql
CREATE TABLE Students (
    id INT PRIMARY KEY,
    name VARCHAR(50),
    age INT
);
```

Rows are stored physically in the order of `id`.

---

## Advantages of Clustered Indexes

### 1. Faster Primary Key Lookups

Since the actual row data is stored in the primary key B+ Tree, locating a row requires only one index traversal.

```sql
SELECT * FROM Students WHERE id = 10;
```

The engine directly reaches the data record.

---

### 2. Efficient Range Queries

Because rows are physically ordered:

```sql
SELECT * FROM Students
WHERE id BETWEEN 100 AND 200;
```

The database can scan adjacent pages sequentially.

---

### 3. Better Cache Locality

Related rows are stored near each other, reducing disk I/O and improving buffer utilization.

---

# 4. Primary Key Storage

In InnoDB:

- The primary key is the clustered index.
- Table rows are stored inside the leaf nodes.
- Every table must have a clustered index.

If no primary key exists:

1. InnoDB looks for a unique non-null key.
2. If none exists, it creates a hidden 6-byte row ID.

---

# 5. Secondary Indexes

Secondary indexes do not store complete rows.

Instead, they store:

```
Secondary Key → Primary Key
```

Example:

```sql
CREATE INDEX idx_name
ON Students(name);
```

The index stores:

| Name | Primary Key |
|------|-------------|
| Alice | 5 |
| Bob | 8 |
| John | 12 |

---

## Secondary Index Lookup

Query:

```sql
SELECT * FROM Students
WHERE name = 'John';
```

Steps:

1. Search secondary index.
2. Obtain primary key value.
3. Traverse clustered index.
4. Fetch actual row.

This extra lookup is called:

> Secondary Index Lookup or Double Read.

---

# 6. Buffer Pool

The buffer pool is InnoDB's main memory cache.

It stores:

- Data pages
- Index pages
- Undo pages
- Frequently accessed information

---

## Purpose

- Reduces disk I/O.
- Speeds up queries.
- Improves transaction performance.

---

## Working

1. Query requests a page.
2. Buffer pool checks if page exists in memory.
3. If present → cache hit.
4. Otherwise → read from disk.

---

## Benefits

- Faster reads.
- Faster updates.
- Reduced storage access.
- Better overall throughput.

---

# 7. Undo Logs

Undo logs store the previous versions of modified rows.

Example:

| Before | After |
|-------|--------|
| Salary = 5000 | Salary = 7000 |

Undo log:

```
Salary = 5000
```

---

## Purposes of Undo Logs

### 1. Transaction Rollback

```sql
START TRANSACTION;

UPDATE Employees
SET salary = 7000;

ROLLBACK;
```

The old value is restored.

---

### 2. MVCC

When a transaction reads older data, InnoDB reconstructs previous versions using undo logs.

This enables:

- Consistent reads
- Snapshot isolation
- Non-blocking reads

---

# 8. Redo Logs

Redo logs record modifications made to pages.

They guarantee durability.

Example:

```
Update page 120:
Salary = 7000
```

The change is first written to the redo log.

---

## Purpose of Redo Logs

### 1. Crash Recovery

Suppose:

1. Data updated.
2. Redo log written.
3. System crashes before data page reaches disk.

During restart:

- Redo log is replayed.
- Changes are restored.

---

### 2. Write-Ahead Logging

InnoDB follows:

> Log first, data later.

This improves performance because sequential log writes are faster than random page writes.

---

# 9. Why Does InnoDB Need Both Undo and Redo Logs?

The two logs solve different problems.

| Undo Log | Redo Log |
|---------|----------|
| Restores old values | Reapplies new values |
| Supports rollback | Supports recovery |
| Enables MVCC | Ensures durability |
| Moves backward | Moves forward |

---

## Example

Transaction:

```sql
UPDATE Accounts
SET balance = 5000
WHERE id = 1;
```

Undo log:

```
balance = 3000
```

Redo log:

```
balance = 5000
```

If rollback occurs:

- Undo log is used.

If crash occurs:

- Redo log is used.

Therefore, both are necessary.

---

# 10. Row-Level Locking

InnoDB uses row-level locks instead of table-level locks.

Example:

Transaction T1:

```sql
UPDATE Accounts
SET balance = 5000
WHERE id = 1;
```

Only row 1 is locked.

Other rows remain accessible.

---

## Advantages

- High concurrency.
- Multiple users can update different rows.
- Better throughput.

---

# 11. Gap Locks

Gap locks lock the spaces between index records.

Example:

Existing values:

```
10, 20, 30
```

Transaction:

```sql
SELECT *
FROM Orders
WHERE id BETWEEN 10 AND 30
FOR UPDATE;
```

The gaps are also locked.

This prevents:

- Phantom rows.
- New insertions inside the range.

---

## Example

Transaction T1:

```sql
SELECT *
FROM Orders
WHERE id BETWEEN 10 AND 30
FOR UPDATE;
```

Transaction T2:

```sql
INSERT INTO Orders VALUES (25);
```

T2 must wait.

---

# 12. Transaction Processing

InnoDB follows the ACID properties.

## Atomicity

Either all operations occur or none occur.

---

## Consistency

Database constraints remain valid.

---

## Isolation

Transactions do not interfere improperly.

---

## Durability

Committed data survives crashes.

---

# 13. Isolation Levels

InnoDB supports four isolation levels.

| Isolation Level | Dirty Reads | Non-repeatable Reads | Phantom Reads |
|----------------|------------|---------------------|--------------|
| READ UNCOMMITTED | Yes | Yes | Yes |
| READ COMMITTED | No | Yes | Yes |
| REPEATABLE READ | No | No | No* |
| SERIALIZABLE | No | No | No |

\* InnoDB prevents many phantom reads using next-key locking.

---

## Default Level

InnoDB uses:

```text
REPEATABLE READ
```

by default.

---

# 14. PostgreSQL MVCC

PostgreSQL uses a different MVCC implementation.

When rows are updated:

1. Old row remains.
2. New row version is created.
3. Readers choose the correct version.

Example:

```
Version 1 → salary = 5000
Version 2 → salary = 7000
```

Old versions remain until VACUUM removes them.

---

# 15. PostgreSQL vs InnoDB

| Feature | InnoDB | PostgreSQL |
|--------|--------|-----------|
| Storage | Clustered | Heap |
| Updates | In-place | New tuple |
| MVCC | Undo logs | Tuple versions |
| Cleanup | Purge thread | VACUUM |
| Primary key storage | Clustered index | Separate index |
| Row location | Primary key | Physical tuple |
| Secondary index | Stores PK | Stores tuple pointer |
| Recovery | Redo logs | WAL |
| Default isolation | REPEATABLE READ | READ COMMITTED |

---

# 16. Why Did PostgreSQL Choose a Different MVCC Model?

PostgreSQL uses tuple versioning because:

1. Readers never block writers.
2. Writers rarely block readers.
3. Historical versions exist directly in the table.
4. Snapshot visibility becomes simple.

However, old tuples accumulate.

Therefore PostgreSQL requires:

```text
VACUUM
```

to reclaim space.

---

# 17. Trade-Offs Between InnoDB and PostgreSQL

## InnoDB Advantages

- Fast primary key lookups.
- Efficient clustered storage.
- Good OLTP performance.
- Strong crash recovery.
- Smaller storage overhead.

---

## InnoDB Disadvantages

- Secondary index lookups require extra traversal.
- Gap locks can reduce concurrency.
- Clustered index updates can be expensive.

---

## PostgreSQL Advantages

- Excellent MVCC implementation.
- Readers rarely block writers.
- Rich indexing options.
- Strong analytical capabilities.

---

## PostgreSQL Disadvantages

- Requires VACUUM.
- Storage bloat from old tuples.
- Larger disk usage.

---

# 18. Conclusion

InnoDB provides a highly optimized transactional storage engine through clustered indexes, undo logs, redo logs, buffer pools, and row-level locking. These mechanisms allow efficient query execution, crash recovery, and high concurrency.

PostgreSQL follows a different MVCC design based on tuple versioning and VACUUM cleanup. Both systems achieve transaction isolation and consistency, but they make different engineering trade-offs.

InnoDB prioritizes clustered storage and log-based recovery, while PostgreSQL emphasizes append-only updates and version-based concurrency control.

---

# Suggested Questions

### 1. Why does InnoDB need both undo and redo logs?

Undo logs support rollback and MVCC, while redo logs ensure durability and crash recovery.

---

### 2. What advantages do clustered indexes provide?

- Faster primary key lookups.
- Better range scans.
- Improved cache locality.
- Reduced disk I/O.

---

### 3. Why did PostgreSQL choose a different MVCC model?

PostgreSQL uses tuple versioning to minimize reader-writer blocking and simplify snapshot visibility, at the cost of storage bloat and VACUUM maintenance.

---

# References

1. MySQL 8.0 Documentation
2. InnoDB Storage Engine Architecture
3. PostgreSQL Documentation
4. Database System Concepts by Silberschatz
5. Designing Data-Intensive Applications by Martin Kleppmann