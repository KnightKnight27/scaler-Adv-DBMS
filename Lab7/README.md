# Lab Session 7: Transaction Manager — MVCC + Two-Phase Locking

This project implements a simulated database Transaction Manager in C++ that demonstrates two key concurrency control mechanisms working together:
1. **Multi-Version Concurrency Control (MVCC)**
2. **Strict Two-Phase Locking (2PL)**

## Features

- **MVCC Version Chain**: Every write creates a new row version rather than overwriting in place. Readers traverse the version chain and use their transaction snapshot to find the correct, consistent version of the row without ever blocking writers.
- **Strict 2PL**: Transactions acquire `SHARED` or `EXCLUSIVE` locks during their "growing" phase. Locks are held strictly until the transaction commits or aborts (the "shrinking" phase), preventing cascading rollbacks and ensuring serializability.
- **Deadlock Detection**: Implements a waits-for graph. When a transaction requests a lock held by another transaction, it runs a Depth-First Search (DFS) cycle detection to find deadlocks and aborts the transaction if a cycle is found.

## Prerequisites
- A C++17 compatible compiler (e.g., GCC, Clang)
- Support for POSIX threads (`-pthread`)

## Compilation & Execution
To compile and run the project, execute the following commands in your terminal:

```bash
g++ -std=c++17 -pthread -o txmgr txmgr.cpp
./txmgr
```

## Demo Scenarios
When you run the executable, it demonstrates four scenarios:
1. **MVCC Snapshot Isolation**: Shows a reader seeing a consistent older version of a row (`1000`) while another transaction updates it.
2. **Concurrent Shared Locks**: Shows two transactions holding `SHARED` locks on the same row concurrently without blocking.
3. **Exclusive Lock Waiting**: Shows a reader waiting for a writer's `EXCLUSIVE` lock to be released before proceeding.
4. **Deadlock Detection**: Creates a cyclic dependency (A waits for B, B waits for A) and shows the Transaction Manager successfully detecting it and aborting the younger transaction.

## Expected Output
```text
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

## Architecture
The application interfaces with `TransactionManager`, which coordinates between:
- `LockManager`: Handles acquiring `SHARED` and `EXCLUSIVE` locks and deadlock detection.
- `MVCC Heap`: Manages the linked list of row versions and evaluates the visibility rules (`xmin`/`xmax`).
