# PostgreSQL Internal Architecture Analysis

## 1. Problem Background

PostgreSQL is one of the most advanced open-source relational database systems available today. Unlike lightweight embedded databases, PostgreSQL is designed for reliability, scalability, high concurrency, and strong transactional guarantees.

Over the years, PostgreSQL has become a popular choice for enterprise applications, financial systems, SaaS platforms, and large-scale web backends. Its success comes from several important internal components, including the Buffer Manager, Multi-Version Concurrency Control (MVCC), B-Tree indexes, and Write Ahead Logging (WAL).

The goal of this report is to study these internal components and understand the engineering decisions that allow PostgreSQL to provide high performance while maintaining data consistency and durability.

---

# 2. Architecture Overview

## High-Level Architecture

```text id="3mw90v"
Client
   |
   v
+-------------------+
| PostgreSQL Server |
+-------------------+
   |
   +-------------------+
   | Query Planner     |
   +-------------------+
   |
   +-------------------+
   | Buffer Manager    |
   +-------------------+
   |
   +-------------------+
   | Storage Manager   |
   +-------------------+
   |
   +-------------------+
   | WAL Manager       |
   +-------------------+
   |
   v
+-------------------+
| Data Files        |
+-------------------+
```

When a query arrives, PostgreSQL parses it, creates an execution plan, accesses required pages through the Buffer Manager, updates data if necessary, records changes in WAL, and finally stores data on disk.

---

# 3. Buffer Manager

The Buffer Manager is responsible for caching database pages in memory.

Reading data directly from disk for every query would be extremely slow. Instead, PostgreSQL keeps frequently accessed pages in a shared memory region called Shared Buffers.

### Shared Buffers

Shared Buffers act as a cache between PostgreSQL and disk storage.

Process:

1. Query requests a page.
2. Buffer Manager checks Shared Buffers.
3. If page exists, it is returned immediately.
4. Otherwise the page is loaded from disk.

This significantly reduces disk I/O.

---

### Page Reads and Writes

When a page is modified:

1. Page is updated in memory.
2. Page becomes dirty.
3. WAL record is generated.
4. Dirty page is flushed later.

This approach improves performance because disk writes can be delayed and grouped together.

---

### Buffer Replacement

Memory is limited, so PostgreSQL cannot keep every page in memory.

When Shared Buffers become full, PostgreSQL selects pages for eviction using a clock-sweep algorithm.

Advantages:

* Low overhead.
* Efficient page management.
* Better performance than simple FIFO replacement.

---

# 4. B-Tree Index Implementation

B-Tree is the default index type in PostgreSQL.

It is optimized for:

* Equality lookups.
* Range queries.
* Ordered scans.

---

## Index Structure

A B-Tree consists of:

```text id="qvx3pf"
Root Page
   |
Internal Pages
   |
Leaf Pages
```

Leaf pages contain references to actual table rows.

Because the tree remains balanced, searches require very few page accesses even for large datasets.

---

## Search Path

When PostgreSQL searches for a value:

1. Start at root page.
2. Follow internal node pointers.
3. Reach leaf page.
4. Locate target entry.

Complexity is approximately:

```text id="a5hclz"
O(log n)
```

which remains efficient as data grows.

---

## Insert Operations

New values are inserted into leaf pages.

If sufficient space exists:

```text id="qqp5qt"
Insert directly
```

Otherwise PostgreSQL performs a page split.

---

## Page Splits

When a page becomes full:

1. Create new page.
2. Move approximately half of entries.
3. Update parent page.
4. Maintain tree balance.

This ensures search performance remains stable.

---

# 5. Multi-Version Concurrency Control (MVCC)

MVCC is one of PostgreSQL's most important features.

Instead of modifying rows directly, PostgreSQL creates new versions of rows whenever updates occur.

Example:

```text id="d4rj3f"
Version 1 -> Salary = 50000
Version 2 -> Salary = 55000
```

Older transactions can still view Version 1 while newer transactions see Version 2.

---

## xmin and xmax

Every tuple contains metadata.

### xmin

Stores the transaction ID that created the row.

