# Lab Session 8: Transaction Manager — MVCC + Two-Phase Locking

This directory contains the implementation of **Lab 8**, which demonstrates a Transaction Manager featuring Multi-Version Concurrency Control (MVCC), Strict Two-Phase Locking (2PL), and automatic Deadlock Detection using a waits-for graph.

## Objective
Build a transaction manager that combines:
1. **MVCC** — Every write creates a new row version; readers see a consistent snapshot without blocking writers.
2. **Strict Two-Phase Locking (2PL)** — Lock acquisition is bounded to the "growing" phase; locks are held until commit/abort, which becomes the instantaneous "shrinking" phase.
3. **Deadlock detection** — Utilizes a waits-for graph and cycle detection (Depth-First Search) to abort a transaction when a cyclic dependency forms.

This mirrors the core of PostgreSQL's concurrency architecture (MVCC snapshot isolation + predicate/row-level locks).

## Concepts

### MVCC Version Visibility Rule
A version created by transaction `xmin` and invalidated by `xmax` is visible to transaction `T` if:
- `xmin` is committed and `xmin <= T.snapshot_xid`
- `xmax` is 0 (not yet deleted) OR `xmax > T.snapshot_xid` OR `xmax` is aborted

### Two-Phase Locking Phases
- **GROWING phase:** Transaction may acquire new locks, but may NOT release any.
- **SHRINKING phase:** Transaction may release locks, but may NOT acquire new ones. In *Strict 2PL*, this phase only happens atomically at the very end of the transaction (commit/abort).

### Deadlock
Transaction A waits for a lock held by B; B waits for a lock held by A. This cycle in the waits-for graph prompts the system to abort one of the transactions.

## Implementation Details
The project is built entirely in C++ (`txmgr.cpp`) and models:
- A global transaction table containing snapshots and Tx status.
- An MVCC heap maintaining version chains for each row key.
- A lock manager handling shared/exclusive modes across transactions.
- Four concrete multithreaded test scenarios showcasing isolation, locking wait semantics, and deadlock abortion.

## How to Compile & Run
A modern C++ compiler (C++17 or later) with pthread support is required.

```bash
g++ -std=c++17 -pthread -o txmgr txmgr.cpp
./txmgr
```

### Expected Output
The test driver runs 4 scenarios:
1. **MVCC Snapshot Isolation:** Confirms that readers see the appropriate pre-commit values without blocking.
2. **Concurrent Shared Locks:** Asserts multiple read-only transactions can share access seamlessly.
3. **Exclusive Lock + Waiting:** Demonstrates readers blocking when another transaction holds an exclusive lock on the requested record.
4. **Deadlock Detection:** Purposely forces a cycle in the waits-for graph and verifies that the deadlock exception triggers a rollback (abort).
