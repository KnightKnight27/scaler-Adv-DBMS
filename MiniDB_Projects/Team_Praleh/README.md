# MiniDB — Advanced DBMS Capstone Project

## Team Information

**Team Name:** `Team_Praleh`

| Name | Roll Number | Email |
|---|---|---|
| Abhay Singh Bhadauria | 24bcs10102 | abhay.24bcs10102@sst.scaler.com |
| Aditya Sharma| 24BCS10176 | aditya.24bcs10176@sst.scaler.com  |
---

## 1. Project Overview

MiniDB is a relational database engine built from scratch in **C++17** for the Advanced DBMS Capstone. The project objective is to integrate all foundational database components into a functional system while illustrating the internals and engineering trade-offs of relational databases.

**Chosen Extension Track:** **Track D — Distributed Systems (Primary-Replica Replication)**

---

## 2. System Architecture

The architecture of MiniDB is fully decoupled into modular subsystems:

```
┌──────────────────────────────────────────────────────────┐
│                  REPL (src/main.cpp)                     │
└────────────────────────┬─────────────────────────────────┘
                         │
               ┌──────────▼──────────┐
               │  Primary Node       │ ← replication/primary.h
               └──┬──────────────────┘
                  │
      ┌───────────┼──────────────────────────────┐
      │           │                              │
   ┌──▼──┐  ┌────▼────┐   ┌────────────┐  ┌────▼─────┐
   │Parse│  │Optimizer│   │  Executor  │  │TxManager │
   └─────┘  └────┬────┘   └──┬──────┬─┘  └────┬─────┘
                 │            │      │          │
                 │      ┌─────▼─┐  ┌─▼──────┐  │
                 │      │Heap   │  │BPlus   │  │
                 │      │File   │  │Tree    │  │
                 │      └──┬────┘  └────────┘  │
                 │    ┌────▼──────┐             │
                 │    │BufferPool │             │
                 │    └────┬──────┘             │
                 │  ┌──────▼──────┐             │
                 │  │DiskManager  │→minidb.db   │
                 │  └─────────────┘             │
                 │                              │
                 │  ┌────────────────────┐      │
                 │  │WAL (recovery/wal)  │←─────┘
                 │  └────────┬───────────┘
                 │           │ tails log
                 │  ┌────────▼───────────┐
                 │  │Replica Node        │→minidb_replica.db
                 └──│(replication/replica)│
                    └────────────────────┘
```

