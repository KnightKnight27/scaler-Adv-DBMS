# **MiniDB — Relational Database System Engine**

### **Team: Gojo**
### **Team Members**
1. **Mehul Agarwal**  
   * **Roll Number**: 24bcs10128  
   * **Email**: mehul.24bcs10128@sst.scaler.com  
2. **Sanjay Kumar**  
   * **Roll Number**: 24bcs10147  
   * **Email**: sanjay.24bcs10147@sst.scaler.com  

---

## **1. Project Overview**
MiniDB is a lightweight, fully functional relational database engine written in C++17. The engine integrates foundational database components studied throughout the Advanced Database Management Systems (ADBMS) curriculum, implementing a page-based storage manager, B+ Tree index, Volcano-style query execution engine, cost-based optimizer, transactional locking (Strict 2PL), write-ahead logging (WAL), and ARIES-style crash recovery.

### **Goals & Scope**
* **Educational Realism**: Model key database internal components (buffer management, indexing, serialization, logging) realistically using raw byte serialization and page-level interactions.
* **Component Integration**: Combine storage, index, query execution, transactions, and recovery into a unified database shell.
* **Extension Track**: **Track A (Performance — Vectorized/Batch Execution)**.

---

## **2. System Architecture**

MiniDB utilizes a clean layered architecture where requests flow from a text-based parser down to physical files on disk.

```
                  ┌───────────────────────────────┐
                  │           SQL Query           │
                  └───────────────┬───────────────┘
                                  ▼
                  ┌───────────────────────────────┐
                  │      Lexer & Parser (AST)     │
                  └───────────────┬───────────────┘
                                  ▼
                  ┌───────────────────────────────┐
                  │     Cost-Based Optimizer      │◄─── Catalog (Stats)
                  └───────────────┬───────────────┘
                                  ▼
                  ┌───────────────────────────────┐
                  │   Volcano Execution Engine    │◄─── Lock Manager (S2PL)
                  │  (Batch / Vectorized Nodes)   │
                  └───────────────┬───────────────┘
                                  ▼
                  ┌───────────────────────────────┐
                  │    Heap Files (Table layer)   │◄─── B+ Tree Index (PK)
                  └───────────────┬───────────────┘
                                  ▼
                  ┌───────────────────────────────┐
                  │     Buffer Pool Manager       │◄─── Log Manager (WAL)
                  └───────────────┬───────────────┘
                                  ▼
                  ┌───────────────────────────────┐
                  │         Disk Manager          │
                  └───────────────┬───────────────┘
                                  ▼
                  ┌───────────────────────────────┐
                  │      Data File & WAL File     │
                  └───────────────────────────────┘
```

### **Major Modules**
1. **SQL Compiler (Lexer & Parser)**: Tokenizes inputs and produces an Abstract Syntax Tree (AST).
2. **Cost-Based Optimizer**: Transforms logical AST plans into physical operator trees based on selectivity and I/O costs.
3. **Execution Engine (Volcano)**: Evaluates operators via a demand-driven iterator model (pull-based). Supported nodes include `TableScan`, `IndexScan`, `Filter`, and `NestedLoopJoin`.
4. **Index Manager (B+ Tree)**: Provides $O(\log N)$ search and insert with automatic node-splitting.
5. **Concurrency Controller**: Uses Strict Two-Phase Locking (S2PL) at the record level with a deadlock detection wait-for graph.
6. **Recovery Manager**: Assures Durability and Atomicity using Write-Ahead Logging (WAL) and ARIES-style recovery (REDO/UNDO).
7. **Storage Engine**: Features fixed 4KB page files with a Buffer Pool Manager (LRU eviction).

---

## **3. Storage Layer**

