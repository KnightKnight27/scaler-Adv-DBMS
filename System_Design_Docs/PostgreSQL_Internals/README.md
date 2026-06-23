## Problem Background

PostgreSQL is designed to handle complex workloads where multiple users read and write data at the same time. While using it, I realized that the real complexity is not in writing SQL queries, but in how the system internally manages memory, concurrency, and crash recovery.

This section focuses on understanding how PostgreSQL actually works under the hood—how it stores data, how it ensures consistency, and how it executes queries efficiently even under heavy load.

## High-Level Architecture

PostgreSQL follows a process-based architecture where each client connection is handled by a separate backend process.

Internally, it consists of:

- Parser: Converts SQL into parse tree
- Planner/Optimizer: Chooses best execution plan
- Executor: Runs the query plan
- Storage Manager: Handles disk I/O
- Buffer Manager: Manages in-memory pages
- WAL System: Ensures durability

A simplified flow:

Client → Parser → Planner → Executor → Storage → Disk

One important thing I noticed is that PostgreSQL does not directly read from disk every time. Instead, most operations go through shared buffers, which act as a cache layer.

## Buffer Manager (Shared Buffers)

The buffer manager is one of the most critical components of PostgreSQL.

Instead of reading data directly from disk every time, PostgreSQL stores frequently accessed pages in a shared memory area called shared buffers.

When a query requests a page:
1. PostgreSQL first checks shared buffers
2. If found → it is a buffer hit (fast)
3. If not found → page is read from disk into buffer

This reduces disk I/O significantly.

The buffer manager also decides:
- Which pages to evict (LRU-like strategy)
- When dirty pages should be written back to disk

This design is important because disk access is much slower than memory access, and buffer management directly impacts performance.

## Storage & Page Layout

PostgreSQL stores data in fixed-size pages (commonly 8KB). Each table is divided into these pages.

Each page contains:
- Tuple data (rows)
- Page header
- Line pointers (row locations)

One key insight is that PostgreSQL does not overwrite rows directly. Instead, updates often create new versions of rows, which is tied to MVCC.

This makes storage slightly more complex but enables high concurrency.

## MVCC (Multi-Version Concurrency Control)

PostgreSQL uses MVCC to handle concurrency without locking readers.

Each row contains:
- xmin → transaction that created it
- xmax → transaction that deleted/updated it

When a query runs, PostgreSQL checks visibility rules using transaction snapshots.

This means:
- Readers do not block writers
- Writers do not block readers (in most cases)

However, old row versions accumulate over time, which is why VACUUM is required.

## B-Tree Index Implementation

PostgreSQL uses B-Tree indexes as the default indexing method.

Structure:
- Root node
- Intermediate nodes
- Leaf nodes (actual pointers to table rows)

When searching:
- PostgreSQL traverses from root → leaf
- Reduces complexity from O(n) → O(log n)

Insert operations may cause page splits when a node becomes full.

This is important because indexes directly affect query performance, especially in large datasets.

## WAL (Write Ahead Logging)

Write-Ahead Logging ensures durability in PostgreSQL.

Before any data is written to disk:
1. Changes are first written to WAL log
2. WAL is flushed to disk
3. Then actual data pages are updated

If a crash happens:
- PostgreSQL replays WAL logs
- Restores database to consistent state

This guarantees no committed transaction is lost.

## Query Execution & Planner

When a query is executed, PostgreSQL does not immediately run it. It first goes through the planner.

The planner:
- Estimates cost of different plans
- Uses statistics from pg_statistic
- Chooses best execution strategy

Example strategies:
- Sequential Scan
- Index Scan
- Hash Join
- Merge Join

The decision depends heavily on table size and data distribution.

## VACUUM & Cleanup

Because PostgreSQL uses MVCC, old row versions are not immediately deleted.

Over time, this creates "dead tuples". VACUUM is used to:
- Remove dead tuples
- Reclaim storage space
- Update statistics for query planner

Without VACUUM, performance degrades because tables grow with unused data.

## Experiments / Observations (EXPLAIN ANALYZE)

I ran a simple query:

```sql
EXPLAIN ANALYZE
SELECT * FROM employees WHERE name = 'Alice';
```

### Observation 1 (Without Index)
PostgreSQL performed a Sequential Scan, meaning it checked every row.

### Observation 2 (After Index)
After creating an index:

```sql
CREATE INDEX idx_name ON employees(name);
```

The query switched to Index Scan.

### Insight
Indexes drastically reduce query time because PostgreSQL avoids scanning full tables.

## Key Learnings

This section helped me understand that PostgreSQL is not just a database that stores data, but a complex system with multiple internal components working together.

The most important insights for me were:
- Buffer manager reduces disk I/O
- MVCC enables high concurrency
- WAL ensures crash recovery
- Planner decides query performance
- VACUUM is necessary maintenance in MVCC systems