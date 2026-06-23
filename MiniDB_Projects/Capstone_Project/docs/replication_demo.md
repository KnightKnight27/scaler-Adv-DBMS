# Primary-Replica Replication Demo Guide

This document describes the design, setup, and validation of MiniDB's **Primary-Replica Replication (Log Shipping)** system.

---

## 1. Design & Replication Topology

MiniDB supports Track D (Distributed Systems) using a file-based primary-replica log shipping model:

```
    ┌──────────────────────┐                     ┌──────────────────────┐
    │   PRIMARY DATABASE   │                     │   REPLICA DATABASE   │
    │     (Read-Write)     │                     │     (Read-Only)      │
    └──────────┬───────────┘                     └──────────▲───────────┘
               │                                            │
               │ appends updates                            │ polls and applies
               ▼                                            │
    ┌───────────────────────────────────────────────────────┴───────────┐
    │              minidb_replication.log (Replication Stream)          │
    └───────────────────────────────────────────────────────────────────┘
```

### 1.1 The Primary Node
* Runs in standard read-write mode.
* In addition to writing its local WAL (`minidb.wal`), it ships committed transactions to a shared log file `minidb_replication.log` using the `Primary::shipLog` mechanism defined in [primary.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/replication/primary.cpp).
* The seek offset is tracked sequentially so that each WAL record is written to the replication stream exactly once.

### 1.2 The Replica Node
* Runs in replica mode via `./minidb --replica`.
* Opens a read-only database instance (`minidb_replica.db`).
* Rejects any write transactions (`INSERT`, `DELETE`, `BEGIN`) typed directly into its shell with: `ERROR: Replica is read-only. All writes must go to the Primary.`
* Runs a background thread that polls `minidb_replication.log` every 200 milliseconds, handled in [replica.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/replication/replica.cpp).
* Tracks its sequential position in bytes using `read_offset_`.
* **Transactional Buffering**: Records are buffered locally in `pending_records_` grouped by `TxID` and are only applied to the replica's heap and B+ tree index once a `COMMIT` record for that `TxID` is received.

---

## 2. Walkthrough of the Demo

To run the automated replication demo:
```bash
bash scripts/demo_replication.sh
```

### 2.1 The Execution Trace
1. **Startup**: The script [demo_replication.sh](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/scripts/demo_replication.sh) deletes any old databases and creates named pipes (`pri_pipe` and `rep_pipe`) to control the processes in the background.
2. **Launch Node Processes**:
   - Launches Replica: `./minidb --replica` (background).
   - Launches Primary: `./minidb` (background).
3. **Primary Write operations**:
   - The script creates table `products` on the Primary.
   - Inserts `key=1, Laptop` and `key=2, Phone` in auto-commit mode.
   - Starts an explicit transaction: `BEGIN`, `INSERT key=3, Tablet`, and `COMMIT`.
4. **Replica Polling & Log Application**:
   - The Replica background thread reads from `minidb_replication.log`.
   - Sees the commits for transactions 1, 2, and 3.
   - Applies the inserts to its local `minidb_replica.db` file and updates its B+ Tree indexes.
5. **Replica Selection Query**:
   - The script queries the Replica with `SELECT * FROM products`.
   - The Replica displays all 3 records matching the Primary's data exactly.
6. **Read-Only Enforcement Check**:
   - The script attempts to run `INSERT INTO products VALUES (4, Monitor)` directly on the Replica.
   - The Replica shell blocks it and prints:
     `ERROR: Replica is read-only. All writes must go to the Primary.`
7. **Cleanup**:
   - Stops both processes and deletes the named pipes.
