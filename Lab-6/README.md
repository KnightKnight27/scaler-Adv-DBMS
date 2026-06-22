# Lab 6: Concurrency Control Engine with MVCC, Strict 2PL, and Deadlock Detection

> **Course:** Advanced Database Management Systems (ADBMS)  
> **Author:** Ayush Kumar Patra (24bcs10474)  
> **Programming Language:** C++17  

---

## 1. Executive Summary

This lab implements a transaction concurrency control engine written in C++17. It integrates three major database execution architectures:
1. **Multi-Version Concurrency Control (MVCC)**: Keeps old versions of records visible to older snapshots while appending new versions, ensuring readers do not block writers and writers do not block readers.
2. **Strict Two-Phase Locking (Strict 2PL)**: Locks are grown continuously during execution and only released altogether during the commit or abort phases, guaranteeing serializability and avoiding cascading aborts.
3. **Deadlock Detection**: Models transaction dependencies in a Waits-For graph and detects cycles via Depth-First Search (DFS) to resolve deadlocks.

---

## 2. Directory Layout & Artifacts

| Component | Responsibility |
| :--- | :--- |
| `transaction_manager.cpp` | Core implementation containing the `RecordVersion`, `LockRegistry`, `DeadlockGraph`, and `TxnCoordinator` classes. |
| `CMakeLists.txt` | Build configuration to compile the multi-threaded code with standard optimization flags (`-O2`). |
| `README.md` | This technical analysis document. |

---

## 3. Build & Run Instructions

To compile and run the engine, use the following commands:

```bash
# Build the binary using CMake
mkdir -p build
cd build
cmake ..
make

# Run the executable
./txn_manager
```

---

## 4. Class Diagrams & Architectural Design

### Concurrency Components

```
                     ┌───────────────────┐
                     │  TxnCoordinator   │
                     └─────────┬─────────┘
                               │
         ┌─────────────────────┼─────────────────────┐
         ▼                     ▼                     ▼
┌─────────────────┐   ┌─────────────────┐   ┌──────────────────┐
│  LockRegistry   │   │  DeadlockGraph  │   │  VersionHistory  │
└─────────────────┘   └─────────────────┘   └──────────────────┘
```

### 1. Record Versioning & MVCC Storage
Instead of overwriting rows in-place, the database stores a chain of versions for each key:
```cpp
struct RecordVersion {
    TransactionID creator_tx;    // ID of the transaction that inserted this version
    TransactionID deleter_tx;    // ID of the transaction that deleted/superseded this version (0 if active)
    std::string data_payload;    // The actual values stored in the database
};

struct VersionHistory {
    std::vector<RecordVersion> list; // Ordered list of historical versions
};
```

* **Visibility Rules**: A version is visible to a transaction with snapshot ID $S$ if:
  $$\text{creator\_tx} \le S \quad \text{AND} \quad (\text{deleter\_tx} = 0 \lor \text{deleter\_tx} > S)$$

### 2. Lock Registry (Strict 2PL)
Locks are divided into `SHARED` (read) and `EXCLUSIVE` (write) modes:
* Multiple transactions can concurrently hold `SHARED` locks on a row.
* A row can only have one `EXCLUSIVE` lock holder at a time.
* All locks are retained until the transaction fully commits or rolls back (`Strict 2PL` constraint).

### 3. Deadlock Graph
Monitors transaction blockages. An edge from $T_A \to T_B$ indicates transaction $T_A$ is blocked waiting for a lock held by $T_B$. A background check (or checked on blockage) uses DFS to identify cycles:
* If a cycle is detected, one of the transactions (e.g., the victim) is aborted to resolve the circular dependency.

---

## 5. Console Output & Test Cases

The driver runs three core tests to showcase the invariants:

