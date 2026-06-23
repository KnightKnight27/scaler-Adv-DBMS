# Extension Track B: Concurrency (MVCC)

For the Advanced DBMS capstone project, our team selected **Track B**, focusing on advanced concurrency control by implementing Multi-Version Concurrency Control (MVCC).

## Why MVCC?
Standard Strict Two-Phase Locking (2PL) enforces serializability but significantly degrades performance under high contention because readers block writers, and writers block readers.
MVCC solves this by maintaining multiple versions of a data row. The fundamental advantage is:
**"Readers never block writers, and writers never block readers."**

## Implementation Details

### Row Versioning
Instead of overwriting rows in place, MiniDB inserts a new version of the row. Each `RowVersion` object tracks:
- `xmin`: The ID of the transaction that created the version.
- `xmax`: The ID of the transaction that deleted or superseded the version (defaults to 0 for active rows).

### Snapshot Isolation
When a transaction begins, it receives a `snapshot_ts`. This timestamp dictates its view of the database.
A transaction only sees row versions that were committed *before* its snapshot was taken.

### Visibility Rules
During query execution, the `MVCCManager` evaluates visibility using these simplified PostgreSQL-style rules:
1. If `xmin` equals the current transaction ID, the version is visible (the transaction can see its own changes).
2. The version is visible if `xmin` is committed and `xmin <= snapshot_ts` AND:
   - `xmax` is 0 (the row was never deleted).
   - OR `xmax` is not yet committed.
   - OR `xmax > snapshot_ts` (the row was deleted *after* our snapshot was taken).

### Write-Write Conflict Detection
While MVCC handles reads elegantly, concurrent writes to the same row still require synchronization. MiniDB integrates MVCC with our existing Lock Manager:
- Writers still acquire an `EXCLUSIVE` lock before updating or deleting a row.
- If two transactions try to modify the same row, the second transaction blocks until the first completes.
- Deadlocks are handled gracefully by the Lock Manager's wait-for graph cycle detection.

### Garbage Collection
To prevent the database from growing infinitely with obsolete versions, the `MVCCManager` implements a `garbage_collect()` routine.
Versions are safely removed if:
- They have been superseded by a newer committed version.
- AND their `xmax` timestamp is older than the `snapshot_ts` of the oldest currently active transaction.

## Benchmarks
Our benchmark suite (`benchmarks/benchmark_runner.py`) clearly demonstrates the benefits of our MVCC implementation. In tests with 5 concurrent readers and 2 concurrent writers, the system maintains high read and write throughput simultaneously, without deadlock or lock timeout errors that commonly plague pure 2PL implementations under similar workloads.
