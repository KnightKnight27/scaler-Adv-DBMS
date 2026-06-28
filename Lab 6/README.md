# Lab 6 — Transaction Manager: MVCC + Two-Phase Locking

## Files
- `txmgr.cpp` — full implementation

## Build & Run
```bash
g++ -std=c++17 -pthread -o txmgr txmgr.cpp && ./txmgr
```

## Concepts

### MVCC version visibility
A version created by `xmin` and invalidated by `xmax` is visible to
transaction `T` if:
- `xmin` is committed AND `xmin < T.snapshot_xid`
- `xmax == 0` OR `xmax > T.snapshot_xid` OR `xmax` aborted

### Two-Phase Locking phases
```
GROWING phase:    acquire locks freely, hold ALL of them
SHRINKING phase:  release locks only — NO new acquires
STRICT 2PL:       shrinking only at commit/abort (no cascading aborts)
```

### Deadlock
A waits for B's lock, B waits for A's → cycle in waits-for graph.
Abort the **younger** transaction (cheaper to roll back).

## Architecture
```
Application
   │
   ▼
TransactionManager.begin() / read() / update() / commit() / abort()
   │
   ├─► LockManager (Strict 2PL)
   │       growing phase: acquire SHARED or EXCLUSIVE
   │       shrinking phase: release all on commit/abort
   │       deadlock detection: waits-for graph, DFS cycle check
   │
   └─► MVCC Heap (version chain per row)
           INSERT  → push {value, xmin=xid, xmax=0}
           UPDATE  → stamp old xmax=xid, push new version
           DELETE  → stamp old xmax=xid
           READ    → walk chain, return first version where:
                     xmin committed < snapshot AND
                     (xmax=0 OR xmax not yet committed at snapshot)
```

## MVCC vs 2PL — why both?

| Property            | MVCC alone                | 2PL alone           | MVCC + Strict 2PL              |
|---------------------|---------------------------|---------------------|--------------------------------|
| Read-write contention | None                   | Readers block writ. | None — readers use snapshots   |
| Write-write contention | Last writer wins       | Serializable        | Serializable (X locks on write)|
| Serializability     | Snapshot Isolation only   | Serializable        | Serializable                   |
| Deadlock            | N/A                       | Possible            | Possible — needs detection     |
| Old version cleanup | Vacuum/GC                 | N/A                 | Vacuum/GC                      |

PostgreSQL uses **MVCC + Serializable Snapshot Isolation (SSI)** — a
refinement that detects dangerous read-write anti-dependencies without
the throughput cost of 2PL.

## Key takeaways
- MVCC: each write creates a new row version; readers walk the version chain
  and check visibility against their snapshot XID.
- Strict 2PL: hold all locks until commit/abort — shrinking is instantaneous
  at transaction end.
- The combination eliminates read-write contention (MVCC) while preserving
  serializability for writes (2PL).
- Deadlock detection via waits-for graph DFS is O(V+E) per lock request —
  PostgreSQL runs this periodically instead of per-request for performance.
- ABORT must undo MVCC writes: own inserts are hidden (xmax=xid), own
  deletes are reversed (xmax=0).