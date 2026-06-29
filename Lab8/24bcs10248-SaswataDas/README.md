# Lab 8 — In-Memory Transaction Manager with MVCC, Strict 2PL, and Deadlock Detection

**Saswata Das · 24BCS10248**

This project implements a C++17 transaction manager that unifies three foundational concurrency-control techniques into a single MV2PL-style engine:

- **Multi-Version Concurrency Control (MVCC):** Each key maintains a linked chain of timestamped versions. Read-only transactions capture a snapshot at their start time and can read without acquiring any locks — they never block or participate in deadlocks.
- **Strict Two-Phase Locking (2PL):** Read-write transactions acquire Shared (S) locks for reads and Exclusive (X) locks for writes, with support for S-to-X upgrades. Every lock is retained until the transaction either commits or aborts, which ensures serializability.
- **Deadlock Detection via Waits-For Graphs:** Whenever a lock request is queued, the system constructs a waits-for graph and performs a DFS-based cycle search. If a cycle exists, the youngest transaction in the cycle is selected as the victim and aborted so the older one can proceed.

---

## Building and Running

You need a C++17-capable compiler and CMake 3.16 or later.

```bash
cd Lab8/24bcs10248-SaswataDas
cmake -S . -B build
cmake --build build
./build/Lab8            # On Windows: .\build\Release\Lab8.exe
```

Alternatively, compile directly:

```bash
g++ -std=c++17 -Iinclude src/main.cpp src/txn/*.cpp -o Lab8
```

---

## Demo Scenarios

The `src/main.cpp` driver executes three demonstrations that exercise each mechanism:

```
========== 1. MVCC version chains / snapshot reads ==========
T1 begin (ts=1)
T1 commit (ts=2)
T2 begin (read-only, snapshot ts=2)
reader sees A = 100
T3 begin (ts=2)
T3 commit (ts=3)
after writer commits 999:
  old reader still sees A = 100  (its snapshot predates the new version)
T4 begin (read-only, snapshot ts=3)
  fresh reader sees A = 999
version chain for A (newest first):
  A: [v=999 by T3 begin=3 end=INF] -> [v=100 by T1 begin=2 end=3]

========== 2. Strict 2PL: write lock blocks a reader until commit ==========
T6 write X=20 -> Ok
  T7 waits for S-lock on X
T7 read X -> Blocked (blocked by T6's X lock)
T6 commits, releasing its locks
T7 retries read X -> Ok, value = 20

========== 3. Deadlock detection (waits-for cycle) ==========
T8 write P -> Ok
T9 write Q -> Ok
T8 write Q -> Blocked (waits for T9)
  T9 waits for X-lock on P
  ! deadlock detected -> aborting victim T9
T9 write P -> Aborted
outcome: T8 is Active, T9 is Aborted
T8 retries write Q -> Ok
final: T8 is Committed
```

---

## Architecture Overview

The system is composed of a central orchestrator that delegates to three independent subsystems:

```
           TransactionManager  (orchestrator)
           /        |          \
  ILockManager  IVersionStore  IDeadlockDetector
  (Strict 2PL)  (MVCC chains)  (waits-for cycle)
```

The flow for each operation type:

- **`read` (read-write txn):** Acquires an `S` lock on the key. If the lock is blocked, deadlock detection runs. Once granted, returns the latest committed value via `readCurrent` (the lock, not a timestamp snapshot, provides isolation).
- **`write`:** Acquires an `X` lock (upgrading from `S` if already held), then appends a new uncommitted version to the key's chain.
- **`snapshotRead` (read-only txn):** Bypasses locking entirely. Uses `readSnapshot` to find the most recent version committed at or before the transaction's start timestamp.
- **`commit`:** Assigns a commit timestamp to the transaction's versions, closes the `endTs` of superseded versions, and releases all held locks.
- **`abort`:** Removes the transaction's uncommitted versions from all chains and releases its locks.

### Lock Compatibility Matrix

|            | Held S | Held X |
| ---------- | ------ | ------ |
| Request S  | ✓      | ✗      |
| Request X  | ✗      | ✗      |

An `S → X` upgrade succeeds only when the requesting transaction is the sole lock holder on the key. Otherwise, the upgrade request is queued and may trigger a deadlock check.

### How Version Visibility Works

A version becomes visible to a snapshot with timestamp `ts` once it is committed and satisfies `beginTs ≤ ts < endTs`. Transactions always see their own uncommitted writes (read-your-writes semantics). When a version *v* is committed, its `beginTs` is set to the commit timestamp and the prior version's `endTs` is closed to that same value — this is what allows older readers to continue seeing the previous value through their snapshots.

### How Deadlocks Are Resolved

The waits-for graph contains an edge from each waiting transaction to every incompatible lock holder on the contested key. A depth-first traversal with a gray/white/black coloring scheme detects back edges (cycles). The victim is always the transaction with the **highest id** (youngest), ensuring older work is preserved. Aborting the victim releases its locks, letting the blocked transaction succeed on retry.

---

## Adherence to SOLID Principles

- **Single Responsibility:** Locking logic (`LockManager`), version management (`VersionStore`), and cycle detection (`DeadlockDetector`) are self-contained modules. `TransactionManager` serves purely as a coordinator.
- **Open/Closed:** Policies like lock compatibility rules and victim selection are encapsulated within their respective classes, so swapping in a different strategy (e.g., abort the transaction holding the fewest locks) requires changing only one component.
- **Liskov Substitution:** Every subsystem is referenced exclusively through its abstract interface, so any conforming implementation can be substituted transparently.
- **Interface Segregation:** Three narrow contracts (`ILockManager`, `IVersionStore`, `IDeadlockDetector`) replace what could have been one monolithic interface.
- **Dependency Inversion:** `TransactionManager` depends on abstractions rather than concrete types, accepting implementations through constructor injection.

---

## Project Structure

```
Lab8/24bcs10248-SaswataDas/
├── CMakeLists.txt
├── README.md
├── include/txn/
│   ├── Types.h, Transaction.h        core types & enums
│   ├── Interfaces.h                  ILockManager / IVersionStore / IDeadlockDetector
│   ├── LockManager.h                 Strict 2PL lock table
│   ├── VersionStore.h                MVCC version chains
│   ├── DeadlockDetector.h            waits-for cycle search
│   └── TransactionManager.h          orchestrator
└── src/
    ├── txn/*.cpp                      implementations
    └── main.cpp                       three-scenario demo
```

## Design Considerations and Limitations

- The entire system runs single-threaded with deterministic simulation: a blocked operation returns `Blocked` and the caller retries once the blocker releases its locks. This approach keeps the three concurrency mechanisms clearly observable and reproducible, avoiding non-determinism from thread scheduling.
- Lock granting is based on compatibility checks rather than strict FIFO ordering, so starvation is theoretically possible. This trade-off is acceptable for a demonstration system but would need addressing in a production lock scheduler.
- Read-write transactions rely on locks for isolation while read-only transactions use MVCC snapshots. This hybrid approach provides snapshot-isolation semantics for readers and full serializability among writers.
