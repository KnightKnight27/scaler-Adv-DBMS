# MiniDB — Advanced DBMS Capstone Project

**Team Blast** · Advanced Database Management Systems · Scaler School of Technology

---

## Team Members

| Name | Email | Roll No |
|------|-------|---------|
| Lavanya Soni | lavanya.24bcs10028@sst.scaler.com | 24bcs10028 |
| Mayank Soni | mayank.24bcs10127@sst.scaler.com | 24bcs10127 |
| Tirth Bhalani | tirth.24bcs10098@sst.scaler.com | 24bcs10098 |
| Sarthak Agarwal | sarthak.24bcs10149@sst.scaler.com | 24bcs10149 |

---

## Team Contributions

### Lavanya Soni (Storage & Memory Cache Subsystem)
* **Assigned Modules**: Storage Layer (`disk_manager.cpp`, `page.h`, `buffer_pool.cpp`)
* **Key Contributions**:
  * Implemented the binary slotted-page structure managing variable-length rows, headers, and slot updates.
  * Developed the `DiskManager` page allocator executing direct system-level POSIX file I/O operations.
  * Built the Clock-Sweep buffer pool caching system (64 frames) coordinating thread-safe page pinning/unpinning and dirty page flushing.

### Mayank Soni (Indexing & Parser Subsystem)
* **Assigned Modules**: Indexing Layer (`bplus_tree.cpp`) & SQL Parser (`parser.cpp`)
* **Key Contributions**:
  * Programmed the primary-key B+ Tree structure (ORDER=4) handling leaf splits, internal routing splits, and traversal.
  * Connected leaf nodes into a double-linked sequence to support fast $O(\log N + K)$ range queries.
  * Designed the SQL command parser tokenizing and converting strings into structured query plan parameters.

### Tirth Bhalani (Volcano Engine & Query Optimizer)
* **Assigned Modules**: Execution Engine (`executor.cpp`) & Cost-Based Optimizer (`optimizer.cpp`)
* **Key Contributions**:
  * Implemented the Volcano iterator model execution operators (`TableScanOp`, `IndexScanOp`, `NestedLoopJoinOp`, `ProjectionOp`).
  * Developed the disk cost estimation formula comparing table page costs against index heights.
  * Designed selectivity cost multipliers ($S = \frac{1}{N}$ for point lookups, $S = 0.5$ for range lookups) and outer/inner join ordering logic.

### Sarthak Agarwal (Transactions, Concurrency, WAL & Replication)
* **Assigned Modules**: Concurrency & Recovery (`lock_manager.cpp`, `tx_manager.cpp`, `wal.cpp`, `recovery.cpp`) & Replication (`primary.cpp`, `replica.cpp`)
* **Key Contributions**:
  * Built the Strict Two-Phase Locking (Strict 2PL) engine enforcing shared and exclusive transaction boundaries.
  * Programmed waits-for dependency graphs and DFS deadlock cycle detection, as well as transaction abort rollback undo logs.
  * Implemented WAL redo-only recovery and primary-replica log shipping streams using sequential file offsets.

---

## 1. Project Overview

### Problem Statement
Traditional relational databases function as complex "black boxes" with hidden layers. Understanding the deep, lower-level interactions between memory caching, file systems, B+ trees, query planning, lock schedules, and transaction boundaries requires building a relational database engine from the ground up. MiniDB was designed to solve this complexity by implementing a clear, explainable, and modular relational database engine in C++17 from raw system calls.

### Goals
* Implement page-level binary storage using a slotted-page architecture.
* Build an efficient memory cache layer using a clock-sweep buffer pool.
* Design a disk-model-ready B+ tree index supporting fast search, insert, and delete.
* Support declarative query execution using a Volcano iterator pipeline (scans, joins, filters).
* Guarantee transactional serializability using Strict Two-Phase Locking (Strict 2PL) with waits-for graph deadlock detection.
* Achieve crash resilience using Write-Ahead Logging (WAL) and ARIES-style Redo-only recovery.
* Deploy a Leader-Follower replication system with log shipping.

### Chosen Extension Track
We selected **Track D — Distributed Systems / Replication**. Under this track, we implemented a synchronous/asynchronous primary-replica log shipping model over local Unix system file pipes, which replicates committed data changes and enforces strict read-only constraints on the replica instance.

