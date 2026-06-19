# Lab 8 - Transaction Manager: MVCC + Two-Phase Locking

## Student Details
- Name: Rudray Mehra
- Roll Number: 24BCS10425

## Objective
Build a transaction manager that combines three mechanisms used by real database
engines (this mirrors PostgreSQL's concurrency core):
1. **MVCC** — every write creates a new row version; readers see a consistent
   snapshot without blocking writers.
2. **Strict Two-Phase Locking (2PL)** — locks are acquired in the growing phase
   and all released together at commit/abort (the shrinking phase).
3. **Deadlock detection** — a waits-for graph is scanned for cycles; when a cycle
   forms, the requesting transaction is aborted.

## Files
- `txn_manager.cpp`: Complete C++17 implementation — MVCC version chains, a
  strict 2PL lock manager with shared/exclusive locks, waits-for cycle detection,
  and a `TransactionManager` API (`begin / read / insert / update / remove /
  commit / abort`). Includes four demo scenarios.

## Compile and Run

```bash
g++ -std=c++17 -pthread LAB-8/txn_manager.cpp -o LAB-8/txn_manager
./LAB-8/txn_manager
```

## Concepts

### MVCC version visibility rule
A version created by `xmin` and invalidated by `xmax` is visible to transaction
`T` when:
- `xmin` is the reader itself, or `xmin` is committed and `xmin < T.snapshot_xid`, and
- `xmax` is 0 (still live), or `xmax` is not yet committed relative to the snapshot.

### Two-Phase Locking phases
```
GROWING phase:   may acquire new locks, may NOT release any
SHRINKING phase: may release locks, may NOT acquire new ones
```
**Strict 2PL** (used here) holds all locks until commit/abort, so the shrinking
phase happens only at transaction end. This avoids cascading aborts.

### Deadlock
If transaction A waits on a lock held by B while B waits on a lock held by A, the
waits-for graph contains a cycle. A DFS detects it and one transaction aborts.

## Demo Scenarios
1. **MVCC snapshot isolation** — a reader keeps seeing the pre-update value even
   after a concurrent writer commits a new version.
2. **Concurrent shared locks** — two readers both hold a shared lock at once.
3. **Exclusive lock + waiting** — a reader on another thread blocks until the
   exclusive writer commits, then reads the new value.
4. **Deadlock detection** — two transactions form a lock cycle; one is aborted,
   the other commits.

## MVCC vs 2PL — Why both?
| Property               | MVCC alone               | 2PL alone            | MVCC + Strict 2PL          |
|------------------------|--------------------------|----------------------|----------------------------|
| Read–write contention  | None (snapshots)         | Readers block writers| None (snapshots)           |
| Write–write contention | Last writer wins         | Serializable         | Serializable (X lock)      |
| Serializability        | Snapshot Isolation only  | Serializable         | Serializable               |
| Deadlock               | N/A                      | Possible             | Possible — needs detection |

## Notes
ABORT must undo a transaction's MVCC writes: its own inserts are hidden
(`xmax = xid`) and its own deletes are reversed (`xmax = 0`). Deadlock detection
runs per lock request here for clarity; production engines (e.g. PostgreSQL) run
the same waits-for check periodically instead, to keep the hot path cheap.
