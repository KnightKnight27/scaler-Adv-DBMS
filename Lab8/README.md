# Lab 8 - Transaction Manager (MVCC + 2PL + Deadlock Detection)

24BCS10079 - Piyush Bansal

## Objective

The objective of this lab was to build a simple transaction manager that uses:

1. MVCC (Multi Version Concurrency Control) - every write makes a new version of the value.
2. Two Phase Locking (2PL) - locks are taken before using an item and released later.
3. Deadlock Detection - find if transactions are waiting on each other in a cycle.

This is similar to how databases like PostgreSQL handle concurrent transactions.

---

## Files

### transaction_manager.cpp

Contains three parts:

* `MVCCStorage` - stores multiple versions of each key.
* `LockManager` - gives and releases locks, and builds a wait-for graph.
* `detectDeadlock()` - checks the wait-for graph for a cycle.

---

## Concepts Used

### MVCC

Instead of overwriting a value, every write creates a new `Version` with the
value and the transaction id that wrote it. The latest version is the current
value, but old versions are still kept.

```cpp
struct Version {
    int value;
    int txnId;
};
```

### Two Phase Locking

A transaction must acquire a lock on an item before writing it. If another
transaction already holds the lock, the new transaction has to wait. Locks are
released at the end of the transaction.

### Deadlock Detection

When a transaction waits for another, an edge is added to the wait-for graph.
To detect a deadlock we run a topological sort (Kahn's algorithm). If we cannot
visit all the nodes, there is a cycle, which means a deadlock.

---

## Data Structures Used

* `unordered_map<string, vector<Version>>` - version chain per key
* `unordered_map<string, int>` - lock owner of each item
* `unordered_map<int, set<int>>` - wait-for graph
* `queue` - for the topological sort

---

## Compilation

```bash
g++ transaction_manager.cpp -o txn
```

## Execution

```bash
./txn
```

---

## Sample Output

```
T1 started
T2 started
T1 got lock on A
T2 got lock on B
T1 waiting for T2
T2 waiting for T1

Deadlock Detected!
T2 aborted
Versions for A: [100, T1]
Versions for B: [200, T2]
T1 committed
```

---

## Conclusion

In this lab a basic transaction manager was built using MVCC for keeping
multiple versions, 2PL for locking items, and a wait-for graph with topological
sort for detecting deadlocks. This showed how a database keeps data consistent
when many transactions run together.
