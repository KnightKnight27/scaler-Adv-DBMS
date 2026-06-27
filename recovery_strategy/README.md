# Crash Recovery Strategy: The ARIES Algorithm

To guarantee the **Atomicity** and **Durability** properties of transactions (ACID), a database management system (DBMS) must be able to recover from unexpected failures (such as OS crashes, power loss, or process termination). When the system restarts after a crash, it must restore the database to a consistent state where all committed transactions are fully reflected, and all uncommitted transactions are completely rolled back.

The industry-standard recovery framework is **ARIES** (Algorithms for Recovery and Isolation Exploiting Semantics), developed by C. Mohan at IBM.

---

## 1. System Assumptions and Buffer Pool Policies

The recovery strategy is tightly bound to how the Buffer Pool Manager interacts with disk storage. There are two primary dimensions of buffer pool policy:

| Policy | Option | Description | Recovery Implication |
| :--- | :--- | :--- | :--- |
| **Steal Policy** | **STEAL** | The buffer pool can write uncommitted dirty pages to disk to free up frames. | Requires **Undo** logging to roll back changes on crash. |
| | **NO-STEAL** | Uncommitted pages can never be written to disk before commit. | No Undo logging required (but restricts database size to physical memory). |
| **Force Policy** | **FORCE** | The system forces all modified pages to disk before committing a transaction. | No **Redo** logging required (extremely slow random I/O at commit time). |
| | **NO-FORCE** | A transaction commits as soon as its log records are flushed to disk; page writes are lazy. | Requires **Redo** logging to restore changes not yet persistent on disk. |

To achieve maximum throughput, modern databases (e.g., PostgreSQL, MySQL, SQLite) implement a **STEAL / NO-FORCE** policy. Under this policy, crash recovery is highly complex, requiring both **Undo** and **Redo** capabilities.

---

## 2. In-Memory and Log Structures

### 2.1 The Write-Ahead Log (WAL)
The core rule of recovery is the **Write-Ahead Logging (WAL) protocol**:
> Before any database page is written to disk, the log records describing the modification to that page must be flushed to non-volatile storage.

Each log record is identified by a monotonically increasing **Log Sequence Number (LSN)**.

#### Structure of a Log Record:
- **LSN**: Unique identifier of the log record.
- **PrevLSN**: The LSN of the previous log record written by this transaction. This forms a backward-linked list of all actions in a transaction.
- **TxID**: The transaction identifier.
- **Type**: The action type (`BEGIN`, `COMMIT`, `ABORT`, `UPDATE`, `CLR`).
- **PageID**: The ID of the database page modified (only for `UPDATE` or `CLR`).
- **Undo/Redo Data**: The byte changes (before-image for undo, after-image for redo).

```
  Log Record on Disk:
  +---------+-------------+--------+--------+---------+-------------------+
  | LSN: 10 | PrevLSN: 0  | TxID: 1| UPDATE | Page: 5 | [Before/After Data]
  +---------+-------------+--------+--------+---------+-------------------+
```

### 2.2 In-Memory Tables Managed During Runtime
During normal database operations, the engine maintains two data structures in volatile memory:

1. **Transaction Table (TT)**: Tracks all active transactions.
   - Keys: `TxID`
   - Fields: `LastLSN` (most recent LSN written by this Tx), `Status` (`Running`, `Committing`, `Aborting`).

2. **Dirty Page Table (DPT)**: Tracks pages in the buffer pool that have modifications not yet written to disk.
   - Keys: `PageID`
   - Fields: `RecLSN` (Recovery LSN: the LSN of the log record that *first* dirtied the page in memory since it was last flushed).

### 2.3 Page-Level Tracking
Every database page contains a header field called `pageLSN`.
- Whenever a transaction modifies a page, the `pageLSN` of that page in memory is updated to the LSN of the corresponding log record.
- This allows the recovery engine to determine if a logged change is already present on the physical disk page.

---

## 3. The ARIES Recovery Protocol

After a crash, the database starts in an uninitialized state. The recovery process reads the log sequentially in three distinct phases: **Analysis**, **Redo**, and **Undo**.

```
  LOG FILE:
  [Checkpoint] -------------------> [End of Log]
        |                                 |
        +======== PHASE 1: ANALYSIS ======>
        
  DPT Min RecLSN -----------------------------------------> [End of Log]
        |                                                       |
        +===================== PHASE 2: REDO ===================>
        
                                                   [Active Loser Transactions]
                                                                |
        <===================== PHASE 3: UNDO ===================+
```

### Phase 1: Analysis Phase
The goal of the Analysis phase is to scan the log *forward* starting from the last active checkpoint to reconstruct the state of the database at the exact moment of the crash.

