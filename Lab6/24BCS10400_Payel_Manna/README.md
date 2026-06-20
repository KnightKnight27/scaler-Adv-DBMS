# Lab 6 — Transaction Manager using MVCC and Two-Phase Locking (2PL)

## Student Details

**Name:** Payel Manna
**Roll Number:** 24BCS10400
**Course:** Advanced Database Management Systems
**Lab:** Lab Session 6
**Topic:** Transaction Manager using MVCC and Two-Phase Locking (2PL)

---

# Objective

The objective of this lab is to implement a simplified transaction manager similar to those used in modern database systems.

The implementation combines:

1. **MVCC (Multi-Version Concurrency Control)**
2. **Two-Phase Locking (2PL)**
3. **Transaction Management**
4. **Snapshot Isolation**
5. **Concurrent Read and Write Operations**

The goal is to understand how databases maintain consistency and isolation when multiple transactions execute simultaneously.

---

# Introduction

A database must support multiple users accessing and modifying data concurrently.

Without concurrency control, several problems can occur:

* Dirty Reads
* Lost Updates
* Non-Repeatable Reads
* Phantom Reads
* Inconsistent Data

Modern database systems solve these issues using mechanisms such as:

* MVCC (PostgreSQL)
* Two-Phase Locking (MySQL/InnoDB)
* Serializable Snapshot Isolation

This lab implements a simplified version of these techniques.

---

# Multi-Version Concurrency Control (MVCC)

## What is MVCC?

MVCC stands for **Multi-Version Concurrency Control**.

Instead of overwriting existing data, every update creates a new version of a row.

Readers access a snapshot of data while writers create new versions.

This allows:

* Readers to proceed without blocking writers.
* Writers to proceed without blocking readers.
* Better concurrency.

---

## Row Version Structure

Each row version contains:

```cpp
struct RowVersion {
    std::string value;
    TxID xmin;
    TxID xmax;
};
```

### Fields

| Field | Description                              |
| ----- | ---------------------------------------- |
| value | Actual row value                         |
| xmin  | Transaction that created the version     |
| xmax  | Transaction that invalidated the version |

---

## Example

Initial row:

```text
balance = 1000
xmin = 1
xmax = 0
```

Transaction T2 updates balance:

```text
Old Version:
balance = 1000
xmin = 1
xmax = 2

New Version:
balance = 2000
xmin = 2
xmax = 0
```

Both versions now exist simultaneously.

---

# Snapshot Isolation

## What is a Snapshot?

When a transaction starts, it records a snapshot transaction ID.

```cpp
snapshot_xid
```

The transaction only sees versions that were committed before this snapshot.

---

## Example

Transaction T1:

```text
balance = 1000
```

Transaction T2 begins.

Transaction T3 updates:

```text
balance = 2000
```

T2 still sees:

```text
balance = 1000
```

because its snapshot was taken before T3 committed.

This guarantees a consistent view of data.

---

# Version Visibility Rules

A row version is visible if:

### Rule 1

The creating transaction is committed.

```text
xmin committed
```

### Rule 2

The version was created before the reader's snapshot.

```text
xmin < snapshot_xid
```

### Rule 3

The row was not deleted before the snapshot.

```text
xmax == 0
```

or

```text
xmax > snapshot_xid
```

---

# Two-Phase Locking (2PL)

## What is 2PL?

Two-Phase Locking ensures serializable execution of transactions.

A transaction executes in two phases:

```text
GROWING PHASE
Acquire Locks

SHRINKING PHASE
Release Locks
```

Rules:

* New locks may be acquired only during the growing phase.
* Locks may be released only during the shrinking phase.
* Once shrinking begins, no new locks can be acquired.

---

# Strict Two-Phase Locking

This implementation uses:

```text
Strict 2PL
```

All locks are held until:

* COMMIT
* ABORT

Benefits:

* Prevents cascading rollbacks.
* Guarantees serializability.
* Simplifies recovery.

---

# Lock Types

## Shared Lock (S)

Used for reading.

Multiple transactions may hold shared locks simultaneously.

Example:

```text
T1 -> READ balance
T2 -> READ balance
```

Allowed.

---

## Exclusive Lock (X)

Used for writing.

Only one transaction may hold an exclusive lock.

Example:

```text
T1 -> UPDATE balance
```

Other reads and writes must wait.

---

# Lock Manager

