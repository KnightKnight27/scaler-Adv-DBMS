# MySQL / InnoDB Storage Engine Architecture

## 1. Problem Background

MySQL is one of the most widely used relational database systems in production environments. Modern MySQL deployments primarily use the InnoDB storage engine, which became the default engine because of its support for:

* ACID transactions
* Crash recovery
* Row-level locking
* MVCC (Multi-Version Concurrency Control)
* High concurrency
* Efficient indexing

Before InnoDB became dominant, MySQL commonly used MyISAM, which lacked transactional guarantees and crash recovery mechanisms. As applications became larger and more concurrent, InnoDB was adopted as the default storage engine.

The design goal of InnoDB is to provide strong consistency and durability while maintaining high read and write performance for OLTP (Online Transaction Processing) workloads.

---

# 2. Architecture Overview

## High-Level Architecture

```text
                    +--------------------+
                    | Client Applications |
                    +--------------------+
                              |
                              v
                    +--------------------+
                    | SQL Layer          |
                    +--------------------+
                              |
                              v
                    +--------------------+
                    | InnoDB Engine      |
                    +--------------------+
                              |
          +-------------------+-------------------+
          |                   |                   |
          v                   v                   v
   Buffer Pool         Undo Logs          Redo Logs
          |                                   |
          +-------------------+---------------+
                              |
                              v
                       Data Files
```

## Main Components

| Component           | Purpose                      |
| ------------------- | ---------------------------- |
| Clustered Index     | Stores table rows physically |
| Secondary Index     | Accelerates searches         |
| Buffer Pool         | Memory cache for pages       |
| Undo Log            | Supports MVCC and rollback   |
| Redo Log            | Crash recovery               |
| Lock Manager        | Concurrency control          |
| Transaction Manager | ACID guarantees              |

---

# 3. Clustered Index Architecture

One of InnoDB's most important design decisions is the use of clustered indexes.

## Experiment

```sql
SHOW INDEX FROM customers;
```

Output:

```text
PRIMARY | customer_id | BTREE
```

Observation:

The PRIMARY KEY automatically becomes the clustered index.

## How Clustered Storage Works

Unlike PostgreSQL, InnoDB stores table rows directly inside the leaf pages of the primary key B-Tree.

```text
PRIMARY KEY B-Tree

        [50]
       /    \
      /      \
   [20]      [80]

Leaf Pages:

(1, Alice, Bangalore)
(2, Bob, Delhi)
(3, Charlie, Mumbai)
```

The actual row data exists in the leaf nodes.

### Benefits

* Fast primary key lookups
* Improved locality of related rows
* Fewer disk reads

### Drawbacks

* Large primary keys increase storage overhead
* Changing primary keys is expensive
* Secondary indexes become larger

---

# 4. Secondary Indexes

## Experiment

```sql
CREATE INDEX idx_city
ON customers(city);
```

Verification:

```sql
SHOW INDEX FROM customers;
```

Output:

```text
PRIMARY
idx_city
```

Query:

```sql
EXPLAIN FORMAT=TREE
SELECT *
FROM customers
WHERE city='Bangalore';
```

Output:

```text
Index lookup on customers using idx_city
```

## Internal Structure

Unlike PostgreSQL, secondary index entries do not store heap pointers.

Instead:

```text
Secondary Index Entry

city -> PRIMARY KEY
```

Example:

```text
Bangalore -> customer_id = 1
Bangalore -> customer_id = 5
```

The lookup process:

```text
Secondary Index
      |
      v
Primary Key Lookup
      |
      v
Actual Row
```

This is known as a double lookup.

---

# 5. Buffer Pool

The Buffer Pool is the most important memory component in InnoDB.

It caches:

* Table pages
* Index pages
* Frequently accessed data

## Experiment

```sql
SHOW VARIABLES
LIKE 'innodb_buffer_pool_size';
```

Output:

```text
134217728 bytes
```

Equivalent to:

```text
128 MB
```

---

## Buffer Pool Statistics

From:

```sql
SHOW ENGINE INNODB STATUS\G
```

Output:

```text
Buffer pool size 8192
Free buffers 7007
Database pages 1181
```

### Interpretation

* 8192 pages allocated
* 1181 pages currently occupied
* 7007 pages available

The Buffer Pool reduces expensive disk reads by serving frequently accessed pages directly from memory.

---

# 6. Undo Logs and MVCC

InnoDB implements MVCC differently from PostgreSQL.

Instead of storing multiple tuple versions directly in the table, InnoDB stores previous versions in Undo Logs.

---

## Experiment

Transaction A:

```sql
UPDATE customers
SET city='Kolkata'
WHERE customer_id=1;
```

Not committed.

Transaction B:

```sql
SELECT *
FROM customers
WHERE customer_id=1;
```

Output:

```text
Alice | Bangalore
```

### Observation

The reader still observed the committed version.

This demonstrates MVCC.

Internally:

```text
Current Row
     |
     v
Undo Log
     |
     v
Older Version
```

Readers reconstruct historical versions using Undo Records.

---

## Why Undo Logs Exist

Undo Logs support:

1. Transaction Rollback
2. Consistent Reads
3. MVCC Snapshots

Without Undo Logs:

* Rollbacks would be impossible
* Snapshot isolation could not be implemented

---

# 7. Redo Logs

Redo Logs guarantee durability.

Principle:

```text
Log First
Data Later
```

