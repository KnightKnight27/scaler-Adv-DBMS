# Lab Session 6: Transaction Manager

## Objective
For this lab, I built a transaction manager that utilizes both MVCC and Two-Phase Locking in C++. The main goal was to replicate the core concepts used in PostgreSQL's concurrency setup.

The features I implemented are:
1. **MVCC**: Handling multiple versions per row so that reading transactions see a consistent view without getting blocked by writers.
2. **Strict Two-Phase Locking (2PL)**: Enforcing lock acquisitions during the growing phase, with all lock releases happening at the end when the transaction commits or aborts (the shrinking phase).
3. **Deadlock Detection**: Added cycle detection on a waits-for graph so that we can abort a transaction if a deadlock is formed.

## Implementation Concepts

### MVCC Version Visibility
A row version that was created by transaction `xmin` and removed by `xmax` is visible to another transaction `T` if:
- `xmin` is committed and its value is `<= T.snapshot_xid`
- `xmax` is either 0 (meaning it's not deleted), `> T.snapshot_xid`, or belongs to an aborted transaction.

### 2PL Phase Details
There are two main phases in 2PL:
- **Growing phase**: The transaction can grab new locks but shouldn't release any.
- **Shrinking phase**: The transaction releases locks. In my strict 2PL implementation, this phase just happens right at the commit or abort point.

### Dealing with Deadlocks
If Transaction A needs a lock that B holds, and B needs one that A holds, a cycle is created. When I detect this cycle through the graph, I abort the transaction causing the issue to break the deadlock.

## How to Compile and Run
To test my implementation, you can run the following command to compile the C++ file and execute the output:
```bash
g++ -std=c++17 -pthread -o txmgr txmgr.cpp
./txmgr
```

You'll see output demonstrating MVCC isolation, shared locks, lock waiting, and deadlock detection working as intended.
