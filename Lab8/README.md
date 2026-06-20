# Lab 8 - Transaction Manager using MVCC and Two Phase Locking

**Name:** Srujan Gowda KS  
**Roll Number:** 24BCS10339

## Objective

The objective of this lab was to implement a simple transaction manager that demonstrates the basic concepts of database concurrency control. The implementation includes Multi Version Concurrency Control (MVCC), Two Phase Locking (2PL), and Deadlock Detection.

These mechanisms help maintain consistency when multiple transactions access shared data simultaneously.

---

## Files

### transaction_manager.cpp

This file contains the complete implementation of:

- MVCC based storage
- Lock management
- Deadlock detection
- Transaction operations such as begin, commit, and abort

---

## Concepts Used

### MVCC

MVCC allows multiple versions of the same data item to exist. Instead of updating a value directly, a new version is created for every write operation. Older versions remain available for reference.

### Two Phase Locking (2PL)

Before accessing a data item, a transaction must acquire a lock. If another transaction already owns the lock, the requesting transaction waits until the resource becomes available.

### Deadlock Detection

A wait-for graph is maintained to represent dependencies between transactions. If a cycle is found in the graph, a deadlock is detected and one transaction is aborted.

---

## Data Structures Used

- `unordered_map<string, vector<Version>>` for storing multiple versions of data
- `unordered_map<string, int>` for tracking lock ownership
- `unordered_map<int, set<int>>` for maintaining the wait-for graph
- `queue<int>` for deadlock detection using graph traversal

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

```text
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

This lab demonstrated the implementation of a basic transaction manager using MVCC for maintaining multiple versions of data, Two Phase Locking for synchronization, and deadlock detection using a wait-for graph. These concepts form the foundation of concurrency control mechanisms used in modern database systems.