#### Steps:
1. Locate the master record to find the LSN of the last checkpoint. Read the checkpoint record, which contains the Transaction Table (TT) and Dirty Page Table (DPT) at checkpoint time.
2. Scan the log forward from the checkpoint.
3. If an `UPDATE` record is found:
   - Add the transaction to the TT if not present, and update `LastLSN = LSN`.
   - Add the page to the DPT if not present, setting `RecLSN = LSN`.
4. If a `COMMIT` or `ABORT` record is found:
   - Change the status of the transaction in the TT to `Committing` or `Aborting`.
   - If a `TX_END` record is found, remove the transaction from the TT.
5. At the end of the Analysis phase:
   - The DPT identifies which pages were dirty in memory at crash time.
   - The TT identifies which transactions were active at the crash. These are called **Loser Transactions** because they never committed.

---

### Phase 2: Redo Phase ("Repeating History")
The goal of the Redo phase is to reconstruct the exact database state at the time of the crash. ARIES repeats history—it redoes all operations, including those of loser transactions that will be rolled back in the next phase.

#### Steps:
1. Find the smallest `RecLSN` in the Dirty Page Table reconstructed during Analysis. This is the starting point for the scan.
2. Scan the log *forward* from the smallest `RecLSN` to the end of the log.
3. For every `UPDATE` or `CLR` (Compensating Log Record) record:
   - Decide if the change needs to be reapplied. We skip Redo *only* if:
     - The page is not in the DPT.
     - The page is in the DPT, but the record's `LSN` is less than the page's `RecLSN` in the DPT.
     - The page is read from disk, and its physical `pageLSN` is greater than or equal to the log record's `LSN` (proving the write reached disk before the crash).
   - If none of the conditions above match, **reapply the update** to the page data in memory and set the page's `pageLSN = LSN`.
4. If a transaction writes a `COMMIT` record during this scan, write a `TX_END` record and remove it from the TT.

---

### Phase 3: Undo Phase
The goal of the Undo phase is to roll back the actions of all loser transactions identified in the Analysis phase.

#### Steps:
1. Initialize a set of LSNs called `ToUndo` containing the `LastLSN` of all transactions in the TT with status `Running` or `Aborting`.
2. Scan the log *backward*, choosing the largest LSN in the `ToUndo` set.
3. If the record is an `UPDATE`:
   - Revert the modification on the page (write the before-image back to the page).
   - Write a **Compensating Log Record (CLR)** to the log. The CLR is a redo-only record that logs the fact that we rolled back this change.
   - Set the CLR's `UndoNextLSN` field to point to the `PrevLSN` of the record we just undid.
   - Replace the undone LSN in the `ToUndo` set with the `PrevLSN` of the record.
4. If the record is a `CLR`:
   - Do not undo a CLR. Instead, read its `UndoNextLSN` and add that LSN to the `ToUndo` set. This ensures we never undo a change twice, making recovery fully **idempotent**.
5. If the `PrevLSN` of an undone record is `0` (indicating the start of the transaction):
   - Write a `TX_END` record to the log.
   - Remove the transaction from the `ToUndo` set.
6. Repeat until the `ToUndo` set is empty.

```
  Example of CLR and UndoNextLSN chain:
  
  LSN 10: Tx1 updates Page A  (PrevLSN = 0)
  LSN 20: Tx1 updates Page B  (PrevLSN = 10)
  ------- CRASH -------
  Recovery Undo Phase:
  1. Read LSN 20 (LastLSN). Undo change to Page B.
  2. Write CLR (LSN 30): Undone Page B, UndoNextLSN = 10.
  3. Read LSN 10 (pointed by UndoNextLSN). Undo change to Page A.
  4. Write CLR (LSN 40): Undone Page A, UndoNextLSN = 0.
  5. Write TX_END for Tx1.
```

---

## 4. Fuzzy Checkpointing

Checkpointing is the process of writing state information to disk so that the recovery process does not have to scan the entire log from the beginning of time. 

If a database stops all transactions to write a checkpoint ("stop-the-world"), latency spikes occur. ARIES uses **Fuzzy Checkpointing**, which records the internal state of the database *without* halting active transactions or forcing dirty pages to disk.

### Execution Steps:
1. Write a `CHECKPOINT_BEGIN` record to the log.
2. In-memory tables (DPT and TT) are copied to a buffer. Transactions continue to execute and modify pages.
3. Write a `CHECKPOINT_END` record to the log containing the snapshot of the DPT and TT.
4. Write the LSN of the `CHECKPOINT_BEGIN` record to a known, safe block on disk called the **Master Record**.

During recovery, the Analysis phase starts at the `CHECKPOINT_BEGIN` record corresponding to the latest checkpoint. Any changes that occurred during the checkpoint creation are recovered because the log scan goes forward from the checkpoint, updating the DPT and TT with any changes that occurred after the checkpoint began.
