# Transaction Manager with MVCC + Strict 2PL

## Objective

Implement a transaction manager supporting:

- MVCC Version Chains
- Snapshot Isolation
- Strict Two-Phase Locking (2PL)
- Deadlock Detection using Waits-For Graph

---

## Features

### MVCC

Each update creates a new row version.

Readers access a consistent snapshot without blocking writers.

### Strict 2PL

Transactions acquire:

- Shared (S) Locks
- Exclusive (X) Locks

Locks are released only during Commit/Abort.

### Deadlock Detection

A waits-for graph is maintained.

Cycles are detected using DFS.

One transaction is aborted to resolve the deadlock.

---

## Compilation

```bash
g++ -std=c++17 -pthread txmgr.cpp -o txmgr
```