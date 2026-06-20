# Lab 8 - Transaction Manager with MVCC and Strict 2PL

## Objective

Build a small in-memory transaction manager that demonstrates the concurrency-control pieces used by relational databases:

- MVCC snapshot reads so readers can see a stable committed view without taking read locks.
- Strict two-phase locking for writes, where exclusive locks are held until commit or abort.
- A waits-for graph for deadlock detection.
- Rollback cleanup for aborted transactions.
- A first-updater-wins validation step to reject stale writes.

## Files

| File | Purpose |
|------|---------|
| `txn_manager.cpp` | Complete transaction manager implementation and deterministic demo. |
| `CMakeLists.txt` | Builds the `lab8_txmgr` executable. |

## Build and Run

```bash
cd lab8
g++ -std=c++17 -Wall -Wextra -pedantic -o lab8_txmgr txn_manager.cpp
./lab8_txmgr
```

Or with CMake:

```bash
cmake -S lab8 -B lab8/build
cmake --build lab8/build
./lab8/build/lab8_txmgr
```

## What the Demo Covers

1. Snapshot isolation: an old reader keeps seeing the value that was committed before its snapshot.
2. First-updater-wins: a transaction that writes from a stale snapshot is aborted during commit validation.
3. Deadlock detection: two writers wait on each other's exclusive locks, a cycle is found, and one transaction is aborted.

## Data Structures

- `unordered_map<Key, list<Version>>` stores the version chain for each logical row.
- `unordered_map<Key, TxId>` tracks the transaction holding each exclusive write lock.
- `unordered_map<TxId, unordered_set<TxId>>` stores waits-for edges.
- Each transaction records its snapshot timestamp and write set.
