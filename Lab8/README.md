# Lab 8 — Simplified Transaction Manager (C++)

- **Role Number:** 24BCS10345
- **Name:** Ansh Mahajan
- **Date:** 2026-06-22

## Aim

Build a small transaction manager in C++ that ties together the four pieces of
concurrency control a real database relies on, and show them interacting:

1. **MVCC version chains** — writes append new versions instead of overwriting.
2. **Strict Two-Phase Locking (2PL)** — locks are taken before access and held
   until commit/abort, which guarantees serializability.
3. **Deadlock detection** — a Wait-For Graph is maintained; a cycle is a
   deadlock.
4. **Transaction lifecycle** — `begin → (read | write)* → commit | abort`.

## How each mechanism is implemented

### MVCC version chains
Each key maps to a `std::vector<Version>`, where `Version{ value, writer,
committed }`. A `WRITE` always **appends** a new (uncommitted) version tagged
with the writing transaction's id. A `READ` walks the chain from newest to
oldest and returns the first version that is **either the reader's own write or
already committed** — so a transaction sees its own changes, everyone else sees
only committed data, and old versions remain intact.

### Strict 2PL
A `READ` needs a **Shared (S)** lock, a `WRITE` an **Exclusive (X)** lock.
Compatibility is the standard matrix: S/S is fine, anything involving X
conflicts. The sole holder of an S lock may upgrade it to X. Crucially, locks
are released **only** in `commit()`/`abort()` — never mid-transaction — which is
exactly what makes the schedule serializable.

### Deadlock detection
When a request cannot be granted, the transaction is queued and an edge
`waiter → holder` is added to the Wait-For Graph. A DFS then checks whether the
new edge closes a cycle. If it does, the requesting transaction is picked as the
**victim** and aborted (rolling back its versions and releasing its locks),
which immediately wakes whoever was waiting behind it.

### Single-threaded resumption
The simulation runs on one thread, so a blocked operation is **parked** with its
pending action (`Read`/`Write` + key + value). When the lock it needs is later
freed, `drainQueue` grants it and `resume` replays the parked action — modelling
what a blocked thread would do on wake-up, but deterministically.

## Build and run

```bash
# Direct (as specified)
g++ -std=c++17 transaction_manager.cpp -o txn
./txn

# Or with CMake
cmake -S . -B build && cmake --build build
./build/txn
```

## Sample run (annotated)

```text
========== Scenario 1: MVCC version chain + lifecycle ==========
T1: WRITE A = 10 -> ok (new version)
T1: WRITE A = 20 -> ok (new version)     # second version, first is kept
T1: READ A -> 20                          # sees its own latest write
T1: COMMIT (versions published)
T2: READ A -> 20                          # sees T1's committed value
version chain of A: [v1=10 byT1 committed] -> [v2=20 byT1 committed]

========== Scenario 2: Strict 2PL - reader blocks on a writer ==========
T3: WRITE B = 99 -> ok (new version)
T4: READ B -> blocked (waiting for lock)  # S request conflicts with T3's X
T3: COMMIT (versions published)
  -> T4 granted S on B, resuming          # release wakes the waiter
     T4: READ B -> 99

========== Scenario 3: Deadlock detection + resolution ==========
T5: WRITE X = 1 -> ok        # T5 holds X(X)
T6: WRITE Y = 2 -> ok        # T6 holds X(Y)
T5: WRITE Y = 3 -> blocked   # edge T5 -> T6
T6: WRITE X = 4 -> DEADLOCK detected (cycle in wait-for graph); chosen as victim
T6: ABORT (writes rolled back)            # Y's uncommitted version removed
  -> T5 granted X on Y, resuming          # T5 unblocks and finishes
     T5: WRITE Y = 3 -> ok (new version)
T5: COMMIT (versions published)
version chain of X: [v1=1 byT5 committed]
version chain of Y: [v1=3 byT5 committed]
```

The output is the proof: Scenario 1 keeps both versions of `A`; Scenario 2
shows the reader genuinely waiting until the writer commits (strict 2PL);
Scenario 3 forms a real `T5 → T6 → T5` cycle, detects it, aborts the victim,
rolls back its write, and lets the survivor complete — so `Y` ends as `3` from
the committed `T5`, with T6's `2` gone.

## Files

| File | Purpose |
| --- | --- |
| [transaction_manager.cpp](transaction_manager.cpp) | MVCC + strict 2PL + deadlock detection + lifecycle |
| [CMakeLists.txt](CMakeLists.txt) | CMake build (C++17, warnings on) |
| `README.md` | This write-up |
