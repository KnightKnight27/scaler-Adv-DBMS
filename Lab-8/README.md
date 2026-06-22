# Lab 8: Transaction Manager with MVCC and 2PL

## Overview

This project implements a simplified Transaction Manager supporting:
* **Multi-Version Concurrency Control (MVCC)** for snapshot isolation. Readers do not block writers, and writers do not block readers.
* **Strict Two-Phase Locking (2PL)** for serialization of write operations. All locks are held until transaction commit/abort (growing phase boundary at transaction end).
* **Waits-For Graph Cycle Detection** to resolve deadlocks. When a deadlock is detected via DFS cycle search, the younger/initiating transaction is aborted with a `DeadlockException`.

## Implementation Details

### MVCC Row versioning
Each key in the database storage maps to a linked list (chain) of row versions:
```cpp
struct RowVersion {
    std::string value;
    TxID xmin = 0;  // transaction ID that created this version
    TxID xmax = 0;  // transaction ID that deleted/superseded this version
};
```
A version is visible to a reading transaction `T` with snapshot ID `snapshot_xid` if:
* `xmin` is committed and `xmin <= snapshot_xid` (or `xmin == T.id` for own writes).
* `xmax` is not committed or `xmax > snapshot_xid` (or `xmax` is aborted).

### Lock Manager & Strict 2PL
* **Shared (S) locks** are requested for reads.
* **Exclusive (X) locks** are requested for writes (inserts, updates, and deletes).
* **Strict 2PL** ensures that transaction lock release only occurs at the commit or abort stage.
* **Shrinking phase violation check**: Re-acquiring locks during the shrinking phase throws a runtime error.

### Deadlock Detection
* The lock manager maintains a dependency graph `waits_for_` of format `waiter -> set of holders`.
* Every block triggers a Depth First Search (DFS) traversal to detect cycles.
* If a cycle is detected, a `DeadlockException` is thrown, triggering a rollback.

---

## Build and Run

To compile and execute:

```bash
# In the Lab-8 directory
mkdir build
cd build
cmake ..
cmake --build .

# Run the demo executable
./transaction_demo
```

## Demonstration Scenarios

The main execution showcases four scenarios:
1. **MVCC Snapshot Isolation**: Transactions see a consistent snapshot matching the point in time they began.
2. **Concurrent Shared Locks**: Multiple transactions can hold shared locks concurrently.
3. **Exclusive Lock Blocking a Reader**: A writer holding an exclusive lock blocks a reader until commit.
4. **Deadlock Detection**: Two transactions attempting to acquire exclusive locks in opposing orders trigger a cycle, resulting in one being aborted.
