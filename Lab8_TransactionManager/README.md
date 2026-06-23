# Lab 6 - Transaction Manager with MVCC, Strict 2PL, and Deadlock Detection

**Name:** Abhijit P
**Roll No:** 24bcs10175

## Objective

The objective of this lab was to implement a simplified transaction management system in C++ that demonstrates core database concurrency control concepts.

The implementation includes:

* Multi-Version Concurrency Control (MVCC)
* Version Chains
* Strict Two-Phase Locking (Strict 2PL)
* Deadlock Detection using a Wait-For Graph
* Transaction Lifecycle Management

---

## Overview

Database systems execute multiple transactions concurrently while maintaining consistency and isolation.

This lab simulates a transaction manager capable of:

* Starting transactions
* Reading and writing data
* Managing multiple versions of records
* Acquiring and releasing locks
* Detecting deadlocks
* Committing and aborting transactions

---

# Part 1 - Multi-Version Concurrency Control (MVCC)

## What is MVCC?

Multi-Version Concurrency Control allows multiple versions of the same record to exist simultaneously.

Instead of overwriting data, each update creates a new version.

This enables concurrent reads and writes while reducing blocking.

---

## Version Structure

Each version contains:

* Transaction ID
* Stored Value
* Pointer to Previous Version

Example:

```text
Version(T3, Charlie)
        ↓
Version(T2, Bob)
        ↓
Version(T1, Alice)
```

---

## Version Chains

The implementation stores versions as a linked list.

Example:

```cpp
store.write(1, 1, "Alice");
store.write(1, 2, "Bob");
store.write(1, 3, "Charlie");
```

Version Chain:

```text
[T3: Charlie] -> [T2: Bob] -> [T1: Alice]
```

---

## MVCC Operations

### Read

Returns the latest visible version.

### Write

Creates a new version and places it at the head of the version chain.

---

# Part 2 - Strict Two-Phase Locking (Strict 2PL)

## What is Strict 2PL?

Strict Two-Phase Locking is a concurrency control protocol that prevents conflicting operations.

Transactions acquire locks before accessing data and release all locks only when they commit or abort.

---

## Lock Types

### Shared Lock (S)

Used for read operations.

Multiple transactions can hold shared locks simultaneously.

Example:

```text
T1 reads Row1
T2 reads Row1
```

Allowed:

```text
Shared Lock
```

---

### Exclusive Lock (X)

Used for write operations.

Only one transaction can hold an exclusive lock on a resource.

Example:

```text
T1 writes Row1
```

Allowed:

```text
Exclusive Lock
```

Any other transaction requesting access must wait.

---

## Lock Manager Responsibilities

The Lock Manager:

* Tracks active locks
* Grants shared locks
* Grants exclusive locks
* Prevents conflicting access
* Releases locks during commit or abort

---

# Part 3 - Deadlock Detection

## What is a Deadlock?

A deadlock occurs when transactions wait indefinitely for one another.

Example:

```text
T1 waits for T2
T2 waits for T3
T3 waits for T1
```

None of the transactions can proceed.

---

## Wait-For Graph

The implementation models transaction dependencies using a Wait-For Graph.

Example:

```text
T1 -> T2
T2 -> T3
T3 -> T1
```

A cycle indicates a deadlock.

---

## Deadlock Detection Algorithm

The implementation uses Depth First Search (DFS).

Steps:

1. Build Wait-For Graph
2. Traverse graph using DFS
3. Detect cycles
4. Report deadlock if a cycle exists

---

# Part 4 - Transaction Manager

The Transaction Manager coordinates all components.

Responsibilities:

* Begin Transaction
* Read Data
* Write Data
* Commit Transaction
* Abort Transaction
* Manage Locks
* Manage Version Chains
* Detect Deadlocks

---

## Transaction Lifecycle

### Begin

```text
BEGIN TRANSACTION
```

Transaction ID is assigned.

### Read / Write

Transaction accesses records using MVCC and locking mechanisms.

### Commit

```text
COMMIT
```

Locks are released.

### Abort

```text
ABORT
```

Locks are released and transaction terminates.

---

# Project Structure

```text
Lab6/
│
├── README.md
│
├── mvcc/
│   ├── Version.h
│   └── MVCCStore.h
│
├── locking/
│   └── LockManager.h
│
├── deadlock/
│   └── DeadlockDetector.h
│
└── transaction_manager/
    ├── TransactionManager.h
    └── main.cpp
```

---

# Demonstration Scenario

The program demonstrates:

### Transaction Creation

```text
T1
T2
T3
```

### MVCC Writes

```text
T1 writes Alice
T2 writes Bob
T3 writes Charlie
```

### Version Chain Generation

```text
[T3: Charlie] -> [T2: Bob] -> [T1: Alice]
```

### Lock Management

Shared and exclusive lock acquisition.

### Deadlock Detection

```text
T1 -> T2
T2 -> T3
T3 -> T1
```

Deadlock detected through cycle detection.

### Commit Processing

Transactions commit and release locks.

---

# Complexity Analysis

## MVCC Operations

| Operation | Complexity |
| --------- | ---------- |
| Read      | O(1)       |
| Write     | O(1)       |

---

## Lock Management

| Operation     | Complexity |
| ------------- | ---------- |
| Acquire Lock  | O(1)       |
| Release Locks | O(n)       |

---

## Deadlock Detection

| Operation     | Complexity |
| ------------- | ---------- |
| DFS Traversal | O(V + E)   |

Where:

* V = Number of Transactions
* E = Number of Wait Dependencies

---

# Database Concepts Demonstrated

This lab demonstrates several important database concepts:

* Concurrency Control
* Transaction Management
* MVCC
* Version Chains
* Shared Locks
* Exclusive Locks
* Strict 2PL
* Wait-For Graphs
* Deadlock Detection
* Transaction Commit and Abort

---

## Conclusion

This lab implemented a simplified transaction manager in C++ using MVCC version chains, Strict Two-Phase Locking, and deadlock detection.

The system demonstrates how modern database engines manage concurrent transactions, maintain consistency, and prevent concurrency-related issues through version management, locking protocols, and deadlock detection mechanisms.