### xmax

Stores the transaction ID that deleted or replaced the row.

Example:

```text id="j7e0s6"
xmin = 100
xmax = 120
```

This information helps PostgreSQL determine row visibility.

---

## Snapshot Isolation

Each transaction receives a snapshot of visible data.

Benefits:

* Consistent reads.
* High concurrency.
* Reduced locking.

Readers generally do not block writers, and writers generally do not block readers.

This is one of the primary reasons PostgreSQL performs well under concurrent workloads.

---

# 6. VACUUM

Because PostgreSQL creates new row versions during updates, old versions accumulate over time.

These obsolete versions are called dead tuples.

VACUUM is responsible for:

* Removing dead tuples.
* Reclaiming storage.
* Updating visibility information.
* Preventing transaction ID wraparound.

Without VACUUM, storage consumption would continuously increase.

---

# 7. Write Ahead Logging (WAL)

Durability is achieved through Write Ahead Logging.

Before data pages are written to disk, PostgreSQL records every modification in WAL.

Process:

```text id="xv0s4u"
Transaction
     ↓
WAL Record
     ↓
Disk
     ↓
Data Page
```

This guarantees that committed transactions can be recovered after a crash.

---

## WAL Records

A WAL record contains information about database modifications.

Examples:

* INSERT
* UPDATE
* DELETE
* Page changes

The actual page update may occur later.

---

## Crash Recovery

If PostgreSQL crashes:

1. Restart server.
2. Read WAL records.
3. Replay committed changes.
4. Restore consistency.

This mechanism allows PostgreSQL to recover safely after unexpected failures.

---

## Checkpointing

Writing every page immediately would be inefficient.

Instead PostgreSQL periodically creates checkpoints.

A checkpoint:

* Flushes dirty pages.
* Establishes a recovery starting point.
* Limits WAL replay time.

Trade-off:

* Frequent checkpoints increase I/O.
* Infrequent checkpoints increase recovery time.

---

# 8. Query Planning and EXPLAIN ANALYZE

PostgreSQL uses a cost-based query optimizer.

Before executing a query, PostgreSQL estimates:

* Number of rows.
* Cost of operations.
* Available indexes.
* Join strategies.

The optimizer chooses the plan with the lowest estimated cost.

---

## Experiment

The following query was executed:

```sql id="qvob40"
EXPLAIN ANALYZE
SELECT * FROM students
WHERE marks > 88;
```

Output:

```text id="6o2xv5"
Seq Scan on students
Rows Returned: 3
Rows Removed by Filter: 2
Planning Time: 1.150 ms
Execution Time: 0.020 ms
```

---

## Observation

PostgreSQL selected a Sequential Scan instead of an index scan.

Reason:

The table contained only a few rows. Reading the entire table was cheaper than using an index.

This demonstrates how PostgreSQL's query planner makes decisions based on estimated execution costs.

---

# 9. Design Trade-Offs

## Advantages

* Excellent concurrency through MVCC.
* Strong durability guarantees.
* Sophisticated query optimization.
* Efficient page caching.
* Reliable crash recovery.

---

## Limitations

* VACUUM maintenance required.
* MVCC increases storage usage.
* Internal architecture is complex.
* Additional WAL writes create overhead.

---

# 10. Key Learnings

* The Buffer Manager reduces expensive disk access through Shared Buffers.
* B-Trees provide efficient search and indexing performance.
* MVCC allows readers and writers to operate concurrently.
* xmin and xmax determine tuple visibility.
* VACUUM is necessary because PostgreSQL stores multiple row versions.
* WAL guarantees durability and crash recovery.
* PostgreSQL's query planner uses cost estimation to choose execution plans.
* High performance in database systems is achieved through carefully balanced architectural trade-offs.

---

# References

1. PostgreSQL Official Documentation
2. PostgreSQL Storage Documentation
3. PostgreSQL WAL Documentation
4. PostgreSQL Source Code Documentation

---

## Author

**Tanmay Mittal**
**Roll Number:** 24BCS10491

Submitted as part of the Advanced DBMS System Design Discussion assignment.
