# Topic 2: PostgreSQL Internal Architecture

## 1. Problem Background
PostgreSQL aims to provide a robust, ACID-compliant, highly concurrent relational database system. To achieve this without locking issues that plague traditional databases, it must manage memory, disk I/O, and transactions very carefully using specialized internal sub-systems.

## 2. Architecture Overview
The architecture involves several critical subsystems working in tandem:
- **Buffer Manager**: Manages reading and writing data pages to and from disk.
- **Storage Manager**: Handles the physical layout of files.
- **Transaction Manager**: Manages MVCC and locks.
- **WAL (Write-Ahead Log)**: Ensures durability.

## 3. Internal Design
### Buffer Manager & Shared Buffers
PostgreSQL uses a shared memory segment for caching data pages (default 8KB). Pages are swapped using a clock-sweep replacement algorithm. If a page is modified, it's marked as "dirty" and eventually flushed by the background writer.

### MVCC (Multi-Version Concurrency Control)
Instead of overwriting rows, PostgreSQL adds new versions of a row to the table (heap). Each row (tuple) has `xmin` (transaction ID that created it) and `xmax` (transaction ID that deleted/updated it).
Visibility rules determine which transaction sees which version based on transaction snapshots.

### WAL (Write Ahead Logging)
To guarantee durability (the 'D' in ACID) without forcing synchronous data file writes, PostgreSQL writes all changes to a sequential WAL. In case of a crash, the system replays the WAL to restore committed transactions.

## 4. Design Trade-Offs
- **Append-only MVCC**: The advantage is that rollbacks are instant (just ignore the new tuples), and reads/writes don't block each other. The major limitation is "bloat." Dead tuples accumulate and require `VACUUM` to reclaim space.
- **Buffer Pool**: Relying partly on the OS page cache (double buffering) can cause memory overhead but delegates some complex caching decisions to the highly-optimized OS kernel.

## 5. Experiments / Observations
When running `EXPLAIN ANALYZE` on a complex join:
- The query planner consults `pg_statistic` to estimate row counts.
- It chooses between Nested Loop, Hash Join, or Merge Join based on these estimates and available indexes (B-Tree).
- If statistics are stale, the planner might choose a suboptimal plan, highlighting the importance of `ANALYZE`.

## 6. Key Learnings
PostgreSQL's design heavily favors correctness and concurrency. The use of tuple-versioning for MVCC simplifies locking but introduces the necessity for aggressive background maintenance (autovacuum). Understanding the interplay between shared buffers, WAL, and MVCC is crucial for tuning PostgreSQL performance.
