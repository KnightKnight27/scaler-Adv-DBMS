# Transaction Manager with MVCC, Strict 2PL, and Deadlock Detection

## Overview

A comprehensive in-memory database transaction manager demonstrating core database concurrency control and recovery principles in C++17.

## Core Features

### Multi-Version Concurrency Control (MVCC)

- Row modifications do not overwrite in place; new versions are prepended to a version chain.
- Each version is tagged with the transaction ID that created it and the timestamp at creation.
- When a transaction aborts, the versions it created are discarded, effectively rolling back the database state.
- Transactions read from the version chain appropriate to their timestamp, enabling snapshot isolation.

### Strict Two-Phase Locking (Strict 2PL)

- Transactions acquire **Shared (S) locks** for reading and **Exclusive (X) locks** for writing.
- All locks are held until the transaction either commits or aborts (no early lock release).
- Ensures conflict serializability and prevents cascading aborts.
- Lock requests that cannot be granted immediately are queued.

### Deadlock Detection (Wait-for Graph)

- A central `DeadlockDetector` manages a directed wait-for graph.
- When a transaction is blocked on a lock, an edge is added from the requesting transaction to blocking transactions.
- Before a transaction is put to sleep, **Depth-First Search (DFS)** traverses the graph to detect cycles.
- If a cycle is found, the requesting transaction is immediately aborted to break the deadlock.

## Project Files

| File | Description |
|------|-------------|
| `mvcc.h` | Version and VersionChain classes for multi-version storage. |
| `lock_manager.h` / `lock_manager.cc` | Lock acquisition/release with Strict 2PL. |
| `deadlock_detector.h` / `deadlock_detector.cc` | Wait-for graph and cycle detection via DFS. |
| `transaction_manager.h` / `transaction_manager.cc` | Orchestrates MVCC, locking, and recovery. |
| `main.cpp` | Demo scenarios: basic read-write, concurrent reads, write conflicts, abort/rollback. |
| `makefile` | Build script with C++17 and optimization flags. |
| `readme.md` | This documentation. |

## Build

```bash
cd Lab-6
make
```

## Run

```bash
./transaction_manager
```

## Design Details

### Transaction State Machine

```
ACTIVE -> COMMITTED
       \-> ABORTED
```

### Version Chain Example

For key `account_A`:
- Version 0: Created by TX1 at timestamp 1, data = "1000"
- Version 1: Created by TX4 at timestamp 3, data = "2000"
- Version 2: Created by TX5 at timestamp 4, data = "3000"

When TX2 (timestamp 2) reads `account_A`, it sees Version 0.
When TX4 reads after commit, it sees Version 2 (or its own version).

### Lock Conflict Matrix

|       | S Lock | X Lock |
|-------|--------|--------|
| S Lock | ✓      | ✗      |
| X Lock | ✗      | ✗      |

- Shared locks don't conflict with other shared locks.
- Exclusive locks conflict with both shared and exclusive locks.

### Deadlock Detection Algorithm

1. Add wait-for edge: TX_waiter → TX_blocker
2. Run DFS from all unvisited nodes
3. If a back edge is found (node in recursion stack), cycle exists
4. Abort requesting transaction if cycle detected

## Time Complexity

- **Lock acquisition**: O(k) where k = number of locks on the resource
- **Deadlock detection**: O(V + E) where V = transactions, E = wait-for edges
- **Read/Write**: O(k + V) amortized

## Example Output

```
TX1 started (timestamp=1)
TX1 write account_A=1000
TX1 write account_B=500
TX1 committed

=== Version Chains ===
Key: account_A | Versions: V0(TX1)
Key: account_B | Versions: V0(TX1)

TX2 started (timestamp=2)
TX2 read account_A=1000
TX2 committed
```

## Future Enhancements

- Add checkpointing and write-ahead logging (WAL)
- Implement snapshot isolation levels
- Support range locks
- Add performance benchmarking
- Implement transaction grouping (batching)

## References

- Concurrency Control and Recovery in Database Systems by Philip A. Bernstein
- Database Systems: The Complete Book (2nd ed.)
