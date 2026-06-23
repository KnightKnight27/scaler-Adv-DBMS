# Lab 8 - Transaction Manager

## Student Details
- Name: Harshit Tiwari
- Roll Number: 24BCS10277

## Aim
Implement a small transaction manager that demonstrates MVCC version chains, strict two-phase locking for writes, and deadlock detection using a wait-for graph.

## Files
- `transaction_lab.cpp`: Complete C++17 demo implementation.

## Compile And Run

```powershell
g++ -std=c++17 lab8/transaction_lab.cpp -o lab8/transaction_lab
.\lab8\transaction_lab.exe
```

## Features Implemented
- Transaction begin, commit, and abort.
- MVCC read visibility using transaction start timestamps.
- Version chain per record.
- Exclusive write locks held until commit or abort.
- Wait-for graph construction when a transaction is blocked.
- Cycle detection to identify deadlocks.
- Demonstration of snapshot reads and a two-transaction deadlock.

## Design Notes
Reads use the transaction start timestamp to find the newest committed version visible to that transaction. Writes are buffered in the transaction's private write set until commit. Write locks are strict, so they are released only when the transaction commits or aborts.

Deadlock detection is handled by recording blocker relationships in a wait-for graph. If adding a wait edge creates a cycle, the waiting transaction is aborted as the victim.
