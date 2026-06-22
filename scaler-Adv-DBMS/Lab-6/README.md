# Lab 6: Concurrency Control via MVCC, Strict 2PL, & Deadlock Detection

> **Subject:** Advanced Database Management Systems (ADBMS)  
> **Student Name:** Nandani Kumari  
> **Roll Number:** 24bcs10317  
> **Language Profile:** C++17  

---

## 1. Project Goal

This laboratory project develops a mock database transaction engine using C++17. It manages concurrent transaction execution using three major paradigms:
1. **Multi-Version Concurrency Control (MVCC)**: Allows concurrent execution of read and write workloads. Multiple versions of records are maintained, ensuring that reads access a consistent snapshot without blocking writes.
2. **Strict Two-Phase Locking (Strict 2PL)**: Imposes serializability on writes. Transactions acquire locks during execution and retain them until either a commit or abort operation occurs.
3. **Deadlock Management**: Uses a transaction dependency graph to detect circular waits using Depth-First Search (DFS), aborting the victim transaction.

---

## 2. Directory Layout

| Deliverable | Description |
| :--- | :--- |
| `transaction_manager.cpp` | Integrated transaction scheduler utilizing the `TupleRevision`, `LockSupervisor`, `DependencyRegistry`, and `StorageEngine` components. |
| `CMakeLists.txt` | Build parameters configured for C++17 compilers with `-O3` compilation optimization. |
| `README.md` | This technical document. |

---

## 3. Compilation & Execution

Build and execute the project using CMake:

```bash
# Prepare the build area
mkdir -p build
cd build
cmake ..
make

# Run the test executable
./concurrency_tester
```

---

## 4. Architectural Components

```
                   ┌─────────────────┐
                   │  StorageEngine  │
                   └────────┬────────┘
                            │
         ┌──────────────────┼──────────────────┐
         ▼                  ▼                  ▼
┌────────────────┐ ┌──────────────────┐ ┌──────────────┐
│ LockSupervisor │ │DependencyRegistry│ │ RevisionList │
└────────────────┘ └──────────────────┘ └──────────────┘
```

### 1. MVCC System Structure
Instead of updating values in place, the engine maintains an append-only revision list for database tuples:
```cpp
struct TupleRevision {
    TxnNum createdBy;     // Transaction ID that created this version
    TxnNum deletedBy;     // Transaction ID that deleted this version (0 if active)
    std::string textValue; // The payload data
};

struct RevisionList {
    std::vector<TupleRevision> chain; // Chain of historic revisions
};
```
* **Snapshot Visibility**: For a transaction snapshot version $S$, a revision is visible if:
  $$\text{createdBy} \le S \quad \text{AND} \quad (\text{deletedBy} = 0 \lor \text{deletedBy} > S)$$

### 2. Lock Supervisor (Strict 2PL)
Locks are categorized into shared (`ACCS_SHARED`) and exclusive (`ACCS_EXCLUSIVE`) access models. Rules:
* Multiple transactions can share a read lock on the same key.
* Exclusive write locks are completely isolated.
* To prevent cascading aborts and guarantee serializability, all acquired locks are held until transaction termination.

### 3. Dependency Check & Deadlocks
When a transaction is blocked waiting for a lock, a dependency edge is registered. The `DependencyRegistry` runs a DFS check on every block:
* If a cycle is detected, the transaction that triggered the cycle is chosen as the victim and aborted.

---

## 5. Sample Engine Output

Running `concurrency_tester` outputs:

