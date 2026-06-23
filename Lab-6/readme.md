# Advanced DBMS Lab 6: Transaction Manager

> **Author:** Om Malviya (24BCS10448)
> **Course:** Advanced Database Management Systems
> **Language:** C++17

This project features an in-memory database **Transaction Manager** demonstrating core database concurrency control and recovery principles from scratch.

## Core Concepts Implemented

1. **Strict Two-Phase Locking (Strict 2PL)**
   - Transactions acquire Shared (`S`) locks for reading and Exclusive (`X`) locks for writing.
   - All locks are held until the transaction either commits or aborts.
   - Ensures conflict serializability and prevents cascading aborts.

2. **Multi-Version Concurrency Control (MVCC)**
   - Row modifications do not overwrite in place. Instead, new versions are prepended to a version chain.
   - When a transaction aborts, the versions it created are discarded, effectively rolling back the database state.

3. **Deadlock Detection (Wait-for Graph)**
   - A central `LockManager` manages all lock requests.
   - When a lock cannot be granted, the system dynamically updates a **waits-for graph** (directed edges from requesting transaction to blocking transactions).
   - Before a transaction is put to sleep, a Cycle Detection algorithm (Depth First Search) traverses the graph.
   - If a cycle is detected, the requesting transaction immediately aborts to break the deadlock and allows the others to proceed.

## Architecture Diagram

```
+------------------+        +--------------------+
|   Transaction 1  |        |   Transaction 2    |
|  write(row 100)  |        |  write(row 200)    |
|  write(row 200)  |        |  write(row 100)    |
+--------+---------+        +----------+---------+
         |                             |
         v                             v
+-------------------------------------------+
|             LockManager                   |
|  lock_table: { row -> [LockRequest...] }  |
|  waits_for_graph: { txn -> {txns} }       |
|  has_deadlock() -> DFS cycle detection    |
+-------------------------------------------+
         |
         v
+-------------------------------------------+
|              Database                     |
|  rows: { row_id -> Version* }             |
|  Version: { txn_id, data, *next }         |
|  write() -> prepend new version (MVCC)    |
|  abort()  -> remove versions for txn      |
|  commit() -> release locks (Strict 2PL)   |
+-------------------------------------------+
```

## Compilation & Execution

```bash
g++ -std=c++17 -pthread txn_manager.cpp -o txn_manager
./txn_manager
```

## Example Run (Deadlock Resolution)
The driver code spins up two concurrent transactions:
- Txn 1 writes to Row 100, then sleeps, then writes to Row 200.
- Txn 2 writes to Row 200, then sleeps, then writes to Row 100.

This intentionally triggers a classic deadlock. The engine successfully catches it, aborts one transaction, and commits the other.

```text
--- Starting Deadlock Simulation ---
Txn 1: Start
Txn 2: Start
Txn 2: Aborted due to deadlock
Txn 1: Committed
--- Final Database State ---
Row 100: A=10
Row 200: B=20
```

## Key Learnings

- **Strict 2PL** guarantees serializability but can cause deadlocks when transactions acquire locks in different orders.
- **MVCC version chains** allow readers to see a consistent snapshot without blocking writers, as long as the correct version is chosen by snapshot timestamp.
- **Waits-for graph DFS** is the standard lightweight deadlock detection approach: O(V+E) per check, practical for small-to-medium transaction counts.
- The real PostgreSQL and MySQL InnoDB both implement variations of these exact mechanisms: 2PL for locking, MVCC for snapshot reads, and periodic or triggered deadlock detection.
