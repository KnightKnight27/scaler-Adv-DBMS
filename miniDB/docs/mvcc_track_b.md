# Track B: MVCC

Track B replaces the blocking read path of 2PL with Multi-Version Concurrency
Control. The implementation is intentionally compact so it can be explained in
the viva.

## Design

- Each transaction gets a monotonically increasing start timestamp.
- Each row ID has a version chain.
- Committed versions have a `[begin_ts, end_ts)` visibility interval.
- Uncommitted versions are visible only to their writer.
- Updates append a new version and close the old version at commit time.
- Deletes append a tombstone version and close the old version at commit time.

## Snapshot Visibility

A transaction reads the newest committed version whose interval contains its
start timestamp. This means:

- Readers do not block writers.
- Writers do not block old readers.
- Older readers keep seeing their original snapshot after later commits.
- New readers see the latest committed version.

## Write Conflicts

Only one active writer may own an uncommitted version for a row. A second writer
trying to update/delete the same RID receives a write-write conflict and must
retry or abort.

## Demonstration Scenarios

- Start reader before writer commits: reader does not see the later commit.
- Start reader after writer commits: reader sees the committed row.
- Update while an older reader is active: older reader sees old value, new reader
  sees updated value.
- Delete creates a tombstone visible to later snapshots.
- Abort removes uncommitted versions.

## Comparison With 2PL

The M4 strict 2PL path blocks an exclusive writer behind shared readers. MVCC
allows readers to keep scanning committed snapshots while writers append new
versions, reducing read blocking under contention.
