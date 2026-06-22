# Lab 8: Transaction Manager (MVCC + Strict 2PL + Deadlock Detection)

## 1. Overview
This lab implements a multi-transaction scheduling and execution framework in C++ that demonstrates three core components of database systems:
1. **Multi-Version Concurrency Control (MVCC):** Multiple physical versions of records are stored in a linked list version chain to allow reading historical snapshots without acquiring read blocks.
2. **Strict Two-Phase Locking (Strict 2PL):** Guarantees serializability by acquiring shared/exclusive locks during transaction progress and holding all locks until the transaction completes (commits or aborts).
3. **Deadlock Detection & Resolution:** Builds a directed **Wait-For Graph (WFG)**, runs a cycle detection algorithm using Depth-First Search (DFS), and aborts the youngest transaction to resolve any deadlock cycles.

---

## 2. System Design & Implementation

### 2.1 MVCC Version Chains
Every record in the database holds a reference to a linked list of `Version` structs:
- `xmin`: Transaction ID that created this version.
- `xmax`: Transaction ID that deleted/superseded this version (0 if active).
- `val`: String payload.

#### Visibility Logic:
A reader transaction `TxID` scanning a record's chain finds the newest version that satisfies:
- Created by us (`xmin == TxID`) OR created by a transaction committed before us.
- AND NOT deleted/superseded by us (`xmax == TxID`) and NOT deleted by a transaction committed before us.

### 2.2 Strict 2PL Lock Manager
Locks are managed using a Lock Table containing active holders and a wait queue for each record:
- **Shared (S) Lock:** Allowed if no transaction holds an exclusive lock. Multiple S-locks can share the same record.
- **Exclusive (X) Lock:** Allowed only if no other transaction holds S or X locks.
- **Strict Release:** Locks are never released mid-transaction. All locks are released together at **Commit** or **Abort** time, preventing cascading rollbacks.

### 2.3 Deadlock Detection
When a transaction is blocked waiting for a lock, the engine checks for deadlock cycles:
1. **Graph Construction:** Scans the lock table's wait queue and maps directed edges `TxA -> TxB` indicating that transaction `A` is blocked waiting for a lock held by transaction `B`.
2. **Cycle Detection:** Runs a Depth-First Search (DFS) tracking the recursion stack to locate cycle loops.
3. **Resolution:** Employs the **Youngest Transaction Abortion** policy. The transaction with the highest transaction ID (youngest) in the cycle is selected as the victim and aborted, releasing its locks and waking up waiting processes.

---

## 3. Deadlock Simulation Trace

The program simulates a classic lock contention deadlock:

1. **Transaction Tx3** begins and writes to Record A. (Tx3 holds an **X-Lock on A**).
2. **Transaction Tx4** begins and writes to Record B. (Tx4 holds an **X-Lock on B**).
3. **Tx3 requests a write on Record B.** It is blocked because Tx4 holds the X-lock on B. Tx3 enters the wait queue for B.
4. **Tx4 requests a write on Record A.** It is blocked because Tx3 holds the X-lock on A. Tx4 enters the wait queue for A.
5. The **Deadlock Detector** constructs the Wait-For Graph:
   ```text
   Tx3 -> Tx4
   Tx4 -> Tx3
   ```
6. The detector identifies the cycle `Tx3 -> Tx4 -> Tx3`.
7. **Resolution:** Since `Tx4` is the youngest (ID 4 > ID 3), it is aborted.
8. **Lock Release:** Tx4 releases its X-lock on B, which wakes up Tx3.
9. **Execution Recovery:** Tx3 successfully acquires the X-lock on B, completes its operations, and commits.

---

## 4. Building and Running

### Compilation
Compile using a standard C++ compiler:
```bash
g++ -std=c++17 -Wall main.cpp -o tx_manager
```

### Execution
Run the compiled binary:
```bash
./tx_manager
```
