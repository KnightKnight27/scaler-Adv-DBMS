# System Design: Designing a Crash Recovery Strategy (ARIES)

**Name:** Shah Musharaf ul Islam
**College ID:** 24bcs10447
**OS:** Arch Linux | **Shell:** zsh

---

## 1. Problem Background

### Why Write-Ahead Logging (WAL) Exists
In database systems, we want transactions to be durable (the 'D' in ACID). But writing modified data pages to disk immediately on every commit is incredibly slow because of random disk I/O. 

To solve this, databases use a Write-Ahead Log (WAL). Instead of writing the actual heavy data pages, we append a small, sequential log record of the change to disk first. As long as the log is safe on persistent storage, we can defer flushing the dirty data pages to disk. If the system crashes, we can recover the database state by replaying this log.

### Steal and No-Force Policies
- **Steal Policy:** The buffer manager is allowed to write uncommitted pages to disk to free up buffer frames. This means the disk might contain uncommitted ("dirty") data.
- **No-Force Policy:** The database does not force dirty pages to be flushed to disk at commit time. This means committed changes might only exist in the volatile buffer pool.

Because we use **Steal** (disk has uncommitted changes) and **No-Force** (disk lacks committed changes), we need a recovery algorithm that can:
1. Redo the changes of committed transactions that were not yet flushed to disk.
2. Undo the changes of uncommitted transactions that were written to disk.

---

## 2. ARIES Recovery Architecture

ARIES (Algorithms for Recovery and Isolation Exploiting Semantics) is the standard crash recovery protocol. It operates in three distinct phases:

```
[Crash Occurs]
      │
      ▼
┌──────────────┐
│  Analysis    │  <-- Scan log forward from the last checkpoint
└──────┬───────┘
       │ Rebuilds Txn Table & Dirty Page Table
       ▼
┌──────────────┐
│    Redo      │  <-- Replay history forward to restore state
└──────┬───────┘
       │
       ▼
┌──────────────┐
│    Undo      │  <-- Roll back active (loser) transactions
└──────┬───────┘
       │
       ▼
[System Recovered]
```

### 1. Analysis Phase
- **What it does:** Scans the log forward starting from the last checkpoint.
- **Goal:** Identify all active transactions (losers) and dirty pages in memory at the time of the crash.
- **Output:** Reconstructed Transaction Table (TT) and Dirty Page Table (DPT).

### 2. Redo Phase
- **What it does:** Scans the log forward starting from the earliest `recLSN` (Recovery LSN) in the Dirty Page Table.
- **Goal:** Reapply all logged changes (both committed and uncommitted) to restore the database to the exact state it was in right before the crash. This is called "repeating history."
- **Output:** The database state is restored, including the changes of loser transactions.

### 3. Undo Phase
- **What it does:** Scans the log backward from the crash point.
- **Goal:** Roll back the changes made by all transactions that were active/loser at the time of the crash.
- **CLR (Compensation Log Records):** For every update rolled back, we write a CLR to the log to prevent repeating the rollback if the system crashes again during recovery.

---

## 3. Concrete Scenario Walkthrough

Let's trace a concrete recovery scenario using the log records.

### The Log Records on Disk:
1. `LSN 101 | Txn 1 | BEGIN`
2. `LSN 102 | Txn 2 | BEGIN`
3. `LSN 103 | Txn 1 | UPDATE | Page A | Old: "v0" | New: "v1"`
4. `LSN 104 | Txn 1 | COMMIT`
5. `LSN 105 | Txn 2 | UPDATE | Page B | Old: "x0" | New: "x1"`
6. `LSN 106 | CHECKPOINT (Active Txns: Txn 2 [lastLSN=105] | Dirty Pages: Page B [recLSN=105])`
7. `LSN 107 | Txn 3 | BEGIN`
8. `LSN 108 | Txn 3 | UPDATE | Page C | Old: "y0" | New: "y1"`
9. `LSN 109 | Txn 2 | UPDATE | Page A | Old: "v1" | New: "v2"`
10. `[CRASH]`

### Recovery Execution:

#### 1. Analysis Phase:
- Starts from the checkpoint at LSN 106.
- We initialize the Transaction Table: `[Txn 2, lastLSN=105]` and Dirty Page Table: `[Page B, recLSN=105]`.
- Scan forward:
  - **LSN 107 (Txn 3 BEGIN):** Add Txn 3 to Transaction Table (`lastLSN=107`).
  - **LSN 108 (Txn 3 UPDATE):** Update Txn 3 `lastLSN=108`. Since Page C is not in DPT, add it: `[Page C, recLSN=108]`.
  - **LSN 109 (Txn 2 UPDATE):** Update Txn 2 `lastLSN=109`. Since Page A is not in DPT, add it: `[Page A, recLSN=109]`.
- **End of Analysis:**
  - **Loser Transactions:** Txn 2 (lastLSN=109), Txn 3 (lastLSN=108).
  - **Dirty Page Table:** Page B (recLSN=105), Page C (recLSN=108), Page A (recLSN=109).

#### 2. Redo Phase:
- The smallest `recLSN` in our DPT is **105** (Page B).
- We scan forward from LSN 105:
  - **LSN 105 (Txn 2 UPDATE Page B):** Reapply "x1" to Page B.
  - **LSN 108 (Txn 3 UPDATE Page C):** Reapply "y1" to Page C.
  - **LSN 109 (Txn 2 UPDATE Page A):** Reapply "v2" to Page A.

#### 3. Undo Phase:
- We need to undo active loser transactions: **Txn 2** and **Txn 3**.
- We undo their operations in reverse chronological order (largest LSN first):
  - **Undo LSN 109 (Txn 2 UPDATE Page A):** 
    - Write CLR record: `LSN 110 | CLR | Page A | Undone LSN 109 | Restored: "v1"`.
    - Restore Page A to "v1".
  - **Undo LSN 108 (Txn 3 UPDATE Page C):**
    - Write CLR record: `LSN 111 | CLR | Page C | Undone LSN 108 | Restored: "y0"`.
    - Restore Page C to "y0".
  - **Undo LSN 105 (Txn 2 UPDATE Page B):**
    - Write CLR record: `LSN 112 | CLR | Page B | Undone LSN 105 | Restored: "x0"`.
    - Restore Page B to "x0".
  - Write `END` log records for Txn 2 and Txn 3.

---

## 4. Key Learnings & Takeaways

- **Repeating History:** Replaying all changes (including uncommitted ones) during the Redo phase simplifies recovery because it restores the system to a known state. We don't have to guess what was on disk.
- **CLRs prevent infinite loops:** Compensation Log Records (CLRs) track what has already been undone. If the system crashes *during* recovery, we won't try to undo an already undone change, avoiding log growth and inconsistency.
- **Checkpointing is crucial for scaling:** Checkpoints limit how far back in the log we have to scan during recovery. Without them, database startup times after a crash would grow indefinitely.