### **Disk Manager & Page Format**
The database is structured as a collection of fixed-size **4096-byte pages** (implemented in `Page.h`).
* **DiskManager** mapping: Page $i$ corresponds directly to a file offset of $i \times 4096$ bytes.
* **Heap File Page Layout**:
  ```
  ┌──────────────────┬────────────────────────────────────────────────────────┐
  │ numRecords (4B)  │ Slot 0 (8B) | Slot 1 (8B) | ... | Slot 510 (8B)         │
  └──────────────────┴────────────────────────────────────────────────────────┘
  ```
  * **Header**: First 4 bytes store the number of slots used.
  * **Payload**: Fixed 8-byte slots mapped to a `Record { int32_t id, int32_t val }` schema. Max records per page: $(4096 - 4) / 8 = 511$.
  * **Logical Deletion**: Records are deleted via tombstones (ID marked as `-1`). This prevents shifting records in memory, which would invalidate active index references.

### **Buffer Pool**
The `BufferPool` manages a configurable in-memory page cache (default 50 pages).
* **LRU Eviction**: Pages are evicted based on least-recently-used access patterns.
* **Pinning**: Pages currently in use by query execution or index traversal are pinned (pin count $> 0$) and locked from eviction.
* **Dirty Tracking**: Modified pages are flagged as dirty and automatically flushed back to disk during eviction or teardown.

---

## **4. Indexing**

MiniDB implements a **B+ Tree Index** (in `BPlusTree.cpp`) as its primary key index on the `id` column.

### **Node Structures**
All nodes share a 9-byte header:
* `isLeaf` (1 byte), `numKeys` (4 bytes), and `parentId` (4 bytes).
* **Leaf Node Layout**: Up to 510 key-recordId pairs.
* **Internal Node Layout**: Up to 510 keys and 511 child page pointers.

### **Search and Insertion**
* **Search**: Recursively descends key routing paths until reaching a leaf page, executing a scan on the leaf page to fetch the `recordId`.
* **Insert**: Places the pair in a sorted slot. If page capacity is exceeded, it splits the page, allocates a new page, and propagates the middle key upwards. Sibling pointers are not supported in this simplified project.
* **Deletion**: Not implemented in this B+ Tree version (known limitation).

---

## **5. Query Execution**

The execution engine uses the Volcano demand-driven iterator model (in `PlanNode.h`):
1. **`open()`**: Initializes child operators and sets up execution state (e.g. materializing the inner relation for joins).
2. **`hasNext()`**: Returns `true` if a child operator has a matching tuple.
3. **`next()`**: Emits the next tuple.
4. **`close()`**: Clean up resources.

Supported physical operators:
* **TableScanNode**: Reads all heap pages sequentially, returning valid records.
* **IndexScanNode**: Uses the B+ Tree for point lookups on indexed columns.
* **FilterNode**: Applies predicates (`=`, `<`, `>`) on attributes.
* **NestedLoopJoinNode**: Joins two tables using an outer/inner loop. The inner relation is materialized in memory on `open()` to enable cheap rewind iterations.

---

## **6. Optimizer**

MiniDB uses a **Cost-Based Optimizer** (`Optimizer.cpp`) to decide access paths and join orders.

### **Cost Model**
* **Table Scan Cost**: $\text{Cost} = \text{numPages} = \lceil\text{numRows} / 511\rceil$
* **Index Scan Cost**: $\text{Cost} = \text{height of B+ Tree} + 1 \approx 4.0$

### **Selectivity Estimation**
* **Equality (`col = val`)**: Selectivity $\sigma = \frac{1}{\text{numDistinct}}$.
* **Range (`col < val` or `col > val`)**: Selectivity $\sigma = \frac{1}{3}$ (heuristic constant).

### **Decisions**
1. **Access Path**: For equality predicates on primary keys, if $\text{IndexScanCost} < \text{TableScanCost}$, it selects `IndexScanNode`; otherwise, it falls back to `TableScanNode + FilterNode`.
2. **Join Ordering**: Compares:
   * $\text{Cost}(A \bowtie B) = |A| + |A| \times |B|$
   * $\text{Cost}(B \bowtie A) = |B| + |B| \times |A|$
   The smaller table is chosen as the outer loop to minimize full inner table scans.

---

## **7. Transactions & Concurrency**

MiniDB guarantees **Serializable isolation** using **Strict Two-Phase Locking (S2PL)**.