---

## 2. System Architecture

MiniDB is divided into six decoupled modules cooperating to parse, optimize, lock, write, recover, and replicate data:

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

### Module Data Flow
1. **Command parsing**: The SQL string is tokenized by the `Parser` into a `ParsedQuery` containing operation metadata.
2. **Optimization**: The `Optimizer` selects either a `TABLE_SCAN` or `INDEX_SCAN` using table metadata and selectivity costs.
3. **Execution**: The Volcano iterator `Executor` processes the plan. For reads, it accesses memory pages. For writes, it interacts with transactions.
4. **Transaction Control**: The `TxManager` coordinates with the `LockManager` to enforce Strict 2PL locks on keys.
5. **Log Ship (WAL)**: Write-Ahead Log entries are written to `minidb.wal` and flushed (`fsync`) before pages are modified.
6. **Replication**: The `Primary` ships the logs to `minidb_replication.log`. The `Replica` process reads the stream sequentially and applies changes.

---

## 3. Storage Layer

The storage engine represents a multi-level layout abstracting hard drives into clean record-level variables:

### Page Format (Slotted Page)
MiniDB pages are fixed at **4 KB (4,096 bytes)** matching OS sector sizes to avoid double buffering. Each page implements a slotted layout to store variable-length values without fragmentation:
```
┌────────────────────────────────────────────────────────┐
│ PageHeader                                             │
│ PageID (4B) | FreeSpaceOffset (2B) | NumSlots (2B)     │
├────────────────────────────────────────────────────────┤
│ Slot Array: [Slot 0] [Slot 1] ...                      │
│ (Each slot stores offset and size of record)           │
├───────────────────────────────────┬────────────────────┤
│             ▲                     │  Record Data       │
│             │                     │  (Grows backward)  │
│      Free space gaps              │  ...               │
│                                   │  [Record 1]        │
│                                   │  [Record 0]        │
└───────────────────────────────────┴────────────────────┘
```
* The **Slot Array** grows forward from the end of the header.
* The **Record Data** grows backward from the end of the page.
* **Fragmentation** is computed dynamically, and new records are inserted at `FreeSpaceOffset`.

### Heap Files
Tables are modeled as `HeapFile` objects consisting of chained physical pages. Heap files support:
* `insertRecord(Record)`: Finds a page with space or allocates a new page, updates page headers, writes data, and returns a unique `RecordID` containing `[PageID, SlotID]`.
* `getRecord(RecordID)`: Accesses page offset to fetch key-value values in $O(1)$ time.
* `deleteRecord(RecordID)`: Marks slot offset as invalid to free slot capacity.

### Buffer Pool
Manages an in-memory array of **64 page frames** cached from disk using the **Clock-Sweep** algorithm:
* Each page frame maintains a `pin_count` (active readers/writers) and a `dirty` flag (indicates unsaved modifications).
* Eviction uses a circular pointer sweeping the frames. If a page has `pin_count == 0` and its reference bit is unset, it is evicted. If the evicted page is dirty, it is flushed to disk first.

---

## 4. Indexing

### B+ Tree Design
We implemented a disk-model-ready primary key B+ Tree index with a fixed **ORDER = 4**. Nodes in the index reside on separate storage pages and are coordinated through the buffer pool.

### Node Structure
* **Internal Nodes**: Contain up to 3 keys and 4 child pointer PageIDs. They route point searches.
* **Leaf Nodes**: Contain up to 3 key-value entries. Instead of child pointers, leaf entries store the physical `RecordID` (`[PageID, SlotID]`) pointing to the record in the Heap File.
* **Linked Leaves**: All leaf nodes contain a `next_leaf` PageID pointer, forming a sequential link. This design supports range queries in $O(\log N + K)$ time without traversing internal nodes.

### Search Path
1. Start at the root page.
2. Read the page into the buffer pool (pin page).
3. Search keys sequentially or via binary search:
   * If internal node: identify the index of the first key greater than search key, fetch child PageID, unpin current node, pin child page, and descend.
   * If leaf node: locate key matching query key and return the associated `RecordID`.

---

## 5. Query Execution

