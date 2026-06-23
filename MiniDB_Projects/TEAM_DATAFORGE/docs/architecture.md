# MiniDB Architecture

MiniDB is built with a modular, layered architecture inspired by modern relational database management systems like PostgreSQL and SQLite.

## 1. Storage Layer
Responsible for persisting data on disk and managing memory.
- **Disk Manager (`disk_manager.py`)**: Handles OS-level file I/O operations, reading and writing fixed-size pages (4KB) to `.db` files.
- **Page (`page.py`)**: Implements the Slotted Page layout. Each page has a header, a slot array growing forwards, and raw tuple data growing backwards. This design allows efficient in-page record deletion and compaction.
- **Heap File (`heap_file.py`)**: Manages a collection of pages that make up a table. It tracks free space in pages to optimize record insertion.
- **Buffer Pool (`buffer_pool.py`)**: Caches disk pages in memory to minimize I/O overhead. Uses an LRU (Least Recently Used) replacement policy and tracks "dirty" pages that must be flushed to disk.

## 2. Catalog & Indexing
- **Catalog (`catalog.py`)**: The system catalog stores metadata about tables, columns, indexes, and statistics.
- **B+ Tree (`bplus_tree.py`)**: A disk-backed B+ tree implementation used for primary and secondary indexes. It provides `O(log N)` search complexity for point queries and maintains a leaf-level linked list for fast range scans.

## 3. Query Processor
- **Lexer & Parser (`lexer.py`, `parser.py`)**: Converts raw SQL string queries into an Abstract Syntax Tree (AST).
- **Optimizer (`cost_estimator.py`, `plan_generator.py`)**: A cost-based optimizer that uses table statistics to estimate query selectivity. It converts logical AST nodes into a Physical Execution Plan, making decisions like selecting a sequential scan vs. index scan, and determining join orders.
- **Execution Engine (`executor.py`, `operators.py`)**: Implements the Volcano Iterator model (`open`, `next`, `close`). Physical operators (e.g., `SeqScan`, `IndexScan`, `NestedLoopJoin`, `Filter`, `Aggregate`) pull tuples from their children to execute queries.

## 4. Transaction Management
- **Transaction Manager (`transaction_manager.py`)**: Handles the lifecycle (`BEGIN`, `COMMIT`, `ROLLBACK`) of transactions, managing global transaction IDs and active states.
- **Lock Manager (`lock_manager.py`)**: Implements Strict Two-Phase Locking (2PL). Writers acquire exclusive row-level locks, blocking other concurrent writes to the same rows. It uses a wait-for graph with cycle detection to resolve deadlocks.
- **MVCC (`mvcc.py`)**: Multi-Version Concurrency Control replaces shared read locks, providing snapshot isolation.

## 5. Recovery Manager
- **Write-Ahead Log (`wal.py`)**: All database modifications are logged sequentially to `_wal.log` *before* the actual data pages are flushed to disk (Force-Log-at-Commit).
- **Recovery (`recovery_manager.py`)**: Implements ARIES-style crash recovery. On system startup, it performs Analysis, Redo (reapplying committed changes), and Undo (rolling back uncommitted changes) phases to restore the database to a consistent state.
