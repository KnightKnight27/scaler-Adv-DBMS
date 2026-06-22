# Lab Session 6: Transaction Manager — MVCC + Two-Phase Locking in C++

**Name:** Snehangshu Roy
**Roll No:** 24BCS10155

## Objective
Build a transaction manager that combines:
1. **MVCC** — every write creates a new row version; readers see a consistent
   snapshot without blocking writers.
2. **Strict Two-Phase Locking (2PL)** — locks acquired in the growing phase; all
   released at commit/abort (the shrinking phase is instantaneous).
3. **Deadlock detection** — waits-for graph cycle detection aborts a transaction.

This mirrors the core of PostgreSQL's concurrency architecture.

## Files
- `txmgr.cpp` — transaction table, MVCC version chains, Strict-2PL lock manager
  with waits-for deadlock detection, and four demo scenarios.
- `makefile` — build / run (links `-pthread`).

## Build & Run
```bash
make
make run
# or
g++ -std=c++17 -pthread -o txmgr txmgr.cpp && ./txmgr
```
> Adds `#include <functional>` (for `std::function` in the cycle detector) and
> `#include <chrono>` (for the demo sleeps) so it compiles cleanly.

## MVCC visibility rule
A version created by `xmin` and invalidated by `xmax` is visible to transaction T if:
- `xmin` is committed and `xmin < T.snapshot_xid` (or T created it), **and**
- `xmax == 0`, or `xmax` is uncommitted/aborted, or `xmax >= T.snapshot_xid`.

## Two-Phase Locking
```
GROWING phase:   may acquire new locks, may NOT release any
SHRINKING phase: may release locks, may NOT acquire new ones
Strict 2PL:      shrinking only happens at commit/abort -> avoids cascading aborts
```

## Demo scenarios
1. **MVCC snapshot isolation** — a reader that started before a concurrent committed
   update still sees the old value.
2. **Concurrent shared locks** — two readers both get a shared lock.
3. **Exclusive lock + waiting** — a reader blocks until the writer commits, then sees the new value.
4. **Deadlock detection** — two transactions form a waits-for cycle; one is aborted.

## MVCC vs 2PL — why both?

| Property               | MVCC alone                     | 2PL alone                | MVCC + Strict 2PL                  |
|------------------------|--------------------------------|--------------------------|------------------------------------|
| Read-write contention  | None (snapshots)               | Readers block writers    | None — readers use snapshots       |
| Write-write contention | Last writer wins               | Serializable (X locks)   | Serializable — X lock on write     |
| Serializability        | Snapshot Isolation only        | Serializable             | Serializable                       |
| Deadlock               | N/A                            | Possible                 | Possible — needs detection         |
| Old version cleanup    | Needs vacuum/GC                | N/A                      | Needs vacuum/GC                    |

PostgreSQL uses MVCC + Serializable Snapshot Isolation (SSI), which detects
dangerous read-write anti-dependencies without 2PL's throughput cost.

## Key Takeaways
- MVCC creates a new row version per write; readers apply the visibility rule
  against their snapshot XID.
- Strict 2PL holds all locks until commit/abort — the shrinking phase is instantaneous.
- The combination removes read-write contention (MVCC) while keeping writes
  serializable (2PL).
- Deadlock detection via waits-for DFS is O(V+E) per request.
- ABORT must undo MVCC writes: own inserts are hidden (`xmax=xid`), own deletes
  reversed (`xmax=0`).