```text
=== Transaction Manager with MVCC + Strict 2PL + Deadlock Detection ===

--- Scenario 1: Basic MVCC Visibility ---

[ENGINE] BEGIN Txn 1 (Snapshot = 1)
[REGISTRY] Txn 1 acquired EXCLUSIVE lock on user:101
[STORE] Txn 1 updates user:101 = 'name=Ayush'
[REGISTRY] Txn 1 released EXCLUSIVE lock on user:101
[COMMIT] Txn 1 committed successfully

[ENGINE] BEGIN Txn 2 (Snapshot = 2)

[ENGINE] BEGIN Txn 3 (Snapshot = 3)
[FETCH] Txn 2 reads user:101 = 'name=Ayush'
[REGISTRY] Txn 3 acquired EXCLUSIVE lock on user:101
[STORE] Txn 3 updates user:101 = 'name=Patra'
[REGISTRY] Txn 3 released EXCLUSIVE lock on user:101
[COMMIT] Txn 3 committed successfully
[FETCH] Txn 2 reads user:101 = 'name=Ayush'
[COMMIT] Txn 2 committed successfully

================= TXN COORDINATOR STATE =================
Txn #1 | State: COMMITTED | Snap ID: 1 | Shrinking: YES
Txn #2 | State: COMMITTED | Snap ID: 2 | Shrinking: YES
Txn #3 | State: COMMITTED | Snap ID: 3 | Shrinking: YES

=================== DATABASE STORAGE ===================
user:101 versions: [creator=1 deleter=3 val='name=Ayush'] -> [creator=3 deleter=0 val='name=Patra']

--- Scenario 2: Strict 2PL Write Contention ---

[ENGINE] BEGIN Txn 4 (Snapshot = 4)

[ENGINE] BEGIN Txn 5 (Snapshot = 5)
[REGISTRY] Txn 4 acquired EXCLUSIVE lock on user:102
[STORE] Txn 4 updates user:102 = 'name=Nandani'
[CONFLICT] TxnE attempts to update the locked row 'user:102'
[REGISTRY] Txn 4 released EXCLUSIVE lock on user:102
[COMMIT] Txn 4 committed successfully

--- Scenario 3: Isolated Snapshots ---

[ENGINE] BEGIN Txn 6 (Snapshot = 6)

[ENGINE] BEGIN Txn 7 (Snapshot = 7)
[REGISTRY] Txn 6 acquired EXCLUSIVE lock on catalog:1
[STORE] Txn 6 updates catalog:1 = 'items=5'
[REGISTRY] Txn 6 released EXCLUSIVE lock on catalog:1
[COMMIT] Txn 6 committed successfully
[FETCH] Txn 7 reads catalog:1 = 'items=5'
[COMMIT] Txn 7 committed successfully

================= TXN COORDINATOR STATE =================
Txn #1 | State: COMMITTED | Snap ID: 1 | Shrinking: YES
Txn #2 | State: COMMITTED | Snap ID: 2 | Shrinking: YES
Txn #3 | State: COMMITTED | Snap ID: 3 | Shrinking: YES
Txn #4 | State: COMMITTED | Snap ID: 4 | Shrinking: YES
Txn #5 | State: ACTIVE | Snap ID: 5 | Shrinking: NO
Txn #6 | State: COMMITTED | Snap ID: 6 | Shrinking: YES
Txn #7 | State: COMMITTED | Snap ID: 7 | Shrinking: YES

=================== DATABASE STORAGE ===================
catalog:1 versions: [creator=6 deleter=0 val='items=5']
user:101 versions: [creator=1 deleter=3 val='name=Ayush'] -> [creator=3 deleter=0 val='name=Patra']
user:102 versions: [creator=4 deleter=0 val='name=Nandani']

=== Concurrency Control Execution Completed ===
```

---

## 6. Key Learnings & Engineering Trade-offs

1. **MVCC Overhead vs. Performance**: Storing version chains eliminates read-write bottlenecks but increases storage costs. Database engines must employ garbage collectors (such as `VACUUM` in PostgreSQL) to clean up dead versions.
2. **Strict 2PL Correctness**: Strict locking protocols prevent anomalies like dirty reads or write skew, though they increase lock contention and latency on hot rows.
3. **Deadlock Recovery**: DFS-based deadlock detection avoids permanent system lockups, but aborting transactions incurs rollback and retry overheads.
