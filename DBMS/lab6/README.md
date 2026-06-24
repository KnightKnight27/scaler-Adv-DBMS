# Lab 6 – Transaction Processing System using MVCC and Lock-Based Concurrency Control

**Name:** Amitabh Panda
**Roll No:** 24BCS10104

## Aim

The purpose of this experiment was to develop a simplified transaction processing framework in C++ that models important database concurrency mechanisms. The system demonstrates how modern database engines handle simultaneous transactions while preserving data consistency and isolation.

The implementation focuses on:

* Multi-Version Concurrency Control (MVCC)
* Version Management
* Strict Two-Phase Locking (Strict 2PL)
* Wait-For Graph Construction
* Deadlock Detection
* Transaction Execution and Recovery

---

# Introduction

Database systems must support multiple users and transactions operating on the same data concurrently. To ensure correctness, mechanisms are required to coordinate reads, writes, commits, and rollbacks.

This project simulates a lightweight transaction manager capable of:

* Creating and tracking transactions
* Managing record versions
* Coordinating lock acquisition
* Detecting circular waits
* Supporting commit and abort operations

---

# Module 1 – Multi-Version Concurrency Control

## Concept

MVCC allows the database to maintain multiple versions of a record rather than overwriting existing values. Readers can access older versions while writers create new ones, reducing contention between transactions.

Instead of modifying a record directly, every update generates a fresh version.

---

## Version Representation

Each record version stores:

* Transaction identifier
* Data value
* Reference to an earlier version

Example:

```text
Version(T12, Product_C)
        ↓
Version(T8, Product_B)
        ↓
Version(T4, Product_A)
```

---

## Version Chain Storage

Versions are maintained as linked structures where the newest version appears first.

Example operations:

```cpp
store.write(10, 1, "Product_A");
store.write(10, 2, "Product_B");
store.write(10, 3, "Product_C");
```

Resulting chain:

```text
[T3: Product_C]
        ↓
[T2: Product_B]
        ↓
[T1: Product_A]
```

---

## Supported MVCC Operations

### Read Operation

Retrieves the newest visible version available to the transaction.

### Write Operation

Creates a new version node and inserts it at the beginning of the chain.

---

# Module 2 – Strict Two-Phase Locking

## Concept

Strict 2PL is a locking protocol that guarantees serializable execution by requiring transactions to hold acquired locks until completion.

Locks are only released after:

* Commit
* Abort

This prevents dirty reads and several other concurrency anomalies.

---

## Shared Locks

Shared locks are used during read operations.

Multiple transactions may hold shared locks on the same resource simultaneously.

Example:

```text
Transaction 1 reads Account
Transaction 2 reads Account
```

Both accesses are permitted because neither transaction modifies the data.

---

## Exclusive Locks

Exclusive locks are required for updates.

Only one transaction can possess an exclusive lock on a resource at any given time.

Example:

```text
Transaction 5 updates Account
```

Other transactions requesting access must wait until the lock is released.

---

## Lock Manager Functionality

The lock manager is responsible for:

* Recording active locks
* Handling lock requests
* Preventing incompatible access
* Managing lock ownership
* Releasing locks after transaction completion

---

# Module 3 – Deadlock Analysis

## Deadlock Scenario

Deadlocks occur when a group of transactions wait for one another in a cycle.

Example:

```text
T1 waiting for T2
T2 waiting for T3
T3 waiting for T1
```

Since every transaction depends on another, none can continue execution.

---

## Wait-For Graph Model

Transaction dependencies are represented using a directed graph.

Example:

```text
T1 → T2
T2 → T3
T3 → T1
```

A cycle in the graph indicates the existence of a deadlock.

---

## Detection Strategy

The implementation performs cycle detection using Depth First Search (DFS).

Procedure:

1. Build the dependency graph.
2. Traverse each transaction node.
3. Track visited nodes and recursion paths.
4. Identify cycles.
5. Report deadlock if a cycle exists.

---

# Module 4 – Transaction Coordinator

The Transaction Manager integrates MVCC, locking, and deadlock detection into a single control layer.

Primary responsibilities include:

* Transaction initialization
* Data access coordination
* Lock handling
* Commit processing
* Abort handling
* Version maintenance
* Deadlock monitoring

---

## Transaction Flow

### Transaction Start

```text
BEGIN
```

A unique transaction identifier is generated and registered.

---

### Data Access

Transactions perform reads and writes through MVCC structures and lock management services.

---

### Commit

```text
COMMIT
```

Changes become durable and all associated locks are released.

---

### Rollback

```text
ABORT
```

The transaction is terminated and held locks are removed.

---

# Directory Layout

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

# Demonstration

The system was tested using several concurrent transactions.

### Transactions

```text
T1
T2
T3
```

### Data Updates

```text
T1 inserts Product_A
T2 updates Product_B
T3 updates Product_C
```

### Generated Version Chain

```text
[T3: Product_C]
        ↓
[T2: Product_B]
        ↓
[T1: Product_A]
```

### Lock Handling

The program demonstrates acquisition of both shared and exclusive locks.

### Deadlock Example

```text
T1 → T2
T2 → T3
T3 → T1
```

The detector successfully identifies the cycle and reports a deadlock condition.

### Completion

Transactions either commit successfully or abort, after which all locks are released.

---

# Performance Analysis

## MVCC

| Operation | Complexity |
| --------- | ---------- |
| Read      | O(1)       |
| Write     | O(1)       |

---

## Locking Operations

| Operation    | Complexity |
| ------------ | ---------- |
| Lock Request | O(1)       |
| Lock Release | O(n)       |

---

## Deadlock Detection

| Operation       | Complexity |
| --------------- | ---------- |
| Graph Traversal | O(V + E)   |

Where:

* V = Number of active transactions
* E = Number of dependency edges

---

# Concepts Covered

The project demonstrates several foundational DBMS topics:

* Concurrent Transaction Processing
* MVCC Architecture
* Version Chain Management
* Shared and Exclusive Locking
* Strict Two-Phase Locking
* Wait-For Graph Construction
* Deadlock Identification
* Commit and Rollback Processing
* Isolation and Consistency Enforcement

---

# Conclusion

This lab provided practical exposure to transaction management techniques used in modern database systems. By combining MVCC, lock-based concurrency control, and deadlock detection, the implementation demonstrates how databases maintain correctness while supporting concurrent execution.

The project highlights the interaction between version storage, lock coordination, and dependency analysis, all of which are essential components of real-world database engines.