### Parser
A robust whitespace tokenizer parsing:
* `CREATE TABLE <table>`
* `INSERT INTO <table> VALUES (<key>, <value>)`
* `SELECT * FROM <table>`
* `SELECT * FROM <table> WHERE id = <key>` (or `id > <key>`)
* `SELECT * FROM <t1> JOIN <t2> ON t1.id = t2.id`
* `DELETE FROM <table> WHERE id = <key>`
* `BEGIN`, `COMMIT`, `ABORT`, `SHOW TABLES`, `QUIT`

### Query Plan Generation
Queries translate into structured Volcano operators:
* `TableScanOp`: Standard full heap-file scanner.
* `IndexScanOp`: Point and range queries utilizing B+ Tree searches.
* `NestedLoopJoinOp`: Processes two table operators via nested iteration.
* `ProjectionOp`: Filters fields for print formats.

### Operator Execution (Volcano Model)
All operators derive from a base interface implementing `init()` and `next()`. Operators execute iteratively, pulling single tuples from children to minimize memory overhead.

---

## 6. Optimizer

The cost-based optimizer estimates costs and chooses between a `TABLE_SCAN` or `INDEX_SCAN`.

### Cost Estimation
The optimizer estimates the cost of a scan in terms of Disk I/O operations ($C_{IO}$):
* $Cost(TableScan) = Pages_{table} \times C_{IO}$
* $Cost(IndexScan) = (Height_{B+Tree} + Selectivity \times Records_{table}) \times C_{IO}$

### Selectivity Estimation
Selectivity ($S$) is the estimated fraction of records matching a predicate:
* **Point Query (`id = key`)**: $S = \frac{1}{Records_{table}}$ (high selectivity)
* **Range Query (`id > key`)**: Assumed to select half of the table: $S = 0.5$
* **Full Scan**: $S = 1.0$

### Join Ordering
When a join query is parsed:
* The optimizer estimates table statistics (record count) and places the smaller table in the outer loop.
* This ordering minimizes outer loops, optimizes page hits in the buffer pool, and leverages index scan lookups on the inner loop.

---

## 7. Transactions & Concurrency

### Locking Strategy (Strict 2PL)
To ensure isolation, MiniDB implements **Strict Two-Phase Locking (Strict 2PL)**:
* **Shared (S) locks**: Acquired before reading a key. Multiple transactions can hold S locks on the same key.
* **Exclusive (X) locks**: Acquired before inserting or deleting a key. Only one transaction can hold an X lock on a key.
* **Strict Enforcement**: All locks acquired by a transaction are held until the transaction finishes and are released at `COMMIT` or `ABORT` (no shrinking phase until termination, preventing cascading aborts).

### Isolation Guarantees
Holding all locks until the commit point ensures **Serializable** isolation (the highest level), preventing dirty reads, non-repeatable reads, and phantoms.

### Deadlock Handling
* Concurrency checks construct an in-memory **Waits-For Graph** representing transactions waiting on locks.
* A background check runs a Depth-First Search (DFS) cycle-detection algorithm.
* If a cycle is detected, the transaction causing the deadlock is aborted with a `DeadlockException`. The executor rolls back its pending changes using transaction undo records.

---

## 8. Recovery

### WAL Design
MiniDB implements a Write-Ahead Log (WAL) to guarantee durability. The log is written sequentially to `minidb.wal` using POSIX system calls. Transactions do not commit until log records are flushed using `fsync`.

### Log Records
Each record is a fixed 309-byte binary structure:
```cpp
struct WALRecord {
    LSN           lsn;          // 8 bytes
    TxID          txid;         // 8 bytes
    WALRecordType type;         // 1 byte (BEGIN, INSERT, DELETE, COMMIT, ABORT)
    char          table_name[32];
    int32_t       key;
    char          value[128];
};
```

### Crash Recovery Procedure
On startup, MiniDB checks for an existing `minidb.wal` and runs ARIES Redo-only recovery:
1. **Analysis Phase**: Scans the WAL sequentially. Collects all transaction IDs that have a `COMMIT` record into a `committed_transactions` set.
2. **Redo Phase**: Scans the WAL sequentially from the beginning. If an `INSERT` or `DELETE` log record is from a transaction in the `committed_transactions` set, the changes are replayed in the heap file and index. Uncommitted transaction records are discarded.

---

## 9. Extension Track: Distributed Replication

### Motivation
In modern databases, single-node storage limits read throughput and creates single points of failure. Replication distributes read traffic and ensures high availability.