Before modifying a data page:

1. Change recorded in Redo Log
2. Log flushed
3. Transaction commits
4. Data pages written later

---

## Experiment

Before Insert:

```text
Log sequence number
155492698
```

After Insert:

```sql
INSERT INTO customers(name,city)
VALUES('Redo_Test','Bangalore');
```

New Value:

```text
Log sequence number
155497395
```

Increase:

```text
4697 bytes
```

### Observation

The insert operation generated Redo Log records.

This demonstrates that all changes are first recorded in the WAL-style Redo Log before becoming permanent.

---

## Why Redo Logs Exist

If the database crashes:

```text
Crash
  |
  v
Redo Log Replay
  |
  v
Restore Changes
```

Redo Logs guarantee the Durability property of ACID.

---

# 8. Row-Level Locking

InnoDB supports row-level locks.

Only rows being modified are locked.

Advantages:

* High concurrency
* Better scalability
* Reduced contention

Example:

```sql
UPDATE customers
SET city='Delhi'
WHERE customer_id=1;
```

Only the matching row is locked.

Other rows remain accessible.

---

# 9. Gap Locks

Gap Locks are unique to InnoDB's implementation of Repeatable Read isolation.

Purpose:

Prevent phantom reads.

---

## Experiment

Transaction:

```sql
START TRANSACTION;

SELECT *
FROM customers
WHERE customer_id BETWEEN 1 AND 5
FOR UPDATE;
```

Concurrent Insert:

```sql
INSERT INTO customers(customer_id,name,city)
VALUES(3,'GapTest','Delhi');
```

Result:

```text
ERROR 1205 (HY000)
Lock wait timeout exceeded
```

### Observation

The insert could not proceed because the range lock protected keys within the selected interval.

This demonstrates InnoDB's next-key locking mechanism.

---

## Why Gap Locks Exist

Without gap locks:

```text
Transaction A:
SELECT COUNT(*)
WHERE id BETWEEN 1 AND 5

Transaction B:
INSERT id=3

Transaction A:
SELECT COUNT(*) again
```

The result could change unexpectedly.

Gap locks prevent such phantom rows.

---

# 10. Transaction Processing

InnoDB transaction workflow:

```text
BEGIN
   |
   v
Modify Rows
   |
   v
Generate Undo Records
   |
   v
Generate Redo Records
   |
   v
Commit
```

This architecture guarantees:

* Atomicity
* Consistency
* Isolation
* Durability

---

# 11. PostgreSQL vs InnoDB

## Storage Model

| PostgreSQL                      | InnoDB                        |
| ------------------------------- | ----------------------------- |
| Heap Storage                    | Clustered Storage             |
| Index points to heap tuple      | Primary key stores actual row |
| Multiple tuple versions in heap | Versions stored in Undo Logs  |

---

## MVCC

### PostgreSQL

```text
UPDATE
   |
   v
Create New Tuple
```

Old tuple remains in table.

Cleanup:

```text
VACUUM
```

required.

---

### InnoDB

```text
UPDATE
   |
   v
Modify Row
   |
   v
Store Old Version
In Undo Log
```

No VACUUM equivalent required.

---

## Concurrency

| PostgreSQL                  | InnoDB                      |
| --------------------------- | --------------------------- |
| Tuple versioning            | Undo-based MVCC             |
| VACUUM cleanup              | Purge thread cleanup        |
| Snapshot Isolation          | Repeatable Read             |
| Readers don't block writers | Readers don't block writers |

---

## Why PostgreSQL Chose a Different Design

PostgreSQL's MVCC:

Advantages:

* Simpler visibility rules
* Excellent read scalability
* Historical versions stored directly in heap

Disadvantages:

* Table bloat
* Requires VACUUM

InnoDB's MVCC:

Advantages:

* Smaller tables
* Efficient clustered storage

Disadvantages:

* More complex Undo infrastructure
* Additional bookkeeping

---

# 12. Design Trade-Offs

| Feature         | Benefit          | Cost                     |
| --------------- | ---------------- | ------------------------ |
| Clustered Index | Fast PK lookups  | Larger secondary indexes |
| Buffer Pool     | Reduced I/O      | Memory consumption       |
| Undo Logs       | MVCC support     | Additional storage       |
| Redo Logs       | Durability       | Extra writes             |
| Gap Locks       | Prevent phantoms | Reduced concurrency      |

---

# 13. Key Learnings

1. InnoDB stores table rows directly inside clustered index leaf pages.

2. Secondary indexes reference primary keys rather than physical row locations.

3. The Buffer Pool is responsible for caching table and index pages.

4. Undo Logs enable MVCC and transaction rollback.

5. Redo Logs provide crash recovery and durability.

6. Row-level locking allows high concurrency.

7. Gap Locks prevent phantom reads under Repeatable Read isolation.

8. InnoDB's MVCC design differs significantly from PostgreSQL's tuple-versioning model.

9. PostgreSQL trades storage overhead for simpler MVCC visibility rules, while InnoDB trades implementation complexity for more compact storage.

10. Modern OLTP systems rely heavily on these internal mechanisms to maintain both correctness and performance.

# References

1. MySQL 8.0 Documentation
2. InnoDB Storage Engine Documentation
3. InnoDB Architecture Whitepapers
4. PostgreSQL Documentation
5. Database System Concepts – Silberschatz, Korth, Sudarshan
