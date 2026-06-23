# MiniDB — Lightweight Relational DBMS Engine

**Team CodeKnights** · Advanced Database Management Systems · Scaler School of Technology

---

## 👥 Team & Responsibilities

| Contributor | Email | Roll Number |
|:---|:---|:---|
| **Smit Rupani** | smit.24bcs10256@sst.scaler.com | 24bcs10256 |
| **Lakshya Mewara** | tanush.24bcs10265@sst.scaler.com | 24bcs10265 |
| **Tanush Shoor** | Lakshya.24bcs10290@sst.scaler.com | 24bcs10290 |
| **Ojas Maheshwari** | ojas.24bcs10227@sst.scaler.com | 24bcs10227 |

---

## 🛠️ Individual Contributions

### Smit Rupani (Storage & Memory Subsystem)
* **Assigned Modules**: [disk_manager.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/storage/disk_manager.cpp), [page.h](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/storage/page.h), and [buffer_pool.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/storage/buffer_pool.cpp)
* **Key Achievements**:
  * Designed a binary slotted-page structure supporting variable-length records, page headers, and slot updates.
  * Implemented low-level file I/O operations inside `DiskManager` using POSIX system calls.
  * Programmed a Clock-Sweep Buffer Pool Manager (64 frames) coordinating thread-safe page pinning/unpinning and dirty page flushing.

### Lakshya Mewara (Indexing & Query Parsing)
* **Assigned Modules**: [bplus_tree.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/index/bplus_tree.cpp) and [parser.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/query/parser.cpp)
* **Key Achievements**:
  * Constructed a primary-key B+ Tree index (Order = 4) supporting node splits, root updates, and page routing.
  * Connected leaf nodes into a double-linked list for $O(\log N + K)$ range queries.
  * Authored a whitespace-tokenizing SQL parser mapping command strings to structured plan inputs.

### Tanush Shoor (Query Execution & Cost-Based Optimizer)
* **Assigned Modules**: [executor.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/query/executor.cpp) and [optimizer.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/query/optimizer.cpp)
* **Key Achievements**:
  * Built Volcano-style iterator execution operators (`TableScanOp`, `IndexScanOp`, `NestedLoopJoinOp`, `ProjectionOp`).
  * Structured Disk I/O cost formulas comparing heap-file page scans against B+ Tree index scans.
  * Defined selectivity cost metrics ($S = 1/N$ for point lookups, $S = 0.5$ for range queries) and optimized join ordering logic.

### Ojas Maheshwari (Concurrency Control, Logging & Replication)
* **Assigned Modules**: [lock_manager.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/transaction/lock_manager.cpp), [tx_manager.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/transaction/tx_manager.cpp), [wal.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/recovery/wal.cpp), [recovery.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/recovery/recovery.cpp), [primary.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/replication/primary.cpp), and [replica.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/replication/replica.cpp)
* **Key Achievements**:
  * Enforced Strict Two-Phase Locking (Strict 2PL) for transactional serializability.
  * Designed DFS-based deadlock detection using waits-for dependency graphs.
  * Developed a Write-Ahead Logging (WAL) engine with ARIES Redo-only crash recovery.
  * Engineered a Primary-Replica replication log-shipping model over POSIX pipes.

---

## 1. Project Overview

MiniDB is a lightweight relational DBMS engine built from scratch in C++17. It aims to demystify DBMS internals by implementing the entire database stack—from raw POSIX file writes to transaction scheduling and log-based replication.

### Core Objectives
* **Slotted-Page Storage**: Store variable-length records on fixed-size 4 KB pages.
* **Clock-Sweep Caching**: Maintain a thread-safe 64-page memory buffer.
* **B+ Tree Indexing**: Support primary-key indexing for fast data retrieval.
* **Volcano Execution Engine**: Process database queries using on-demand tuple streams.
* **Strict 2PL Concurrency**: Prevent concurrency anomalies using transaction lock schedules.
* **Durability & Recovery**: Secure data writes using WAL logging and Redo-only recovery.
* **Log-Shipping Replication**: Replicate transactions to a read-only replica node.

---

## 2. Architecture & Data Flow

MiniDB employs a decoupled, modular design to coordinate query execution and transactional storage:

```
                    ┌─────────────────────────────────┐
                    │          REPL / main.cpp         │
                    └────────────┬────────────────────┘
                                 │ SQL command string
                    ┌────────────▼────────────────────┐
                    │         Parser (parser.h)        │
                    └────────────┬────────────────────┘
                                 │ QueryPlan struct
                    ┌────────────▼────────────────────┐
                    │       Optimizer (optimizer.h)    │
                    │  TableScan vs IndexScan choice   │
                    └────────────┬────────────────────┘
                                 │ chosen plan
                    ┌────────────▼────────────────────┐
                    │       Executor (executor.h)      │
                    │ TableScan / IndexScan / Join ops │
                    └──────┬────────────┬─────────────┘
                                 │            │
            ┌───────────────▼─┐    ┌─────▼─────────────┐
            │  HeapFile (heap) │    │  BPlusTree (index) │
            └────────┬─────────┘    └──────┬─────────────┘
                     │                     │
            ┌────────▼─────────────────────▼─────────────┐
            │            BufferPool (buffer_pool.h)        │
            │         Clock-sweep replacement policy      │
            └────────────────────┬────────────────────────┘
                                 │
            ┌────────────────────▼────────────────────────┐
            │          DiskManager (disk_manager.h)        │
            │        POSIX file I/O (open/read/write)     │
            └─────────────────────────────────────────────┘
```