### Design
We implemented a **Leader-Follower (Primary-Replica) log shipping model**:
* **Primary Node**: Runs in read-write mode, writing changes to `minidb.wal`. It ships committed logs sequentially to `minidb_replication.log`.
* **Replica Node**: Runs in read-only mode via `./minidb --replica` using database `minidb_replica.db`.
* **Polling Thread**: The replica runs a background thread polling `minidb_replication.log` every 200ms. It uses a custom `read_offset_` to read sequentially and buffers logs per `TxID`. Once a `COMMIT` record is parsed, the buffered changes are written to the replica's local heap and index.

### Results
* Fully functional two-node architecture running on local processes.
* The replica displays updated records immediately after a primary `COMMIT`.
* Read-only protection prevents data divergence on the replica.

---

## 10. Benchmarks

### Experimental Setup
* **Operating System**: Linux Ubuntu 22.04 LTS (x86_64)
* **Compiler**: GCC 11.4.0 (C++17, Optimization Level: Debug)
* **Disk Model**: Slotted pages stored in SSD block systems
* **RAM Configuration**: 64 cached frames (256 KB buffer pool cache size)

### Results

| Metric / Experiment | Cost / Speed | Output / Throughput |
|---------------------|--------------|---------------------|
| **10,000 Inserts (Auto-Commit)** | Total time: 4.51 seconds | **2,218 writes/second** |
| **Average Insert Latency** | **450.75 μs** per write | Includes Strict 2PL + WAL fsync |
| **Table Scan Point Query** | **0.303 ms** per scan | Scan of 1,000 records ($O(N)$) |
| **Index Scan Point Query** | **0.00018 ms** per index | B+ Tree query ($O(\log N)$) |
| **Search Speedup Factor** | **1,645.37x speedup** | B+ Tree over full Table Scan |
| **Pages Allocated** | 48 pages | Data compact slot utilization |

### Analysis
* **Insert Latency**: Auto-commit inserts require an `fsync` call to ensure durability. The disk I/O throughput of ~2,218 transactions per second matches standard disk sync speeds on modern file systems.
* **Scan vs Index Search**: B+ Tree index lookup shows a **1,645.37x speedup** over Table Scan. This highlights why primary-key indexing is critical for databases as table sizes scale.

---

## 11. Limitations

### Missing Features
* **No Secondary Indexing**: Indexing is restricted to primary key integers.
* **Simple Queries**: Parser does not support arbitrary nested WHERE conditions (only basic key comparison operator expressions like `=`, `>`).
* **Basic Joins**: The engine only supports single-attribute, primary-key equality joins.

### Scalability Limits
* **Static Cache size**: Buffer pool is locked at 64 frames. A larger database footprint would experience cache thrashing under high workloads.
* **Lock Granularity**: Locking is enforced at the key level. Table-level and page-level locks are not implemented.

### Future Improvements
* **Network log shipping**: Replace file log shipping with TCP/IP network sockets.
* **Multi-version Concurrency Control (MVCC)**: Implement reader/writer non-blocking snapshots to improve concurrent transaction throughput.

---

## 12. How to Run

### Dependencies
* **g++ 11+** (supporting C++17)
* **CMake 3.17+**
* Linux / POSIX environment

### Build Steps
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

### Running Demonstrations

#### 1. Performance Benchmarks
To run the performance suite showing insert throughput and B+ tree search speedups:
```bash
./benchmarks/run_benchmarks.sh
```

#### 2. WAL Crash Recovery Demo
To verify WAL recovery of committed transactions after a hard process crash (`kill -9`):
```bash
./scripts/demo_crash_recovery.sh
```

#### 3. Replication Demo
To launch the primary and replica background processes, replicate data, and verify the replica's read-only checks:
```bash
./scripts/demo_replication.sh
```

#### 4. Interactive REPL Shell (Manual Run)
To start the standard primary instance:
```bash
./build/minidb
```
To start the replica instance in a separate terminal:
```bash
./build/minidb --replica
```
Inside the prompt, try executing:
```sql
CREATE TABLE users
INSERT INTO users VALUES (10, Mayank)
SELECT * FROM users WHERE id = 10
BEGIN
INSERT INTO users VALUES (20, Tirth)
COMMIT
QUIT
```
