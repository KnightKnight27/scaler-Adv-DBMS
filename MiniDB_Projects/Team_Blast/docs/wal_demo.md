# WAL & Crash Recovery Demo Guide

This document describes the design, execution, and verification of MiniDB's **Write-Ahead Logging (WAL)** and **ARIES-style Redo-only Crash Recovery** mechanism.

---

## 1. Design & Core Concepts

### 1.1 The Write-Ahead Log (WAL) Rule
MiniDB strictly follows the Write-Ahead Log rule:
> All log records representing a database update must be written and flushed (fsynced) to the WAL file on disk *before* the corresponding database pages in the buffer pool are allowed to be modified or written to the database file on disk.

In MiniDB, this is implemented in `Executor::doInsert` and `Executor::doDelete`:
1. `wal_.logInsert(...)` (appends record to `minidb.wal` in memory/file buffer).
2. `heap->insertRecord(...)` (modifies slotted page memory).
3. On `COMMIT`, `wal_.logCommit(...)` is written and `fsync` is called on the WAL file.

### 1.2 WAL Record Layout
Each log entry in `minidb.wal` is a fixed 309-byte binary structure:
```
┌──────────┬──────────┬──────────┬──────────────┬──────────┬────────────────┐
│ LSN (8B) │ TxID (8B)│ Type (1B)│ Table (32B)  │ Key (4B) │ Value (128B)   │
└──────────┴──────────┴──────────┴──────────────┴──────────┴────────────────┘
```
* **LSN (Log Sequence Number)**: Monotonically increasing unique sequence number.
* **TxID (Transaction ID)**: ID of the transaction that wrote this log.
* **Type**: `BEGIN` (0), `INSERT` (1), `DELETE` (2), `COMMIT` (3), `ABORT` (4).
* **Table/Key/Value**: The table name, primary key, and string value of the change.

### 1.3 Redo-Only Recovery Algorithm
MiniDB implements a fast, robust **Redo-only** recovery algorithm. Because MiniDB uses **Strict Two-Phase Locking (Strict 2PL)**, no uncommitted writes are ever visible to other transactions, and transactions hold write locks until commit/abort. This means we do not need to perform an undo pass for recovery on startup (no cascading aborts, no dirty reads).

On startup:
1. **Pass 1 (Analysis Phase)**: Scan the WAL from beginning to end. Build a set of all committed Transaction IDs.
2. **Pass 2 (Redo Phase)**: Scan the WAL again. For every `INSERT` or `DELETE` record:
   - If its `TxID` is in the committed transactions set, replay the write (insert into HeapFile and update B+ Tree index).
   - If its `TxID` is NOT in the committed set (meaning the transaction was uncommitted/aborted when the system crashed), skip it.

---

## 2. Walkthrough of the Demo

To run the automated recovery demo:
```bash
./scripts/demo_crash_recovery.sh
```

### 2.1 The Execution Trace
1. **Startup**: The script deletes any old `minidb.db` and `minidb.wal` files.
2. **Committed Inserts**:
   - Starts MiniDB.
   - Inserts row `key=1, value=Alice` (committed).
   - Inserts row `key=2, value=Bob` (committed).
3. **Multi-Statement Transaction**:
   - Executes `BEGIN`.
   - Inserts row `key=3, value=Carol`.
   - Executes `COMMIT`.
4. **Uncommitted/Crashed Transaction**:
   - Executes `BEGIN`.
   - Inserts row `key=4, value=Dave` (writes `BEGIN` and `INSERT` to WAL, but does NOT write `COMMIT`).
5. **Hard Crash**:
   - The script sends `SIGKILL` (`kill -9`) to the running process, preventing any clean shutdown or buffer flushes.
6. **Recovery**:
   - The script restarts MiniDB.
   - MiniDB sees `minidb.db` and triggers crash recovery.
   - Analysis Phase finds 3 committed transactions (TX 1, TX 2, TX 3).
   - Redo Phase replays `Alice`, `Bob`, and `Carol`, inserting them back into the tables and rebuilding their B+ Tree indexes.
   - `Dave` (TX 4) is skipped because it lacked a `COMMIT` record.
7. **Verification**:
   - The restarted instance runs `SELECT * FROM users` showing Alice, Bob, and Carol are present. Dave is missing.