1. **SQL Parsing**: [parser.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/query/parser.cpp) tokenizes incoming queries into AST nodes.
2. **Cost Optimization**: [optimizer.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/query/optimizer.cpp) selects the cheapest execution strategy (e.g., table scan vs. index search).
3. **Execution**: [executor.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/query/executor.cpp) streams tuples using a Volcano iterator interface.
4. **Locks**: [lock_manager.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/transaction/lock_manager.cpp) coordinates Shared and Exclusive locks.
5. **WAL Logging**: [wal.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/recovery/wal.cpp) flushes log records to disk before writing database pages.
6. **Replication**: [primary.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/replication/primary.cpp) ships committed transaction streams to [replica.cpp](file:///home/smit/Desktop/ExternalAssignments/scaler-Adv-DBMS/MiniDB_Projects/Capstone_Project/src/replication/replica.cpp).

---

## 3. Storage Layer

The storage engine manages low-level byte arrays, transforming them into records:

* **Slotted-Page Format**: Each page is configured at **4 KB (4,096 bytes)**. A forward-growing slot array tracks record offsets and lengths, while data records grow backward from the end of the page to maximize free space.
* **Heap File Chaining**: Chained pages are managed dynamically. Inserting a record allocates page locations and returns a `RecordID` (`[PageID, SlotID]`).
* **Clock-Sweep Buffer Pool**: Keeps 64 pages pinned/unpinned in memory. When eviction is triggered, dirty pages are flushed to disk first.

---

## 4. Indexing & B+ Trees

MiniDB leverages a disk-based B+ Tree (Order = 4) to support key lookups:
* **Node Types**: Internal nodes route key searches, whereas leaf nodes store key-`RecordID` mapping tables.
* **Leaf Linking**: All leaf pages are sequentially linked, enabling $O(\log N + K)$ range searches.
* **Buffer Pool Integration**: Nodes are pinned, loaded, and unpinned as separate database pages during traversal.

---

## 5. Transactions, WAL & Crash Recovery

To guarantee transaction consistency, the engine combines strict concurrency rules with durable logging:

* **Strict 2PL**: Shares (S) and Exclusive (X) locks are held until transaction completion (either `COMMIT` or `ABORT`), ensuring serializable isolation and preventing cascading rollbacks.
* **Deadlock Resolution**: A background cycle-detection algorithm scans waits-for dependency graphs using DFS and aborts deadlocked transactions.
* **ARIES-style Redo Recovery**: On restart, MiniDB parses the binary WAL file, identifies committed transaction IDs, and replays their log operations sequentially to recover consistent state. Uncommitted writes are discarded.

---

## 6. Distributed Replication

MiniDB implements a **Primary-Replica Log Shipping** replication model (Track D):
* **Primary (Read-Write)**: Streams committed transaction records into `minidb_replication.log`.
* **Replica (Read-Only)**: A background polling thread reads the replication log, buffers incoming updates per transaction, and applies them locally to `minidb_replica.db` once a `COMMIT` record is parsed. Direct writes to the replica are rejected.

---

## 7. Performance Benchmarks

* **Environment**: Linux Ubuntu 22.04 LTS, GCC 11.4.0 (C++17, Debug optimization).
* **Workload Size**: 10,000 auto-commit insert operations and 1,000 record point selections.

| Metric | Measured Value | Details |
|:---|:---|:---|
| **Auto-Commit Write Rate** | **~2,218 writes/second** | Includes Strict 2PL locking + WAL fsync |
| **Average Write Latency** | **~450.75 μs** | Time taken per INSERT query |
| **Table Scan Latency** | **~0.303 ms** | Point lookup over 1,000 records ($O(N)$) |
| **Index Scan Latency** | **~0.00018 ms** | Primary-key B+ Tree search ($O(\log N)$) |
| **Search Speedup Factor** | **~1,645x** | Index search speedup over Table Scan |

---

## 8. Limitations & Scope

* **Numeric Indexing**: Indexing is limited to primary keys of integer types.
* **Simple Query Grammar**: Joins are limited to primary-key equality. Nested WHERE clauses are not supported.
* **Fixed Cache Limits**: The buffer pool is set at 64 pages.
* **Lock Granularity**: Supports key-level locking; page-level and table-level locks are not implemented.

---

## 9. Installation & Execution

### Prerequisites
* **g++ 11+** (supporting C++17 standards)
* **CMake 3.17+**
* Linux/POSIX environment

### Build Steps
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

### Running Demonstrations

1. **Performance Suite**:
   ```bash
   bash benchmarks/run_benchmarks.sh
   ```
2. **Crash Recovery Test**:
   ```bash
   bash scripts/demo_crash_recovery.sh
   ```
3. **Replication Sync Test**:
   ```bash
   bash scripts/demo_replication.sh
   ```
4. **Manual Prompt Shell (Primary)**:
   ```bash
   ./build/minidb
   ```
5. **Manual Prompt Shell (Replica)**:
   ```bash
   ./build/minidb --replica
   ```
