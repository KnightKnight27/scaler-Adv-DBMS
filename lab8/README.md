# Lab 8: Transaction Manager with MVCC, Strict 2PL, & Deadlock Detection

**Student:** Lokendra Singh Rajawat — 23bcs10075  
**Subject:** Advanced Database Management Systems

---

## Overview

This lab implements a multi-threaded C++ simulation of a transactional database storage engine featuring:
1.  **Multi-Version Concurrency Control (MVCC)**: Maintaining a version chain (`xmin` and `xmax` timestamps) for each database record to support snapshot isolation. Reads are completely lock-free and do not block writers.
2.  **Strict 2-Phase Locking (Lock Manager)**: Restricting concurrent updates to the same record by requiring write transactions to acquire exclusive (X) locks on keys. Locks are held until transaction commit or abort (Strict 2PL).
3.  **Deadlock Detection via Wait-For Graph (WFG)**: Resolving dependency cycles by building a directed graph of blocked transactions, executing Depth-First Search (DFS) to detect cycles, and aborting the cycle-completing transaction (victim rollback).

---

## System Design & Architecture

```
       [Client Transaction Threads]
         │                    │
         ▼ (Read)             ▼ (Write)
    [MVCC Reader]       [Lock Manager (Strict 2PL)]
         │                    │
         │ (Snapshot Read)    ├─► Blocked? ──► [Wait-For Graph]
         │                    │                      │
         ▼                    ▼ (Yes: Lock Assigned) ▼ (Cycle Detected)
   [Version Chain]     [DB Engine Write]       [Victim Abort / Rollback]
```

### 1. MVCC Version Chains & Visibility
Every database record holds a linked list of `RecordVersion` nodes. Each node contains:
*   `value`: The data payload.
*   `xmin`: The ID of the transaction that inserted the version.
*   `xmax`: The ID of the transaction that superseded/deleted it (0 if active).
*   `prev`: Pointer to the previous (older) version.

When a transaction starts, it captures a snapshot containing a set of active transaction IDs. A version is visible to reader transaction $T$ if its `xmin` has committed before $T$ started, and its `xmax` is either uncommitted or committed after $T$ started.

### 2. Lock Manager & Strict 2PL
*   **Exclusive Locks**: Writes acquire an exclusive write lock on the key. If held by another active transaction, the requesting thread blocks on a condition variable.
*   **Strict 2PL**: To guarantee transaction serializability and prevent cascading aborts, all locks are held by the transaction until its execution concludes with a commit or abort.

### 3. Deadlock Detection & Victim Rollback
*   When a thread blocks, it maps the lock dependency into a Wait-For Graph (WFG) where directed edges $T_i \to T_j$ denote that $T_i$ is waiting for a lock held by $T_j$.
*   The system performs DFS cycle detection on the WFG. If a cycle is formed, the requester transaction is selected as the victim, and is aborted immediately.
*   **Rollback**: Aborting scans database chains, prunes version nodes created by the aborted transaction (`xmin == tx_id`), and resets the `xmax` deletions to `0` for older versions. It then releases all locks, waking up remaining blocked transactions.

---

## Compilation & Run Instructions

To compile and run the simulator:

```bash
# Navigate to lab8 directory
cd lab8

# Compile the source files
make

# Run the concurrent test scenarios
make run
```

---

## Verification & Test Scenarios

The simulator executes three concurrent scenarios:

### Test 1: MVCC Snapshot Read Isolation
*   Key 1 is initialized to `"Original"`.
*   Transaction 1 starts and reads Key 1 (`"Original"`).
*   Transaction 2 starts, overwrites Key 1 to `"Updated by Tx 2"`, and commits.
*   Transaction 1 reads Key 1 again. Because Transaction 2 committed after Transaction 1 started, Transaction 1's snapshot isolates it, and it reads the version `"Original"`.
*   A newly started Transaction 3 successfully reads `"Updated by Tx 2"`.

### Test 2: Strict 2PL Write Serialization
*   Transaction A writes to Key 2 and sleeps for 500ms while holding the lock.
*   Transaction B attempts to write to Key 2. It blocks on the condition variable.
*   When Transaction A commits, it releases its locks, waking up Transaction B. Transaction B then writes its update and commits.
*   The final check confirms the value is serialized to Transaction B's write.

### Test 3: Deadlock Detection & Rollback
*   Transaction A locks Key 10.
*   Transaction B locks Key 20.
*   Transaction A attempts to write Key 20 (blocks waiting for Transaction B).
*   Transaction B attempts to write Key 10. This creates a cycle: $Tx\ A \to Tx\ B \to Tx\ A$.
*   The deadlock detector runs DFS on the WFG, detects the cycle, and aborts Transaction B (requester).
*   Transaction B's updates are rolled back, and its locks are released.
*   Transaction A wakes up, successfully updates Key 20, and commits.
*   Verification checks prove Key 10 has Transaction A's update, and Key 20 has reverted Transaction B's change to write Transaction A's change.

---

## Key Design Insights & Implementation Trade-Offs

1.  **Requester Abort Strategy**: When a cycle is detected, aborting the transaction that just requested the lock is highly efficient. It avoids the need to asynchronously interrupt other threads or coordinate lock transfers mid-wait, resolving the deadlock on the spot.
2.  **Granular Lock Mutexes**: To prevent lock manager operations from blocking the database reader, thread sync is split: read visibility uses the database mutex (`db_mtx`), while waiting threads block on lock manager mutexes (`lk_mtx`) and transaction-specific condition variables (`cv_table`).
3.  **Torn Page & Cascading Aborts Prevention**: Strict 2PL guarantees that no other transaction reads uncommitted writes. If a transaction aborts, rollback is isolated and does not affect other active transactions.
