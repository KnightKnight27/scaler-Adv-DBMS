# Lab 7 — Transaction Manager using MVCC, Strict 2PL and Deadlock Detection

## Overview

This project implements a simplified database transaction manager in C++. The system combines three important database concurrency mechanisms:

* Multi-Version Concurrency Control (MVCC)
* Strict Two-Phase Locking (2PL)
* Deadlock Detection using a Waits-For Graph

The goal is to allow concurrent transaction execution while preserving consistency, isolation, and correctness.

---

## System Components

### 1. Transaction Registry

The transaction registry maintains metadata for every active transaction.

Each transaction stores:

* Transaction ID (XID)
* Snapshot ID
* Current state (ACTIVE, COMMITTED, ABORTED)
* Shrinking phase status

Responsibilities:

* Generate unique transaction IDs
* Track transaction state transitions
* Provide snapshot information for MVCC visibility checks

---

### 2. MVCC Storage Engine

The storage layer maintains multiple versions of each row.

Each version contains:

| Field          | Purpose                                  |
| -------------- | ---------------------------------------- |
| value          | Stored row data                          |
| created_by_xid | Transaction that created the version     |
| deleted_by_xid | Transaction that invalidated the version |

Instead of overwriting existing data:

* INSERT creates a new version
* UPDATE creates a new version and marks the old version obsolete
* DELETE marks the current version as deleted

Readers evaluate version visibility using their transaction snapshot.

This allows readers to access historical versions without interfering with concurrent writers.

---

### 3. Lock Manager

The lock manager enforces Strict Two-Phase Locking.

Supported lock types:

* Shared Lock (Read)
* Exclusive Lock (Write)

Behavior:

* Multiple transactions may hold shared locks simultaneously.
* Only one transaction may hold an exclusive lock.
* Exclusive locks block both readers and writers.
* Locks are released only at commit or abort.

This guarantees serializable execution for conflicting operations.

---

### 4. Deadlock Detection

The system maintains a waits-for graph.

If:

* Transaction A waits for B
* Transaction B waits for A

a cycle is formed.

The lock manager performs DFS-based cycle detection whenever a transaction becomes blocked.

When a cycle is detected:

* The requesting transaction is aborted
* Locks are released
* Waiting transactions continue execution

---

## Transaction Workflow

```text
Begin Transaction
        |
        v
Acquire Required Locks
        |
        v
Perform Read / Write
        |
        v
MVCC Visibility Check
        |
        v
Commit or Abort
        |
        v
Release All Locks
```

---

## Build Instructions

```bash
g++ -std=c++17 -pthread -o txmgr txmgr.cpp
./txmgr
```

---

## Demonstrated Scenarios

### Scenario 1 – Snapshot Isolation

A transaction reads an older committed version even after another transaction updates the same record.

Concept Verified:

* MVCC snapshot visibility
* Read consistency

---

### Scenario 2 – Concurrent Readers

Two transactions acquire shared locks on the same row simultaneously.

Concept Verified:

* Shared lock compatibility
* Concurrent reads

---

### Scenario 3 – Writer Blocking Reader

A transaction holds an exclusive lock while another transaction attempts to read the same data.

Concept Verified:

* Exclusive lock enforcement
* Lock waiting behavior

---

### Scenario 4 – Deadlock Handling

Two transactions attempt to acquire resources in opposite order.

Concept Verified:

* Waits-for graph creation
* Cycle detection
* Transaction abort and recovery

---

## Architecture

```text
+----------------------+
| Transaction Manager  |
+----------+-----------+
           |
    +------+------+
    |             |
    v             v

+---------+   +-----------+
| MVCC    |   | Lock      |
| Storage |   | Manager   |
+----+----+   +-----+-----+
     |                |
     v                v

Version Chains   Waits-For Graph
                     |
                     v
              Deadlock Detector
```

---

## Key Learnings

* MVCC enables non-blocking reads through version chains.
* Strict 2PL guarantees correctness for concurrent writes.
* Deadlock detection prevents transactions from waiting indefinitely.
* Combining MVCC and locking mechanisms provides both consistency and concurrency.
* Modern database systems use similar techniques to handle large numbers of concurrent transactions efficiently.
