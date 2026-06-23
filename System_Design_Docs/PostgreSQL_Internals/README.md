# PostgreSQL Internal Architecture

**Advanced Database Management Systems (ADBMS)**
**System Design Discussion – Topic 2**

**Author:** Praneeth Budati
**Roll No:** 24BCS10081

---

## Overview

PostgreSQL is a relational database management system designed to provide reliable transaction processing, concurrency, and durability. It achieves these goals through four major subsystems:

* Buffer Manager
* B-Tree Indexes
* Multi-Version Concurrency Control (MVCC)
* Write-Ahead Logging (WAL)

These components work together to ensure that PostgreSQL can efficiently handle large numbers of concurrent transactions while maintaining ACID properties.

---

## Why These Components Matter

Every database system must solve four fundamental problems:

| Challenge                   | PostgreSQL Component |
| --------------------------- | -------------------- |
| Fast data access            | Buffer Manager       |
| Efficient searching         | B-Tree Index         |
| Concurrent reads and writes | MVCC                 |
| Crash recovery              | WAL                  |

A single INSERT operation interacts with all four systems:

1. A page is loaded into memory by the Buffer Manager.
2. Data is inserted into the table and indexes.
3. Transaction metadata is attached through MVCC.
4. WAL records are generated before commit.

---

# 1. Buffer Manager

The Buffer Manager is responsible for managing `shared_buffers`, PostgreSQL's main memory cache.

Instead of reading every page directly from disk, PostgreSQL first checks whether the page is already available in memory.

### Page Access Process

1. Search the buffer cache.
2. If found, return the cached page.
3. If not found, load the page from disk.
4. Store it in the buffer pool for future access.

### Buffer Replacement

PostgreSQL uses a Clock-Sweep algorithm instead of a traditional LRU cache.

Benefits:

* Lower lock contention
* Better scalability
* Efficient operation under high concurrency

### Advantages

* Reduces disk I/O
* Improves query performance
* Allows multiple backends to share cached pages

---

# 2. B-Tree Indexes

PostgreSQL's default indexing structure is the B+-Tree implementation known as `nbtree`.

### Structure

* Internal pages contain separator keys.
* Leaf pages contain index entries.
* Leaf nodes are linked together for efficient range scans.

### Search Complexity

Because B-Trees remain balanced, lookup operations require only a small number of page accesses.

For example:

| Rows             | Typical Tree Height |
| ---------------- | ------------------- |
| 500,000          | 3 levels            |
| Tens of millions | 4 levels            |

This logarithmic growth is the main reason B-Trees are widely used.

### Insert Operations

When a page becomes full:

1. The page splits.
2. Entries are redistributed.
3. Parent pages are updated.

This keeps the tree balanced but increases write overhead.

---

# 3. Multi-Version Concurrency Control (MVCC)

MVCC allows readers and writers to work simultaneously without blocking each other.

Instead of modifying rows directly, PostgreSQL creates a new row version whenever an UPDATE occurs.

Each tuple stores:

| Field | Purpose                                      |
| ----- | -------------------------------------------- |
| xmin  | Transaction that created the row             |
| xmax  | Transaction that deleted or replaced the row |

### Example

Before Update:

| xmin | xmax |
| ---- | ---- |
| 797  | 0    |

After Update:

| xmin | xmax |
| ---- | ---- |
| 842  | 0    |

The old row version remains until it is no longer needed.

### Benefits

* Non-blocking reads
* High concurrency
* Consistent snapshots

### Drawback

Old row versions accumulate over time and require cleanup.

---

# VACUUM

Since PostgreSQL keeps old row versions, unused tuples must eventually be removed.

VACUUM performs this cleanup by:

* Reclaiming storage space
* Updating visibility information
* Preventing transaction ID wraparound

Without VACUUM, tables would continuously grow in size.

---

# 4. Write-Ahead Logging (WAL)

WAL is PostgreSQL's durability mechanism.

### Core Principle

Changes must be written to the WAL before modified pages are written to disk.

This is known as the **Write-Ahead Logging Rule**.

### Commit Process

1. Generate WAL records.
2. Flush WAL to disk.
3. Confirm transaction commit.
4. Write data pages later.

This approach makes commits fast because only sequential log writes are required.

---

# Crash Recovery

After a crash:

1. PostgreSQL loads the latest checkpoint.
2. WAL records are replayed.
3. Missing changes are reconstructed.
4. Database consistency is restored.

This mechanism ensures durability even if power is lost unexpectedly.

---

# Design Trade-Offs

| Component      | Benefit           | Cost                      |
| -------------- | ----------------- | ------------------------- |
| Buffer Manager | Faster reads      | Cache pollution possible  |
| B-Tree         | Efficient lookups | Page splits               |
| MVCC           | High concurrency  | Dead tuples               |
| WAL            | Durability        | Additional write overhead |

---

# Key Observations

### Buffer Manager

* Frequently accessed pages remain in memory.
* Reduces disk access significantly.

### B-Tree

* Tree height grows very slowly.
* Even large indexes require only a few page reads.

### MVCC

* Readers and writers rarely block each other.
* Old row versions create storage overhead.

### WAL

* Enables fast commits.
* Makes crash recovery possible.

---

# Conclusion

PostgreSQL's architecture is built around the interaction of the Buffer Manager, B-Tree indexes, MVCC, and WAL. Each subsystem solves a different problem, but together they provide a database that is fast, highly concurrent, and durable.

The most important insight is that PostgreSQL achieves performance by separating transaction durability from data-page writes. WAL records are written immediately, while actual data pages are written later by background processes. This design allows PostgreSQL to maintain strong ACID guarantees without sacrificing throughput.

---

# References

1. PostgreSQL 16 Documentation
2. PostgreSQL Source Code (`bufmgr.c`, `nbtree`)
3. PostgreSQL MVCC Documentation
4. PostgreSQL WAL Documentation
5. The Internals of PostgreSQL – Hironobu Suzuki
6. Lehman & Yao, Efficient Locking for Concurrent Operations on B-Trees
