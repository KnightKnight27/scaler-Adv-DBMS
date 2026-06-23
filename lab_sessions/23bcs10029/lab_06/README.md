# Lab 6: Transaction Manager — MVCC + Two-Phase Locking

## What this lab covers

A transaction manager combining:
1. **MVCC** — each write creates a new row version; readers see a consistent snapshot without blocking writers
2. **Strict 2PL** — all locks held until commit/abort; no cascading aborts
3. **Deadlock detection** — waits-for graph cycle detection (DFS) to abort one transaction when a cycle forms

## Build & Run

```bash
g++ -std=c++17 -pthread -o txmgr txmgr.cpp && ./txmgr
```

## Expected output

```
=== Scenario 1: MVCC Snapshot Isolation ===
[TX 3] COMMITTED
[TX 4] COMMITTED
  [TX 2] READ balance = 1000        <- sees pre-update snapshot

=== Scenario 2: Concurrent Shared Locks ===
  [TX 4] READ balance = 2000
  [TX 5] READ balance = 2000
[TX 4] COMMITTED
[TX 5] COMMITTED

=== Scenario 3: Exclusive Lock + Waiting ===
  [TX 7] waiting for shared lock on balance...
[TX 6] COMMITTED
  [TX 7] READ balance = 3000
[TX 7] COMMITTED

=== Scenario 4: Deadlock Detection ===
  Deadlock detected, aborting tx 8
[TX 8] ABORTED
[TX 9] COMMITTED
```

## Architecture

```
TransactionManager
    |
    ├─► LockManager (Strict 2PL)
    │       GROWING phase:   acquire SHARED or EXCLUSIVE locks
    │       SHRINKING phase: release all at commit/abort
    │       Deadlock:        waits-for graph, DFS cycle check
    │
    └─► MVCC Heap (version chain per row key)
            INSERT -> push {value, xmin=xid, xmax=0}
            UPDATE -> stamp old xmax=xid, push new version
            DELETE -> stamp old xmax=xid
            READ   -> walk chain, return first visible version
                      (xmin committed < snapshot AND xmax=0 or not yet committed)
```

## MVCC visibility rule

A version `(xmin, xmax)` is visible to transaction T with `snapshot_xid` if:
- `xmin` is committed AND `xmin < snapshot_xid` (or T is reading its own write)
- `xmax = 0` OR `xmax` is not yet committed OR `xmax >= snapshot_xid`

## Why MVCC + 2PL together

| Property              | MVCC alone                   | 2PL alone              | MVCC + Strict 2PL       |
|-----------------------|------------------------------|------------------------|-------------------------|
| Read-write contention | None (readers use snapshots) | Readers block writers  | None                    |
| Write-write conflict  | Lost update possible         | Serializable           | Serializable            |
| Deadlock              | N/A                          | Possible               | Possible — needs detect |

PostgreSQL uses MVCC + SSI (Serializable Snapshot Isolation) — a refinement that detects dangerous read-write anti-dependencies without 2PL's throughput cost.