### **Locking Strategy**
* **Granularity**: Record-level locking (`LockManager.cpp`), encoding table and record IDs into a single `int64_t`.
* **Modes**: Shared (S) and Exclusive (X) locks. Multiple transactions can hold S locks, but X locks are mutually exclusive.
* **Strict Phase**: Locks are acquired during execution (Growing phase) and **only** released together at transaction commit or abort (no shrinking phase).

### **Deadlock Handling**
* To prevent deadlocks, the lock manager implements a **no-wait** variant: if a lock cannot be immediately granted due to conflict, the transaction immediately aborts and rolls back.
* A cycle detection system is implemented in `DeadlockDetector.h` using a Wait-For Graph (DFS cycle search). If a cycle is detected, the youngest transaction (highest ID) is flagged as the victim.

---

## **8. Recovery**

MiniDB guarantees atomicity and durability via Write-Ahead Logging (WAL) and ARIES-style recovery (`RecoveryManager.cpp`).

### **WAL & Log Records**
Every page update is preceded by a log record containing:
`lsn`, `txnId`, `type` (BEGIN, INSERT, UPDATE, DELETE, COMMIT, ABORT), `tableId`, `recordId`, and both `old` (for UNDO) and `new` (for REDO) records (fixed 36-byte size).
WAL guarantees:
1. **Dirty Page Rule**: Log records must be written to disk before flushing a dirty page.
2. **Commit Rule**: All log records must be flushed before committing.

### **ARIES Crash Recovery Procedure**
1. **REDO Phase (Forward Scan)**: Replays all actions (inserts, updates, deletes) in the WAL to restore the database to its crash-time state.
2. **UNDO Phase (Backward Scan)**: Scans the log backwards, identifying "loser" transactions (active transactions without a COMMIT record) and restoring their modified slots to their `old` values.

---

## **9. Extension Track — Track A (Performance)**

We chose **Track A (Performance — Vectorized/Batch Execution)**.

### **Design & Motivation**
Traditional Volcano execution incurs a virtual function dispatch call for every single tuple processed. To mitigate this overhead, we introduced **vectorized batch processing** in `PlanNode.h`:
```cpp
virtual std::vector<Record> nextBatch(int batchSize = 100);
```
Operators like `TableScanNode`, `FilterNode`, and `NestedLoopJoinNode` override `nextBatch()` to extract up to 100 records in a single virtual call, replacing iterator loops with fast contiguous memory scans.

### **Results**
Benchmark tests comparing standard Volcano scan vs. batch-based scan processing 10,000 records show:
* **Row-at-a-time scan**: ~494 μs (~20.2 million rows/sec)
* **Batch scan (100 rows)**: ~448 μs (~22.2 million rows/sec)
* **Performance Gain**: ~10% improvement in scanning execution throughput by amortizing virtual calls.

---

## **10. Limitations**

1. **B+ Tree Deletion**: Sibling pointer updates and merging/redistribution are not implemented. Deleted records are tombstoned on pages but persist in the index.
2. **No Actual Thread Blocking**: Lock contention causes immediate aborts instead of thread-level sleeps.
3. **No Sibling Pointers**: The B+ Tree does not support range scans because sibling node link-pointers are absent.
4. **Single-Column Fixed Schema**: Schemas are restricted to `{ int32_t id, int32_t val }`.

---

## **11. How to Run**

### **Dependencies**
* **Compiler**: C++17 compatible compiler (GCC 8+, Clang 7+, or Apple Clang 11+).
* **Build System**: CMake (version 3.14 or higher).
* **OS**: Linux, macOS, or Windows.

### **Build Steps**
1. Open a terminal and navigate to the project directory:
   ```bash
   cd MiniDB_Projects/Team_stormborns
   ```
2. Configure the build system:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   ```
3. Compile the executable:
   ```bash
   cmake --build build
   ```

### **Run Examples**
Execute the unified system demonstration:
```bash
./build/minidb
```
This runs the full test suite verifying:
* SQL parsing.
* Insert/Select execution.
* Cost-Based Optimizer access paths and join order choices.
* Strict 2PL transaction compatibility.
* Standalone deadlock wait-for-graph cycle detection.
* WAL logging and crash recovery.
* Vectorized batch throughput benchmarks.
