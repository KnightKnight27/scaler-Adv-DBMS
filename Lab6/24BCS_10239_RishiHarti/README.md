# Lab 6: Transaction Manager with MVCC + Strict 2PL + Deadlock Detection

**Student Name:** Rishi Harti  
**Roll Number:** 24BCS10239  
**Lab Session:** Lab 6  
**Course:** Advanced Database Management Systems (DBMS)

---

## 1. Concurrency Control Architecture Overview

This transaction engine prototypes a hybrid concurrency control manager combining **Multi-Version Concurrency Control (MVCC)** and **Strict Two-Phase Locking (Strict 2PL)**. It is reinforced by a directed **Waits-For Graph (WFG)** deadlock cycle detector.

```
       [ Multi-Version Concurrency Control ]       [ Strict Two-Phase Locking ]
                     (MVCC)                                (Strict 2PL)
                       │                                         │
                       ▼                                         ▼
            Non-blocking Reads (SI)                   Write-Write Serialization
          (Traverses Version Chains)                  (Exclusive Locks on Rows)
                       │                                         │
                       └───────────────────┬─────────────────────┘
                                           │
                                           ▼
                            [ Locks Release at Commit/Abort ]
                                           │
                                           ▼
                            [ Waits-For Graph Cycle Monitor ]
                                (Resolves Deadlocks)
```

### Architectural Responsibilities
1. **Reads (MVCC):** Reads are non-blocking. Transactions read a consistent snapshot of the database using **Snapshot Isolation (SI)** by traversing version chains. S-locks are bypassed for reads to optimize throughput.
2. **Writes (Strict 2PL):** Writes acquire an **Exclusive Lock (X-Lock)** on the target row before writing. To prevent cascading rollbacks (dirty reads), all locks must be held for the entire duration of the transaction and are released only at `COMMIT` or `ABORT` time.
3. **Deadlocks (WFG Detector):** When write-write blocking occurs, a dependency is added to a Waits-For Graph. A cycle detector monitors this graph and resolves deadlocks by aborting the requesting transaction.

---

## 2. Multi-Version Concurrency Control (MVCC) Visibility Rules

Each database record maintains a linked list of historical versions (newest to oldest):

```
Row [101] -> [Head Version] -> [Older Version] -> [Initial Version]
               txid_created: 2    txid_created: 1    txid_created: 0
               txid_expired: 0    txid_expired: 2    txid_expired: 1
```

### 2.1 Visible Version Resolution Logic
For a transaction `T_read` reading Row `R`, it traverses the version chain starting from `head` and selects the first version $V$ where the following logical conditions evaluate to **True**:

$$\text{Visible}(V) = \left( \text{txid\_created}_V = T_{\text{read}} \ \lor \ \text{IsCommitted}(\text{txid\_created}_V) \right)$$

$$\land \ \left( \text{txid\_created}_V \le T_{\text{read}} \right)$$

$$\land \ \left( \text{txid\_expired}_V = 0 \ \lor \ \text{txid\_expired}_V > T_{\text{read}} \ \lor \ \neg \text{IsCommitted}(\text{txid\_expired}_V) \right)$$

This formula ensures that `T_read` sees the latest committed version that was created before it started, and ignores any uncommitted modifications or future updates.

---

## 3. Strict Two-Phase Locking (Strict 2PL)

Strict 2PL prevents conflicting concurrent writes.

```
  Lock State     [Un-Locked] ──────> Acquire Lock ──────> [X-Locked]
                      ▲                                        │
                      │                                        ▼
                      └─────────── Commit / Abort ─────────────┘
                               (Release all locks)
```

1. **Growing Phase:** A transaction can acquire locks but cannot release any.
2. **Shrinking Phase:** All acquired locks are released simultaneously during transaction completion (`COMMIT` or `ABORT`). 
3. **Cascading Abort Prevention:** Releasing locks only at transaction end ensures that uncommitted data remains hidden from other writing transactions, preventing dirty writes and cascading rollbacks.

---

## 4. Deadlock Detection & Waits-For Graph (WFG)

When a transaction blocks waiting for a lock, it registers a dependency in the Waits-For Graph.

### 4.1 Deadlock Cycle Illustration
If Transaction `Tx 4` holds a lock on Row A and requests Row B (held by `Tx 5`), and `Tx 5` requests Row A:

```
                      Row 101 (Row A)           Row 102 (Row B)
                       [ Held by ]               [ Held by ]
                            │                         │
                            ▼                         ▼
                         [ Tx 4 ] ─── Waits For ──> [ Tx 5 ]
                            ▲                         │
                            │                         │
                            └─────── Waits For ───────┘
                               (Deadlock Cycle!)
```

### 4.2 Detection & Resolution Walkthrough
1. **Tx 4** locks Row 101, **Tx 5** locks Row 102.
2. **Tx 4** requests lock on Row 102 $\implies$ Blocks waiting for **Tx 5**. Directed edge `Tx 4 -> Tx 5` is added to the graph.
3. **Tx 5** requests lock on Row 101 $\implies$ Blocks waiting for **Tx 4**. Directed edge `Tx 5 -> Tx 4` is registered.
4. **Deadlock Check:** The cycle monitor runs DFS on the graph and detects the cycle:
   $$\text{Tx 5} \longrightarrow \text{Tx 4} \longrightarrow \text{Tx 5}$$
5. **Resolution:** To break the cycle, the manager aborts **Tx 5** (the active requester). It cleans up **Tx 5**'s waiting queue entries and releases its held locks, allowing **Tx 4** to acquire Row 102 and finish successfully.

---

## 5. Compilation and Verification

The accompanying `main.cpp` executes two multithreaded simulations to verify correctness.

### 5.1 Compilation Command
Compile the code with C++17 support:

```bash
# Compile using g++
g++ -std=c++17 main.cpp -o transaction_manager

# Execute the simulation
./transaction_manager
```

### 5.2 Verification Log Outputs
```
==========================================================
    LAB 6: MVCC ENGINE + STRICT 2PL + DEADLOCK DETECTION  
    Roll No: 24BCS10239 | Name: Rishi Harti
==========================================================

--- SCENARIO 1: MVCC SNAPSHOT ISOLATION & WRITE-WRITE LOCK BLOCKING ---
Version chain for Row [101]: ("Initial_A", CreatedBy: Tx 0, ExpiredBy: None)
[Tx 1] Started.
[LockMgr] Tx 1 successfully acquired exclusive lock on Row 101
[Tx 1] Wrote Row 101: "A_Version_1_Tx1"
[Tx 2] Started.
[Tx 2] MVCC Isolation Read Row 101: "Initial_A" (Should see committed "Initial_A")
[Tx 2] Attempting to acquire lock on Row 101 to update...
[LockMgr] Tx 2 blocked on Row 101 (held by Tx 1). Registering Waits-For dependency...
[Tx 1] Committed.
[LockMgr] Releasing all locks held by Tx 1 (Strict 2PL)...
[LockMgr] Lock on Row 101 passed from Tx 1 to Tx 2. Waking up Tx 2
[Tx 2] Woke up. Wrote Row 101: "A_Version_2_Tx2"
[Tx 2] Committed.
[LockMgr] Releasing all locks held by Tx 2 (Strict 2PL)...

[+] State of row version chains post Scenario 1:
Version chain for Row [101]: ("A_Version_2_Tx2", CreatedBy: Tx 2, ExpiredBy: None) -> ("A_Version_1_Tx1", CreatedBy: Tx 1, ExpiredBy: Tx 2) -> ("Initial_A", CreatedBy: Tx 0, ExpiredBy: Tx 1)
[System Read] Row 101 visible version: "A_Version_2_Tx2"

--- SCENARIO 2: DEADLOCK CYCLE DETECTION AND RESOLUTION ---
[Tx 4] Started.
[LockMgr] Tx 4 successfully acquired exclusive lock on Row 101
[Tx 5] Started.
[LockMgr] Tx 5 successfully acquired exclusive lock on Row 102
[Tx 4] Requesting Row 102...
[LockMgr] Tx 4 blocked on Row 102 (held by Tx 5). Registering Waits-For dependency...
[Tx 5] Requesting Row 101 (creating deadlock loop)...
[LockMgr] Tx 5 blocked on Row 101 (held by Tx 4). Registering Waits-For dependency...
[DEADLOCK DETECTED] Waits-For cycle detected. Proactively aborting requesting Tx 5 to break the cycle.
[Tx 5] Aborted by deadlock resolver.
[LockMgr] Releasing all locks held by Tx 5 (Strict 2PL)...
[LockMgr] Lock on Row 102 passed from Tx 5 to Tx 4. Waking up Tx 4
[Tx 4] Committed.
[LockMgr] Releasing all locks held by Tx 4 (Strict 2PL)...

[+] All simulations finished successfully! Transaction Manager is robust.
```
