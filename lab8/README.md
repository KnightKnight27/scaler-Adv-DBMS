# Lab 8 — Transaction Manager: MVCC + Strict 2PL + Deadlock Detection

**Vimal Kumar Yadav · 24BCS10273**

A small in-memory transaction manager in C++17 that combines the three classic
concurrency-control mechanisms into one coherent model (an MV2PL-style design):

1. **MVCC version chains** — every key keeps a chain of versions tagged with
   begin/end commit timestamps. Read-only transactions take a start-time
   snapshot and read **without locks**, so they never block and never deadlock.
2. **Strict 2PL** — read-write transactions take Shared locks to read and
   Exclusive locks to write (with `S → X` upgrade). All locks are held until
   commit or abort (the *strict* part), which gives serializable schedules.
3. **Deadlock detection** — when a lock request blocks, the manager builds the
   waits-for graph, searches it for a cycle, and aborts the youngest member as
   the victim so the older transaction makes progress.

---

## Build & run

Requires a C++17 compiler and CMake ≥ 3.16.

```bash
cd lab8
cmake -S . -B build
cmake --build build
./build/lab8            # Windows: .\build\Release\lab8.exe
```

Direct compilation:

```bash
g++ -std=c++17 -Iinclude src/main.cpp src/txn/*.cpp -o lab8
```

---

## What the demo shows

`src/main.cpp` runs three scenarios. Real program output:

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

## How it fits together

```
              TransactionManager  (orchestrator)
              /        |          \
   ILockManager   IVersionStore   IDeadlockDetector
   (Strict 2PL)   (MVCC chains)   (waits-for cycle)
```

A data operation flows like this:

- **`read` (read-write txn)** → acquire `S` lock → if blocked, run deadlock
  detection → read the newest committed version (`readCurrent`; the lock, not
  the snapshot, provides isolation).
- **`write`** → acquire `X` lock (upgrade if already `S`) → append a new
  uncommitted version to the chain.
- **`snapshotRead` (read-only txn)** → no lock; read the newest version
  committed at or before the transaction's start timestamp (`readSnapshot`).
- **`commit`** → stamp the transaction's versions with a commit timestamp,
  close the previous versions' `endTs`, release all locks.
- **`abort`** → drop the transaction's uncommitted versions, release all locks.

### Lock compatibility

|            | held S | held X |
| ---------- | ------ | ------ |
| request S  | ✓      | ✗      |
| request X  | ✗      | ✗      |

`S → X` upgrade is granted when the requester is the only holder; otherwise it
waits (and may participate in a deadlock).

### Version visibility

A version is visible to a snapshot timestamp `ts` once it is committed with
`beginTs ≤ ts < endTs`. A transaction always sees its own pending write
(read-your-writes). Committing version *v* sets `v.beginTs` and closes the
`endTs` of the version it superseded — that is what lets an older reader keep
seeing the older value.

### Deadlock detection

The waits-for graph has an edge `waiter → holder` for every blocked request and
each incompatible lock holder on that key. A depth-first search with a recursion
stack finds a cycle; the victim is the **youngest** transaction (highest id) in
the cycle, guaranteeing the older transaction survives. Aborting it releases its
locks, which unblocks the waiter on retry.

---

## SOLID mapping

- **Single Responsibility** — locking (`LockManager`), versioning
  (`VersionStore`), and cycle detection (`DeadlockDetector`) are independent
  units; `TransactionManager` only orchestrates them.
- **Open/Closed** — lock compatibility and visibility rules are localised, and
  the victim-selection policy lives entirely inside `DeadlockDetector`, so a
  different policy (e.g. fewest-locks) is a one-class change.
- **Liskov Substitution** — every component is used only through its interface;
  the manager never depends on a concrete type.
- **Interface Segregation** — three small contracts (`ILockManager`,
  `IVersionStore`, `IDeadlockDetector`) instead of one fat interface.
- **Dependency Inversion** — `TransactionManager` depends on the interfaces and
  accepts implementations via constructor injection (a default constructor
  wires the standard concrete components).

---

## Layout

```
lab8/
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

## Design notes & limitations

- Single-threaded, deterministic simulation: a blocked operation returns
  `Blocked` and the caller retries after the blocker releases. This keeps the
  three mechanisms observable and reproducible without thread-timing flakiness.
- Lock granting is compatibility-based and does not enforce FIFO queue order, so
  it does not guarantee freedom from starvation — adequate for demonstrating the
  mechanisms, not a production scheduler.
- Read-write transactions use locks for isolation; read-only transactions use
  MVCC snapshots. Mixing the two yields snapshot-isolation semantics for
  read-only work and serializable semantics among read-write transactions.
