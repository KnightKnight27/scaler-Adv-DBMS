# Lab 6 — Transaction Manager: MVCC + Two-Phase Locking

**Rohan Ranjan — 24BCS10428**

## Objective
Build a transaction manager that combines:
1. **MVCC** — every write creates a new row version; readers see a consistent snapshot
   without blocking writers.
2. **Strict 2PL** — locks acquired in the growing phase; all released at commit/abort
   (the shrinking phase is instantaneous, which avoids cascading aborts).
3. **Deadlock detection** — a waits-for graph with DFS cycle detection aborts a
   transaction when a cycle forms.

This mirrors the core of PostgreSQL's concurrency architecture (MVCC snapshot isolation +
row-level locks).

## Build & run
```bash
g++ -std=c++17 -pthread -o txmgr txmgr.cpp
./txmgr
```
(`<functional>` and `<chrono>` are included for the deadlock-detection DFS and the demo's
`sleep_for`, which the lab text used without including.)

## MVCC visibility rule
A version created by `xmin` and invalidated by `xmax` is visible to transaction `T` when:
- `xmin` is committed and `xmin < T.snapshot_xid` (or `xmin` is `T` itself), **and**
- `xmax == 0`, or `xmax >= T.snapshot_xid`, or `xmax` is aborted (i.e. not yet visibly deleted).

Operations on the version chain:
- **INSERT** → push `{value, xmin=xid, xmax=0}`.
- **UPDATE** → stamp the old visible version `xmax=xid`, push a new version.
- **DELETE** → stamp the visible version `xmax=xid`.
- **ABORT** → hide own inserts (`xmax=xid`) and reverse own deletes (`xmax=0`).

## Two-phase locking
```
GROWING:   may acquire S/X locks, may NOT release any
SHRINKING: may release locks, may NOT acquire new ones
```
Strict 2PL puts the entire shrinking phase at commit/abort. Shared locks are mutually
compatible; an exclusive lock conflicts with everything else.

## Demo scenarios (`main`)
1. **MVCC snapshot isolation** — `t2` still reads `1000` after `t3` commits `2000`,
   because `t3` committed *after* `t2`'s snapshot was taken.
2. **Concurrent shared locks** — two readers both acquire S locks with no blocking.
3. **Exclusive lock + waiting** — a reader on another thread blocks until the X-lock
   holder commits, then reads the new value.
4. **Deadlock detection** — `t8` holds A and wants B; `t9` holds B and wants A; the cycle
   is detected and one transaction is aborted.

## MVCC vs 2PL — why both?
| Property               | MVCC alone                         | 2PL alone                  | MVCC + Strict 2PL          |
|------------------------|------------------------------------|----------------------------|----------------------------|
| Read-write contention  | None (snapshots)                   | Readers block writers      | None (snapshots)           |
| Write-write contention | Last writer wins (lost update)     | Serializable via X locks   | Serializable (X on write)  |
| Serializability        | Snapshot Isolation only            | Serializable               | Serializable               |
| Deadlock               | N/A                                | Possible                   | Possible — needs detection |

PostgreSQL uses MVCC + Serializable Snapshot Isolation (SSI), which detects dangerous
read-write anti-dependencies without 2PL's throughput cost.

## Key takeaways
- MVCC creates a new version per write; readers apply the visibility rule against their snapshot XID.
- Strict 2PL holds all locks until commit/abort — the shrinking phase is instantaneous.
- The combination removes read-write contention (MVCC) while keeping writes serializable (2PL).
- Deadlock detection via waits-for DFS is O(V+E) per request; PostgreSQL runs a similar check periodically.
- ABORT must undo MVCC writes: own inserts hidden (`xmax=xid`), own deletes reversed (`xmax=0`).