The lock manager maintains:

```cpp
struct LockRequest
```

and

```cpp
struct LockQueue
```

for every row.

Responsibilities:

* Grant locks
* Queue waiting transactions
* Wake blocked transactions
* Enforce 2PL

---

# Transaction Lifecycle

## Begin Transaction

```cpp
TxID begin()
```

Creates:

```text
Transaction ID
Snapshot ID
ACTIVE state
```

---

## Read

```cpp
read()
```

Steps:

1. Acquire Shared Lock
2. Traverse MVCC version chain
3. Return visible version

---

## Insert

```cpp
insert()
```

Steps:

1. Acquire Exclusive Lock
2. Create new version

---

## Update

```cpp
update()
```

Steps:

1. Acquire Exclusive Lock
2. Mark old version deleted
3. Create new version

---

## Delete

```cpp
remove()
```

Steps:

1. Acquire Exclusive Lock
2. Mark current version deleted

---

## Commit

```cpp
commit()
```

Steps:

1. Mark transaction COMMITTED
2. Release all locks

---

## Abort

```cpp
abort()
```

Steps:

1. Undo transaction modifications
2. Mark transaction ABORTED
3. Release locks

---

# Architecture

```text
Application
      |
      v
Transaction Manager
      |
      +------------------+
      |                  |
      v                  v
 Lock Manager        MVCC Heap
      |                  |
      v                  v
 Shared/Exclusive    Version Chains
 Lock Control        Snapshot Reads
```

---

# Scenario 1: MVCC Snapshot Isolation

### Initial State

```text
balance = 1000
```

### Operations

Transaction T2 starts.

Transaction T3 updates:

```text
balance = 2000
```

T2 still reads:

```text
balance = 1000
```

because T2 sees an older snapshot.

---

# Scenario 2: Concurrent Shared Locks

Two transactions perform reads simultaneously.

```text
T4 -> READ
T5 -> READ
```

Both obtain shared locks.

No blocking occurs.

Output:

```text
READ balance = 2000
READ balance = 2000
```

---

# Scenario 3: Exclusive Lock Blocking

Transaction T6 updates:

```text
balance = 3000
```

T7 attempts to read.

Because T6 holds an exclusive lock:

```text
T7 waits
```

After T6 commits:

```text
T7 resumes
```

and reads:

```text
balance = 3000
```

---

# Sample Output

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
[TX 7] waiting for shared lock...
[TX 6] COMMITTED
[TX 7] READ balance = 3000
[TX 7] COMMITTED

All scenarios completed.
```

---

# Time Complexity Analysis

## Begin Transaction

```text
O(1)
```

---

## Insert

```text
O(1)
```

Insert new version at front of version chain.

---

## Read

```text
O(V)
```

where:

```text
V = number of versions
```

May need to traverse version chain.

---

## Update

```text
O(V)
```

Find visible version and create new version.

---

## Delete

```text
O(V)
```

Locate visible version.

---

## Lock Acquisition

```text
O(L)
```

where:

```text
L = number of lock requests
```

---

## Commit

```text
O(number_of_locks_held)
```

Release all locks.

---

# Real Database Connection

This lab models concepts used in real databases.

### PostgreSQL

Uses:

```text
MVCC
Snapshot Isolation
Serializable Snapshot Isolation
```

### MySQL InnoDB

Uses:

```text
MVCC
Record Locks
Gap Locks
2PL
```

### Oracle

Uses:

```text
Undo Segments
MVCC
Read Consistency
```

---

# Learning Outcomes

Through this lab, the following concepts were explored:

* Multi-Version Concurrency Control (MVCC)
* Snapshot Isolation
* Version Chains
* Shared Locks
* Exclusive Locks
* Two-Phase Locking (2PL)
* Strict Two-Phase Locking
* Transaction Management
* Concurrency Control
* Database Internals
* Commit and Abort Processing
* Lock-Based Synchronization

---

# Conclusion

This lab demonstrates how modern database systems manage concurrent transactions safely and efficiently.

MVCC provides non-blocking reads through version chains, while Strict Two-Phase Locking guarantees correctness for concurrent writes.

The combination of these techniques enables databases to maintain consistency, isolation, and high concurrency while processing multiple transactions simultaneously.

The implementation provides a simplified but practical view of the core mechanisms used inside modern relational database systems such as PostgreSQL, MySQL, and Oracle.
