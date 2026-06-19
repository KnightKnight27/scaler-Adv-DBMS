# Lab 8 – Transaction Manager with MVCC, Strict 2PL and Deadlock Detection

## Objective

To implement a simplified transaction manager in C++ demonstrating:

- MVCC Version Chains
- Strict Two Phase Locking (2PL)
- Deadlock Detection
- Transaction Lifecycle Management

---

## Concepts Implemented

### MVCC

Multiple versions of data are maintained.
Each write operation creates a new version instead of overwriting existing data.

### Strict 2PL

Transactions acquire locks before accessing data.

Locks are held until commit or abort.

This guarantees serializability.

### Deadlock Detection

A Wait-For Graph is maintained.

Cycles in the graph indicate deadlocks.

---

## Files

- transaction_manager.cpp
- README.md

---

## Compilation

```bash
g++ transaction_manager.cpp -o txn