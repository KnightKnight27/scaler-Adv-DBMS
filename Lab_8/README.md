# Lab 8: Concurrency Control and Transaction Engine (MVCC & Strict 2PL)

## Objectives & Scope

This project implements a lightweight database transaction scheduler supporting:
* **Multi-Version Concurrency Control (MVCC)**: Offers snapshot isolation. Read queries read older committed versions without blocking writers, and update queries write new versions without blocking readers.
* **Strict Two-Phase Locking (2PL)**: Guarantees serializability for data modification operations. All acquired locks are retained until the transaction finishes (commits or aborts).
* **Deadlock Detection (Waits-For Graph)**: Checks for deadlock cycles on resource requests using DFS (Depth-First Search). Deadlocked transactions are aborted by throwing a `CycleDetectedException`.

---

## Architectural Details

### MVCC Rows and Versions
Every entry in the system storage holds a list of historic version states:
```cpp
struct DbVersion {
    std::string content;
    TxnID createdTx = 0; // xmin
    TxnID deletedTx = 0; // xmax
};
```
A reading transaction `T` (with Snapshot ID `snapId`) can access a version if:
- `createdTx` has committed and `createdTx < snapId` (or `createdTx` equals `T.txId`).
- `deletedTx` is either not committed or `deletedTx > snapId` (or `deletedTx` was aborted).

### Lock Management & strict 2PL
- **Read (S) locks** are requested for read access.
- **Write (X) locks** are requested for inserts, updates, and deletes.
- **Strict 2PL** guarantees locks are released only at the termination (commit/abort) of the transaction.
- **Shrinking Phase Check**: Attempting to acquire new locks after entering the shrinking phase raises a runtime error.

### Deadlock Resolution
- The lock table maintains a dependency map (`dependencyGraph_`) containing: `waiting txn -> set of holding txns`.
- Every block event performs a Depth-First Search cycle check.
- If a cycle is resolved, a `CycleDetectedException` causes the younger blocking transaction to rollback.

---

## Compiling and Running

Compile directly with a C++17 compiler:

```bash
# Compile
clang++ -std=c++17 main.cpp transaction_manager.cpp -o txn_engine_demo

# Run
./txn_engine_demo
```

Alternatively, use CMake:

```bash
mkdir build
cd build
cmake ..
cmake --build .
./txn_engine_demo
```

---

## Test Cases Covered

The driver program verifies four main database concurrency scenarios:
1. **MVCC Snapshot Isolation**: Transactions see a frozen snapshot of values current to their start time.
2. **Concurrent Shared Locks**: Multiple transactions read from the same resource concurrently.
3. **Exclusive Lock Blocking**: Exclusive write locks block other read attempts until final commit.
4. **Deadlock Detection**: Mutually blocking resource queries are identified, and the deadlock is broken by aborting one transaction.