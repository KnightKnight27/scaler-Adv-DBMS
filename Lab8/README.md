# Lab 8: Transaction Manager with MVCC and 2PL

## Overview

This project implements a simplified transaction manager supporting:

* Multi-Version Concurrency Control (MVCC)
* Strict Two-Phase Locking (2PL)
* Shared and Exclusive Locks
* Deadlock Detection using a Waits-For Graph
* Transaction Commit and Abort

The implementation demonstrates how modern database systems manage concurrent transactions while maintaining consistency.

## Components

### Transaction Manager

Responsible for:

* Beginning transactions
* Reading rows using MVCC visibility rules
* Insert, Update, and Delete operations
* Commit and Abort processing

### MVCC

Each row is stored as a version chain.

Each version contains:

* `xmin` : creating transaction ID
* `xmax` : deleting/updating transaction ID

Transactions read the version visible to their snapshot.

### Lock Manager

Supports:

* Shared (S) locks for reads
* Exclusive (X) locks for writes

Strict 2PL is enforced by holding locks until commit or abort.

### Deadlock Detection

A waits-for graph is maintained for blocked transactions.

When a transaction waits for another transaction:

* An edge is added to the graph
* DFS cycle detection is performed
* If a cycle exists, one transaction is aborted

## Demonstration Scenarios

1. MVCC Snapshot Isolation
2. Concurrent Shared Locks
3. Exclusive Lock Blocking a Reader
4. Deadlock Detection and Recovery

## Build Instructions

```bash
mkdir build
cd build

cmake ..
make

./transaction_demo
```

## Files

* `main.cpp` : Demonstration program
* `transaction_manager.h` : Data structures and class declarations
* `transaction_manager.cpp` : Transaction manager implementation
* `CMakeLists.txt` : Build configuration
