# MySQL / InnoDB Storage Engine

> Advanced DBMS · System Design Discussion · Topic 3

---

## 1. Introduction

Every database storage engine must address three fundamental challenges simultaneously:

1. Efficiently organizing data on persistent storage.
2. Allowing many transactions to execute concurrently without interfering with one another.
3. Preserving consistency and durability in the presence of failures.

These objectives naturally compete. Immediate persistence improves reliability but reduces throughput. Long-running readers want a stable view of the data, while writers need to modify it. High concurrency improves responsiveness but complicates consistency guarantees.

The way a database resolves these competing concerns defines much of its architecture.

InnoDB, the default storage engine of MySQL since version 5.5, was designed specifically to overcome limitations of the older MyISAM engine. Although MyISAM performed well for simple read-heavy workloads, it relied on table-level locking and lacked transactional recovery mechanisms. A crash during modification could therefore leave data structures in an inconsistent state.

To solve these problems, InnoDB introduced:

* Full ACID transaction support
* Row-level locking
* Multi-Version Concurrency Control (MVCC)
* Crash recovery through logging

What makes InnoDB particularly interesting is the set of architectural choices underlying these capabilities.

Three design decisions influence nearly every aspect of the engine:

### Clustered Storage

Instead of storing table data separately from indexes, InnoDB organizes the table itself as a clustered B+ tree ordered by the primary key.

### Undo-Based MVCC

The latest version of a row is stored directly in the table. Older versions are reconstructed from undo records when required by long-running transactions.

### Dual Logging System

Recovery is split between:

* Redo logs for replaying committed modifications
* Undo logs for reversing incomplete transactions

These decisions differ significantly from PostgreSQL's approach. PostgreSQL stores rows in heap files, maintains multiple row versions directly inside the table, and relies on a single WAL-based recovery mechanism together with VACUUM.

Comparing the two systems reveals how different architectural choices lead to different performance characteristics and operational trade-offs.

---

## 2. Architectural Overview

At a high level, InnoDB can be viewed as two cooperating layers:

* An in-memory execution layer
* A persistent storage layer

The memory layer absorbs most read and write activity, while background processes gradually move modified data to durable storage.

### Core Components

#### Clustered Index

The clustered index forms the primary structure of every InnoDB table. Rows are physically stored inside the leaf pages of a B+ tree ordered by primary key.

#### Secondary Indexes

Additional indexes exist as independent B+ trees. Rather than pointing directly to physical row locations, they store the corresponding primary-key values.

#### Buffer Pool

The buffer pool is the primary in-memory cache. Database pages are loaded here before being read or modified.

#### Undo Logs

Undo logs preserve previous row states. They enable transaction rollback and provide historical row versions required for MVCC snapshots.

#### Redo Logs

Redo logs capture physical page modifications before they reach disk. They ensure durability and support crash recovery.

#### Lock Manager

The locking subsystem coordinates concurrent access through record locks, gap locks, and next-key locks.

---

## Read Processing

When executing a query, InnoDB first locates the appropriate index and traverses its B+ tree structure.

If required pages are not already cached, they are loaded into the buffer pool.

For transactions operating under MVCC, the engine may need to reconstruct an older row version using undo records before returning results.

The resulting workflow is:

```id="b4bz35"
Query
  │
Index Lookup
  │
Buffer Pool
  │
Row Visible?
  │
 ├─ Yes → Return Row
 │
 └─ No → Follow Undo Chain
            │
      Reconstruct Version
            │
         Return Row
```

---

## Write Processing

Updates follow a different path.

When a row is modified:

1. The target page is loaded into the buffer pool.
2. The page is updated in memory.
3. An undo record containing the previous row state is generated.
4. A redo record describing the modification is written to the log buffer.
5. The page becomes dirty and awaits background flushing.

The actual table page does not need to reach disk immediately.

---

## Commit Processing

Transaction durability is determined by the redo log rather than by immediate data-page writes.

At commit time:

1. Pending redo records are flushed to disk.
2. The transaction is acknowledged as committed.
3. Dirty data pages remain in memory until a later checkpoint or background flush.

This approach allows transactions to commit using efficient sequential log writes while avoiding expensive random writes to data files.

The principle can be summarized as:

**Log first, write data later.**

---

## Clustered Storage Model

One of the most distinctive aspects of InnoDB is that table rows are physically organized within the primary-key index.

The leaf nodes of the clustered B+ tree contain complete row records rather than pointers to separate heap storage.

This design provides several advantages:

* Fast primary-key lookups
* Efficient primary-key range scans
* Compact storage organization

However, it also means that insertion patterns strongly influence performance.

Sequential primary keys tend to append near the rightmost edge of the tree, resulting in dense pages and minimal fragmentation.

Random keys distribute inserts throughout the tree, causing frequent page splits, increased fragmentation, and reduced cache efficiency.

Consequently, primary-key design has a much larger impact on performance in InnoDB than in engines such as PostgreSQL.

---

## High-Level Data Flow

A simplified end-to-end write path is:

```id="7y3y3x"
Client Request
      │
 Buffer Pool Update
      │
 ┌────┴────┐
 │         │
Undo Log  Redo Log
 │         │
 │     Commit Flush
 │
 Dirty Page
      │
Background Flush
      │
 Tablespace Files
```

Similarly, reads travel from indexes through the buffer pool, optionally consulting undo records to reconstruct older versions when snapshot visibility requires it.

---

## Key Architectural Themes

Several recurring principles define InnoDB's design:

* Data is organized around clustered primary-key storage.
* The latest version of a row remains directly accessible.
* Historical versions are reconstructed through undo chains.
* Durability depends on redo logging rather than immediate page writes.
* Most operations occur through the buffer pool before reaching disk.
* Concurrency combines MVCC snapshots with fine-grained locking mechanisms.

Together these choices make InnoDB particularly well suited for transactional workloads that rely heavily on primary-key access patterns while maintaining strong ACID guarantees.


