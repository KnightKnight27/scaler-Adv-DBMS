# Lab 8 — Transaction Manager: MVCC + Two-Phase Locking in C++

A transaction manager that combines three classic concurrency-control
mechanisms, mirroring the core of PostgreSQL's architecture (MVCC snapshot
isolation layered over row-level locks):

1. **MVCC** — every write creates a new row version, so readers see a
   consistent snapshot without blocking writers.
2. **Strict 2PL** — locks are acquired only during the growing phase and all
   released together at commit/abort. The shrinking phase is instantaneous,
   which avoids cascading aborts.
3. **Deadlock detection** — a waits-for graph is checked for cycles on every
   blocked lock request; a cycle aborts the transaction that detects it.

## Build

```bash
g++ -std=c++17 -pthread -o main main.cpp
```

## Run

```bash
./main
```

## MVCC version visibility

A version is created by `xmin` and invalidated by `xmax` (0 = still live).
For a transaction `T` reading at `snapshot_xid`, the version is visible when:

- `xmin` is our own write, **or** `xmin` is committed and `xmin < snapshot_xid`; **and**
- `xmax == 0`, **or** the deletion is not visible to us (not our own delete,
  and `xmax` not committed before our snapshot).

| Operation | Effect on the version chain                                  |
|-----------|--------------------------------------------------------------|
| INSERT    | push a new version `{value, xmin = T, xmax = 0}`             |
| UPDATE    | stamp the visible version `xmax = T`, then push a new one    |
| DELETE    | stamp the visible version `xmax = T`                         |
| READ      | walk the chain, return the first version visible to `T`      |

## Two-Phase Locking phases

```
GROWING phase    : may acquire new locks, may NOT release
SHRINKING phase  : may release locks, may NOT acquire new ones
```

**Strict 2PL** (used here) only enters the shrinking phase at commit/abort, so
all locks are released together. Attempting to acquire a lock after that point
throws a `2PL violation`.

- SHARED locks are compatible with each other.
- EXCLUSIVE conflicts with both SHARED and EXCLUSIVE.

## Deadlock detection

The lock manager maintains a waits-for graph (`waiter -> holders it blocks on`).
When a request would block, the manager records the edges and runs a DFS cycle
check (O(V+E)). If waiting would close a cycle, a `DeadlockException` is thrown
and the detecting transaction aborts, breaking the cycle and waking the others.

## Scenarios in `main`

1. **MVCC snapshot isolation** — a reader started before a concurrent update
   still sees the old value.
2. **Concurrent shared locks** — two readers hold SHARED locks on the same row
   simultaneously.
3. **Exclusive lock + waiting** — a reader blocks on a writer's EXCLUSIVE lock
   and proceeds once the writer commits.
4. **Deadlock detection** — two transactions lock A and B in opposite order;
   the cycle is detected and one is aborted.

## Example output

```
=== Scenario 1: MVCC Snapshot Isolation ===
[TX 1] COMMITTED
[TX 3] COMMITTED
  [TX 2] READ balance = 1000
[TX 2] COMMITTED

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
[TX 8] COMMITTED
[TX 9] COMMITTED
  Deadlock detected, aborting tx 11
[TX 11] ABORTED
[TX 10] COMMITTED

All scenarios complete.
```

Scenario 4's victim depends on thread timing — the transaction that detects the
cycle is the one aborted, while the other commits.

## Why MVCC *and* 2PL?

| Property               | MVCC alone                   | 2PL alone                | MVCC + Strict 2PL          |
|------------------------|------------------------------|--------------------------|----------------------------|
| Read–write contention  | None (readers use snapshots) | Readers block writers    | None — readers snapshot    |
| Write–write contention | Lost-update risk             | Serialized via X locks   | Serialized via X locks     |
| Serializability        | Snapshot isolation only      | Serializable             | Serializable               |
| Deadlock               | N/A                          | Possible                 | Possible → needs detection |

MVCC removes read–write contention; Strict 2PL preserves serializability for
writes. The price is possible deadlocks, which the waits-for cycle check
resolves.