```text
=== Concurrency Controller with MVCC + Strict 2PL + Deadlock Checker ===

--- Scenario 1: Multi-Version Reads ---

[DATABASE] BEGIN Transaction 1 (Snapshot ID = 1)
[SUPERVISOR] Txn 1 granted EXCLUSIVE lock on client:808
[SET] Txn 1 sets client:808 = 'profile=Nandani'
[SUPERVISOR] Txn 1 released EXCLUSIVE lock on client:808
[COMMIT] Txn 1 committed successfully

[DATABASE] BEGIN Transaction 2 (Snapshot ID = 2)

[DATABASE] BEGIN Transaction 3 (Snapshot ID = 3)
[GET] Txn 2 reads client:808 = 'profile=Nandani'
[SUPERVISOR] Txn 3 granted EXCLUSIVE lock on client:808
[SET] Txn 3 sets client:808 = 'profile=Kumari'
[SUPERVISOR] Txn 3 released EXCLUSIVE lock on client:808
[COMMIT] Txn 3 committed successfully
[GET] Txn 2 reads client:808 = 'profile=Nandani'
[COMMIT] Txn 2 committed successfully

================= CONCURRENCY ENGINE STATE =================
Transaction ID: 1 | Status: COMMITTED | Read Snapshot: 1 | Shrinking: YES
Transaction ID: 2 | Status: COMMITTED | Read Snapshot: 2 | Shrinking: YES
Transaction ID: 3 | Status: COMMITTED | Read Snapshot: 3 | Shrinking: YES

==================== STORAGE SNAPSHOT ====================
client:808 history: [created=1 deleted=3 payload='profile=Nandani'] -> [created=3 deleted=0 payload='profile=Kumari']

--- Scenario 2: Two-Phase Locking Write Lock Conflict ---

[DATABASE] BEGIN Transaction 4 (Snapshot ID = 4)

[DATABASE] BEGIN Transaction 5 (Snapshot ID = 5)
[SUPERVISOR] Txn 4 granted EXCLUSIVE lock on client:909
[SET] Txn 4 sets client:909 = 'profile=Ayush'
[LOCK CONFLICT] TxnE attempts write on 'client:909'
[SUPERVISOR] Txn 4 released EXCLUSIVE lock on client:909
[COMMIT] Txn 4 committed successfully

--- Scenario 3: Reading Committed Values ---

[DATABASE] BEGIN Transaction 6 (Snapshot ID = 6)

[DATABASE] BEGIN Transaction 7 (Snapshot ID = 7)
[SUPERVISOR] Txn 6 granted EXCLUSIVE lock on inventory:abc
[SET] Txn 6 sets inventory:abc = 'count=12'
[SUPERVISOR] Txn 6 released EXCLUSIVE lock on inventory:abc
[COMMIT] Txn 6 committed successfully
[GET] Txn 7 reads inventory:abc = 'count=12'
[COMMIT] Txn 7 committed successfully

================= CONCURRENCY ENGINE STATE =================
Transaction ID: 1 | Status: COMMITTED | Read Snapshot: 1 | Shrinking: YES
Transaction ID: 2 | Status: COMMITTED | Read Snapshot: 2 | Shrinking: YES
Transaction ID: 3 | Status: COMMITTED | Read Snapshot: 3 | Shrinking: YES
Transaction ID: 4 | Status: COMMITTED | Read Snapshot: 4 | Shrinking: YES
Transaction ID: 5 | Status: ACTIVE | Read Snapshot: 5 | Shrinking: NO
Transaction ID: 6 | Status: COMMITTED | Read Snapshot: 6 | Shrinking: YES
Transaction ID: 7 | Status: COMMITTED | Read Snapshot: 7 | Shrinking: YES

==================== STORAGE SNAPSHOT ====================
client:808 history: [created=1 deleted=3 payload='profile=Nandani'] -> [created=3 deleted=0 payload='profile=Kumari']
client:909 history: [created=4 deleted=0 payload='profile=Ayush']
inventory:abc history: [created=6 deleted=0 payload='count=12']

=== All Concurrency Control Runs Completed ===
```

---

## 6. Execution Insights & Implementation Details

1. **Version History Cleanup**: MVCC prevents read-write blocking but consumes storage space as version chains grow. Production engines require a background garbage collection worker to prune stale records.
2. **Lock Overhead**: Tight locking policies ensure serialized execution, but hot rows can cause transactions to stall or fail under high contention.
3. **Deadlock Recovery Overhead**: Detecting cycles dynamically with DFS ensures correct database execution, but choosing transaction aborts to break loops introduces operational overhead.
