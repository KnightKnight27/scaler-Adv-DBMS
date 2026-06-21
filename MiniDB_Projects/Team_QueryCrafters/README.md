# MiniDB — A Ground-Up Relational Database Engine

**Team Name**: Team_QueryCrafters  
**Members**:  
- Ayushkumar Tapendrakumar Singh | Roll No. 10199  
- Anushka Jain | Roll No. 10193  

---

## 1. Project Overview
MiniDB is a complete, fully functional, and self-contained relational database engine built entirely from scratch in pure Python. It implements slotted-page disk storage, B+ Tree indexing, a recursive descent SQL parser, a Volcano execution model, statistics-based selectivity estimation and query planning, Two-Phase Locking, Write-Ahead Logging (WAL) recovery (ARIES), and Snapshot Isolation via Multi-Version Concurrency Control (MVCC).

## 2. System Architecture
The system consists of the following components working end-to-end:

```
            +------------------------------------+
            |             Client REPL            |
            +-----------------+------------------+
                              | SQL Query String
                              v
            +-----------------+------------------+
            |      Recursive Descent Parser      |
            +-----------------+------------------+
                              | Query AST
                              v
            +-----------------+------------------+
            |       Cost-Based Optimizer         |
            +-----------------+------------------+
                              | Physical Query Plan
                              v
            +-----------------+------------------+
            |     Volcano Execution Engine       |
            +-----------------+------------------+
             /                |                 \
            /                 |                  \
           v                  v                   v
+----------+----+     +-------+--------+     +----+-----------+
| Storage Layer |     | B+ Tree Index  |     | Transaction /  |
|  (Slotted)    |     |   (.idx files) |     |  Locks / MVCC  |
+----------+----+     +----------------+     +----+-----------+
           |                                      |
           v                                      v
+----------+----+                            +----+-----------+
|  Buffer Pool  |                            |   Recovery /   |
|   (10 pgs)    |                            |   wal.log      |
+---------------+                            +----------------+
```

## 3. Storage Layer
- **Page Layout (`storage/page.py`)**: Stores records using fixed-size 4KB slotted pages. The header contains the page ID, number of slots, free space boundary offset, and flags. Individual slots grow forward, and JSON-encoded records grow backward.
- **Heap File (`storage/heap_file.py`)**: Manages physical `.db` table files. Scans pages via the buffer pool and handles dynamic page allocation.
- **Buffer Pool (`storage/buffer_pool.py`)**: Implements a cache with a capacity of 10 pages using LRU eviction. Tracks page pin counts to prevent active pages from eviction and implements dirty-page writeback.

## 4. Indexing
- **B+ Tree Index (`indexing/bplus_tree.py`)**: Implements an Order-4 B+ tree. Leaf nodes are linked doubly to optimize range scans.
- **Primary & Secondary Index**: A primary key index is built automatically on table creation. Supports building secondary indexes on columns (e.g. `age` or `name`). Persists index trees to `.idx` files.

## 5. Query Execution
- **Volcano Iterator Model (`executor/operators.py`, `executor/executor.py`)**: Implements `open()`, `next()`, and `close()` iterators.
- **Operators**: Includes `SeqScan`, `IndexScan`, `Filter`, `Projection`, `NestedLoopJoin`, `Insert`, and `Delete`.

## 6. Optimizer
- **Statistics (`optimizer/optimizer.py`)**: Tracks row counts and column boundaries (min, max, and distinct values).
- **Cost Estimation**: Estimates query selectivity for equality (`1 / distinct`) and range (`(max - val)/(max - min)`) predicates. Compares cost of `IndexScan` vs `SeqScan` and evaluates the join order permutations of 2-table joins.

## 7. Transactions & Concurrency
- **Lock Manager (`transactions/lock_manager.py`)**: Supports Shared and Exclusive locks. Resolves lock conflicts by building a Waits-for graph and aborting the youngest transaction in a cycle.
- **Transaction Manager (`transactions/transaction_manager.py`)**: Supports Strict Two-Phase Locking (SS2PL) for serializable transaction isolation.

## 8. Recovery
- **Write-Ahead Log (`recovery/wal.py`)**: Appends transaction logs to `wal.log` using standard LSN numbering. Enforces the WAL rule (flushing WAL records before dirty data pages).
- **Recovery Manager (`recovery/recovery_manager.py`)**: Implements ARIES recovery consisting of:
  1. *Analysis Pass*: Finds active transactions at crash.
  2. *Redo Pass*: Replays all logged updates to restore database state.
  3. *Undo Pass*: Rolls back modifications made by uncommitted loser transactions.
- **Checkpoints**: Triggers checkpoint writes and flushes all dirty memory pages.

## 9. Extension Track: MVCC
- **Snapshot Isolation (`extension/mvcc.py`)**: Replaces 2PL locking with multi-versioned records storing `created_by_txn`, `deleted_by_txn`, `begin_ts`, and `end_ts`. Reader transactions never block writer transactions.
- **Conflict Aborts**: Detects write-write conflicts on write/delete operations and aborts the transaction (first-committer-wins).
- **Pruning**: Periodically garbage-collects deleted versions that are older than all active transaction snapshots.

## 10. Benchmarks
Benchmark results are collected and formatted in [results.md](file:///Users/ayushsingh/Desktop/MiniDB relational database/MiniDB_Projects/Team_QueryCrafters/benchmarks/results.md). They cover:
- Insert throughput (rows/sec)
- SeqScan vs IndexScan latency percentiles (p50/p95)
- Concurrent transaction throughput and deadlock abort rates
- Average response times of readers under MVCC vs 2PL
- Crash recovery duration

## 11. Limitations
- Single-statement updates are currently implemented as `DELETE` + `INSERT`.
- Complex nested subqueries are not supported by the SQL parser.
- Locks are held globally in memory (reconstructed dynamically during transactions).

## 12. How to Run
Prerequisites: Python 3.x and `pytest`.

```bash
# Install pytest
pip install pytest

# Start the REPL
PYTHONPATH=. python3 src/minidb.py

# Run the test suite
python3 -m pytest tests/

# Run the benchmark suite
PYTHONPATH=. python3 benchmarks/benchmark.py
```