### Data Flow
1. **Query Entry**: The user inputs a command via the CLI REPL.
2. **Parsing**: [Parser](file:///run/media/abhay/e/DBMS_Project_miniDB/src/query/parser.cpp) tokenizes the input query into a `Statement` abstract struct.
3. **Optimization**: [Optimizer](file:///run/media/abhay/e/DBMS_Project_miniDB/src/query/optimizer.cpp) calculates selectivity and plan costs to select between Table Scan, Index Scan, Nested Loop Join, or Index Nested Loop Join.
4. **Execution**: [Executor](file:///run/media/abhay/e/DBMS_Project_miniDB/src/query/executor.cpp) coordinates with [LockManager](file:///run/media/abhay/e/DBMS_Project_miniDB/src/transaction/lock_manager.cpp) to acquire locks under Strict 2PL and then accesses the database records.
5. **Storage Access**: Record operations are evaluated through the `HeapFile` and cached in the `BufferPool` before the `DiskManager` performs byte-offset file I/O.
6. **Logging & Recovery**: Modifications write recovery records to the `WAL`. On startup, `Recovery` replays the WAL.
7. **Replication**: The background `Replica` thread continuously monitors the WAL, replaying committed transactions onto its local standby database.

---

## 3. Storage Layer

* **Page (`storage/page.h`)**: The basic unit of data storage. It represents a 4096-byte in-memory frame tracking page-level metadata like `page_id` and dirty states.
* **DiskManager (`storage/disk_manager.h/cpp`)**: Handles low-level direct file I/O mapping page IDs to direct byte offsets (`offset = page_id * PAGE_SIZE`).
* **BufferPool (`storage/buffer_pool.h/cpp`)**: Keeps up to 64 active pages in RAM. Uses a standard LRU (Least Recently Used) replacement policy implemented via a doubly-linked list (`std::list`) and a hash map lookup for $O(1)$ updates.
* **HeapFile (`storage/heap_file.h/cpp`)**: Represents a collection of linked database pages. Records are stored as fixed-size slotted rows (12 bytes). Deletions are logical (using a tombstone bit flag inside each record).

### Page Layout
```
[0..3]   next_page_id (int32)
[4..7]   num_records  (int32)
[8..]    Record[0], Record[1], ... (each 12 bytes: id, value, tombstone)
```

---

## 4. Indexing

* **BPlusTree (`index/bplus_tree.h/cpp`)**: A self-balancing search tree structure configured with a fanout degree of `ORDER = 4`.
  * **Leaf Nodes**: Hold the mapping of key IDs to disk page locations and form a linked list to execute fast range searches.
  * **Internal Nodes**: Hold router keys to direct search traversals.
  * **Operations**:
    * `Search`: Performs $O(\log N)$ traversal from the root to the target leaf node.
    * `Insert`: Recursively navigates nodes, performing key insertion and splitting nodes (and potentially updating the root) upon overflow.
    * `Delete`: Implements lazy deletion where records are deleted from nodes without complex structure merging for system simplicity.

---

## 5. Query Execution

* **Parser (`query/parser.h/cpp`)**: Parses queries including `INSERT`, `SELECT`, `DELETE`, and `SHOW`.
* **Executor (`query/executor.h/cpp`)**: Translates AST statements into specific execution pipelines:
  * **Point Index Scan**: Directly look up a single ID using the B+ Tree index.
  * **Table Scan**: Reads through the entire Heap File, evaluating filter predicates row by row.
  * **Nested Loop Join**: Scans the outer table and performs a full scan of the inner table to find matches.
  * **Index Nested Loop Join**: Scans the outer table and queries the B+ Tree index of the inner table to find matches, minimizing scanning overhead.

---

## 6. Cost-Based Optimizer

The [Optimizer](file:///run/media/abhay/e/DBMS_Project_miniDB/src/query/optimizer.cpp) makes cost-based execution plan choices:

### Cost & Selectivity Estimation
1. **Point Queries**: Selectivity is estimated at exactly $\frac{1}{N}$ where $N$ is the total record count. Point queries on primary key IDs always select `POINT_INDEX_SCAN`.
2. **Range Queries** (`ID > X` or `VALUE > Y`): Selectivity is estimated via min/max value interpolation:
   $$\text{Selectivity} = \frac{\text{MaxVal} - X}{\text{MaxVal} - \text{MinVal}}$$
   If `ID > X` has a selectivity $< 0.4$ (less than $40\%$ of records matched), it selects an `INDEX_SCAN_FILTER` utilizing the B+ Tree. Otherwise, it falls back to a sequential `TABLE_SCAN` to optimize disk read throughput.
3. **Joins**: Compares the cost of a classic Nested Loop Join against an Index-Nested Loop Join:
   $$\text{Cost}_{\text{NLJ}} = \text{Pages}_{\text{outer}} + (\text{Records}_{\text{outer}} \times \text{Pages}_{\text{inner}})$$
   $$\text{Cost}_{\text{INLJ}} = \text{Pages}_{\text{outer}} + (\text{Records}_{\text{outer}} \times \text{IndexLookupCost})$$
   It automatically selects the cheaper join configuration.

---

## 7. Transactions & Concurrency

* **LockManager (`transaction/lock_manager.h/cpp`)**: Implements Strict Two-Phase Locking (2PL) ensuring Serializable isolation.
  * **Shared (S) Locks**: Acquired for read operations, allowing concurrent readers.
  * **Exclusive (X) Locks**: Acquired for write operations, ensuring mutual exclusion.
  * Blocks execution using `std::condition_variable` rather than CPU-bound busy waiting.
* **TxManager (`transaction/tx_manager.h/cpp`)**: Controls the transaction lifecycle (`BEGIN`, `COMMIT`, `ABORT`). On `COMMIT`, transaction logs are flushed to disk. On `ABORT`, uncommitted writes are rolled back using the log data.
* **Deadlock Prevention**: Lock attempts block with a timeout threshold (800ms). If the lock cannot be acquired within this window, the transaction aborts and releases all active locks, breaking potential deadlock cycles.

---

## 8. Recovery

* **Write-Ahead Logging (WAL) (`recovery/wal.h/cpp`)**: Writes stable, binary-formatted 20-byte records to disk for all modification operations before updating the database page.
* **Recovery Engine (`recovery/recovery.h/cpp`)**: Runs on system startup to perform crash recovery:
  1. **Analysis Pass**: Scans the WAL file to identify all committed transactions.
  2. **REDO Pass**: Replays both `INSERT` and `DELETE` log operations for all committed transactions in Log Sequence Number (LSN) order. Uncommitted transactions are ignored (effectively aborted).

---

## 9. Extension Track D — Replication

MiniDB chooses Track D, implementing a robust **Primary-Replica Log-Shipping Architecture**:
* **Primary Node**: Handles all SQL updates, writes WAL logs, and commits transactions.
* **Replica Node**: Runs as an independent background thread, tailing the active `minidb.wal` file at $300\text{ ms}$ intervals.
* **Consistency**: The replica only applies WAL records corresponding to committed transactions, keeping the replica database (`minidb_replica.db`) eventually consistent without blocking the primary node.

---

## 10. Benchmarks

### Experimental Setup
* **OS**: Linux
* **Compiler**: GCC with C++17 support enabled
* **Build Configuration**: Release optimization (`-O3`)

### Results & Metrics
* **Write Throughput (`bench_insert`)**:
  * **Operations**: 10,000 inserts.
  * **Throughput**: ~7,247 inserts/second.
  * **Average Latency**: ~0.138 ms/op.
* **Read Throughput (`bench_select`)**:
  * **Operations**: 5,000 lookups.
  * **Index Scan vs. Table Scan Speedup**: point B+ Tree lookups achieve constant-time $O(\log N)$ performance compared to scanning the linear heap files.

---

## 11. Limitations

* **B+ Tree Node Merging**: Nodes are not merged during delete operations, meaning the B+ Tree can become sparse under heavy deletions.
* **Log-Shipping Polling Lag**: The replication thread polls the WAL log every $300\text{ ms}$ which can result in minor replication lag.
* **SQL Grammar Limits**: The parser supports single-table queries and simple self-joins; it does not parse complex arbitrary multi-table joins or nested subqueries.
* **Network Replication**: Currently runs in a single process using multiple database files; a production system would distribute data via TCP/IP sockets.

---

## 12. How to Build and Run

### Requirements
* GCC $\ge 9$ or Clang $\ge 10$
* CMake $\ge 3.14$

### Build Steps
```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Running the Interactive REPL
```bash
./build/minidb
```
* Type `HELP` for list of SQL commands.
* Type `DEMO` to run the automated DBMS test suite.
* Type `CRASH` to simulate a system crash, then run `RECOVER` to restore data.
* Type `REPLICA` to view replica synchronization.
* Type `QUIT` to exit.

### Running Crash Recovery Script
```bash
./scripts/demo_crash_recovery.sh
```

### Running Benchmarks
```bash
./benchmarks/run_benchmarks.sh
```
