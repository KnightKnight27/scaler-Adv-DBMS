# PostgreSQL Internal Architecture

## 1. Problem Background

PostgreSQL is an open-source relational database management system designed to provide reliability, scalability, and strong transactional guarantees.

As databases grow larger and support thousands of concurrent users, directly reading and writing data from disk becomes inefficient. PostgreSQL solves this problem through multiple internal components such as the Buffer Manager, B-Tree indexes, MVCC, and Write Ahead Logging (WAL).

These components work together to improve performance, maintain consistency, and ensure durability even during crashes.

---

# 2. Architecture Overview

Application
↓
Query Parser
↓
Planner / Optimizer
↓
Executor
↓
Buffer Manager
↓
Storage Manager
↓
Disk Files + WAL

Major Components:

* Query Parser
* Query Planner
* Executor
* Buffer Manager
* Storage Manager
* WAL Subsystem
* Background Processes

---

# 3. Internal Design Analysis

## Buffer Manager

Location:
src/backend/storage/buffer/

### Purpose

Disk access is extremely slow compared to memory.

The Buffer Manager keeps frequently used database pages in shared memory to reduce disk I/O.

### Shared Buffers

PostgreSQL allocates a shared memory region called Shared Buffers.

Example:

Client Query
↓
Buffer Manager
↓
Shared Buffer Cache
↓
Disk (only if page not found)

### Page Movement

Step 1:
A query requests a table page.

Step 2:
Buffer Manager checks whether page already exists in Shared Buffers.

Step 3:
If present:

* Buffer Hit

Step 4:
If absent:

* Buffer Miss
* Page loaded from disk

Step 5:
Page modified in memory.

Step 6:
Dirty page eventually written back to disk.

### Buffer Replacement

When cache becomes full:

PostgreSQL uses a Clock Sweep algorithm.

Reason:

* Lower overhead than pure LRU
* Better scalability under concurrency

### Analysis

Design Choice:
Cache pages in memory.

Benefit:
Reduces expensive disk reads.

Trade-off:
Consumes RAM.

---

## B-Tree Index Implementation

Location:
src/backend/access/nbtree/

### Structure

Root Page
│
├── Internal Pages
│
└── Leaf Pages

### Search Path

For:

SELECT * FROM users WHERE id = 100;

Process:

Root
→ Internal Node
→ Leaf Node
→ Record Location

Time Complexity:

O(log n)

### Insert Operation

When inserting:

1. Locate leaf page.
2. Insert key.
3. If page full:

   * Split page.
4. Update parent.

### Page Splits

Before:

[10 20 30 40]

Insert 50

After:

[10 20]
↑ Parent
[30 40 50]

### Analysis

Why B-Trees?

Advantages:

* Balanced structure
* Predictable lookup performance
* Efficient range scans

Trade-off:

* Page splits increase write cost

---

## MVCC (Multi Version Concurrency Control)

### Motivation

Problem:

Without MVCC:

Reader blocks Writer
Writer blocks Reader

Concurrency becomes poor.

### PostgreSQL Solution

Create multiple versions of tuples.

Example:

Row:
Salary = 50000

Transaction T1 updates salary.

Instead of modifying row:

Old Version:
Salary = 50000

New Version:
Salary = 60000

Both versions temporarily coexist.

### xmin and xmax

Every tuple contains:

xmin:
Transaction that created row

xmax:
Transaction that deleted row

### Visibility Rules

Transaction sees row if:

xmin committed
AND
xmax not committed

### Snapshot Isolation

Each transaction sees a consistent snapshot.

Benefits:

* Readers do not block writers
* Writers do not block readers

### Why VACUUM is Necessary

Old tuple versions accumulate.

Example:

Version 1
Version 2
Version 3

Only latest version useful.

VACUUM:

* Removes dead tuples
* Reclaims storage
* Prevents table bloat

### Analysis

Benefit:
Excellent concurrency.

Trade-off:
Extra storage overhead.

---

## WAL (Write Ahead Logging)

### Motivation

What if power fails during write?

Database may become corrupted.

### WAL Principle

Write log first.
Write data later.

Process:

Transaction
↓
WAL Record
↓
Disk Flush
↓
Actual Data Page

### WAL Records

Contain:

* Insert operations
* Updates
* Deletes
* Page modifications

### Crash Recovery

After crash:

1. Read WAL.
2. Replay committed operations.
3. Restore database consistency.

### Checkpointing

Writing every dirty page immediately is expensive.

PostgreSQL periodically creates checkpoints.

Checkpoint:

* Flush dirty pages
* Record recovery position

Benefits:

* Faster crash recovery

Trade-off:

* Temporary I/O spike

---

# 4. Query Planning and Statistics

## Experiment

Example Query

SELECT c.customer_name,
SUM(o.amount)
FROM customers c
JOIN orders o
ON c.id = o.customer_id
GROUP BY c.customer_name;

Run:

EXPLAIN ANALYZE
SELECT ...

### Planner Responsibilities

Planner estimates:

* Row counts
* Join costs
* Index usefulness

### Statistics Source

Collected by:

ANALYZE

Stored in:

pg_statistic

Contains:

* Distinct values
* Data distribution
* Null fractions

### Why Statistics Matter

Bad statistics
→ Bad estimates
→ Poor execution plans

Good statistics
→ Better performance

### Observation

Query planning depends heavily on accurate statistics.

Therefore PostgreSQL regularly collects statistics and updates planner metadata.

---

# 5. Design Trade-Offs

| Component      | Benefit          | Trade-Off            |
| -------------- | ---------------- | -------------------- |
| Buffer Manager | Faster reads     | Uses RAM             |
| B-Tree         | Fast lookups     | Page split overhead  |
| MVCC           | High concurrency | Requires VACUUM      |
| WAL            | Durability       | Extra write overhead |
| Statistics     | Better plans     | Maintenance cost     |

---

# 6. Key Learnings

1. Buffer Manager reduces disk access through shared page caching.
2. B-Tree indexes provide logarithmic search performance.
3. MVCC allows readers and writers to work concurrently.
4. VACUUM is essential because MVCC creates dead tuples.
5. WAL guarantees durability and crash recovery.
6. Query optimization depends heavily on collected statistics.
7. PostgreSQL's architecture prioritizes correctness, concurrency, and reliability over implementation simplicity.
