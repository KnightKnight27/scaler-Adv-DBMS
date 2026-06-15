# Lab Session 6: Transaction Manager — MVCC + Two-Phase Locking in C++

## Objective
Build a transaction manager that combines:
1. **MVCC** — every write creates a new row version; readers see a consistent snapshot without blocking writers.
2. **Two-Phase Locking (2PL)** — lock acquisition is bounded to the "growing" phase; no new locks after the first release ("shrinking" phase). Strict 2PL holds all locks until commit/abort.
3. **Deadlock detection** — waits-for graph cycle detection to abort one transaction when a cycle forms.

This mirrors the core of PostgreSQL's concurrency architecture (MVCC snapshot isolation + predicate/row-level locks).

---

## Concepts

### MVCC version visibility rule
A version created by transaction `xmin` and invalidated by `xmax` is visible to transaction `T` if:
- `xmin` is committed and `xmin` <= `T.snapshot_xid`
- `xmax` is 0 (not yet deleted) OR `xmax` > `T.snapshot_xid` OR `xmax` is aborted

### Two-Phase Locking phases
```
GROWING phase:  transaction may acquire new locks, may NOT release any
SHRINKING phase: transaction may release locks, may NOT acquire new ones
                 (Strict 2PL: shrinking phase only happens at commit/abort)
```

### Deadlock
Transaction A waits for a lock held by B; B waits for a lock held by A → cycle → abort the younger transaction.

---

## Implementation

The source code is located in [txmgr.cpp](file:///C:/Users/singh/Downloads/scaler-Adv-DBMS-main/scaler-adv-dmbs/lab6/txmgr.cpp).

### Core Features Handled:
- **Multithreading Safe**: Mutex guards protect access to version chains, transaction tables, and locks.
- **Waits-for Graph**: Dynamic construction of locks waits-for graphs.
- **Cycle Detection**: Depth-First Search (DFS) implementation to detect deadlock cycles immediately.
- **Abort Clean-up**: Fully reverts transaction changes on aborted transactions (xmin version hiding and xmax deletion recovery).

Compile and run:
```bash
g++ -std=c++17 -pthread -o txmgr txmgr.cpp
./txmgr
```

---

## Architecture Diagram

```
Application
    │
    ▼
TransactionManager.begin() / read() / update() / commit() / abort()
    │
    ├─► LockManager (Strict 2PL)
    │       growing phase: acquire SHARED or EXCLUSIVE lock
    │       shrinking phase: release all locks on commit/abort
    │       deadlock detection: waits-for graph, DFS cycle check
    │
    └─► MVCC Heap (version chain per row)
            INSERT  → push new version {value, xmin=xid, xmax=0}
            UPDATE  → stamp old xmax=xid, push new version
            DELETE  → stamp old xmax=xid
            READ    → walk chain, return first version where
                      xmin committed < snapshot AND
                      (xmax=0 OR xmax not yet committed)
```

---

## MVCC vs 2PL — Why both?

| Property               | MVCC alone                                  | 2PL alone                              | MVCC + Strict 2PL                        |
|------------------------|---------------------------------------------|----------------------------------------|------------------------------------------|
| Read-write contention  | None — readers never block writers          | Readers block writers (shared lock)    | None — readers use snapshots             |
| Write-write contention | Last writer wins (lost update problem)      | Serializable via exclusive locks       | Serializable — exclusive lock on write   |
| Serializability        | Snapshot Isolation only (not always serial) | Serializable                           | Serializable                             |
| Deadlock               | N/A                                         | Possible                               | Possible — needs detection               |
| Old version cleanup    | Needs vacuum/GC                             | N/A                                    | Needs vacuum/GC                          |

PostgreSQL uses MVCC + Serializable Snapshot Isolation (SSI) — a refinement that detects dangerous read-write anti-dependencies without the throughput cost of 2PL.

---

## Two-Phase Locking — Phase Boundary

```
GROWING PHASE               │  SHRINKING PHASE
acquire S lock on A    ✓    │
acquire X lock on B    ✓    │
acquire S lock on C    ✓    │
                            │  release lock on A    ✓
                            │  release lock on B    ✓
acquire lock on D      ✗  ← violation: 2PL broken, schedule no longer serializable
```

**Strict 2PL** (used here) simplifies this: the shrinking phase only occurs at commit/abort, so the boundary is always at the end of the transaction. This avoids cascading aborts.

---

## Key Takeaways
- MVCC creates a new row version per write; readers walk the version chain and apply the visibility rule against their snapshot XID.
- Strict 2PL holds all locks until commit/abort — the shrinking phase is instantaneous at transaction end.
- The combination eliminates read-write contention (MVCC) while preserving serializability for writes (2PL).
- Deadlock detection via waits-for graph DFS is O(V+E) per lock request — PostgreSQL runs a similar check periodically rather than per-request for performance.
- ABORT must undo MVCC writes: own inserts are hidden (xmax=xid), own deletes are reversed (xmax=0).
