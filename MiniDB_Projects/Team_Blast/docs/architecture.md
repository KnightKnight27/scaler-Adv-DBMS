# MiniDB Architecture Document

This document provides a comprehensive overview of the design and architecture of **MiniDB**, a system-level relational database written in C++17 from scratch.

---

## 1. System Topology & Data Flow

MiniDB separates parser, optimizer, executor, transaction management, page storage, and replication layers into modular components:

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

---

## 2. Core Subsystems

### 2.1 Storage Layer (`src/storage/`)
* **Slotted Page Layout (`page.h`)**: Each page is 4 KB. The header (`PageHeader`) keeps track of page ID, number of slots, and the offset of free space. The slot array grows forwards (from the header), and the actual record data grows backwards (from the end of the page).
* **Disk Manager (`disk_manager.h/.cpp`)**: Operates direct POSIX system calls (`open`, `read`, `write`, `lseek`) to read/write raw pages. Page 0 stores page allocation metadata.
* **Buffer Pool Manager (`buffer_pool.h/.cpp`)**: Caches up to 64 pages in RAM using a **Clock-Sweep** replacement policy. Pages are pinned when active and unpinned with a dirty bit. Eviction sweeps frames and flushes dirty pages to disk first.
* **Heap File (`heap_file.h/.cpp`)**: Organizes database records in slotted pages. Provides a forward scanner to scan all records in the table.

### 2.2 Indexing Layer (`src/index/`)
* **B+ Tree Index (`bplus_tree.h/.cpp`)**: A disk-model-ready primary key B+ tree (fixed ORDER = 4 for clear splits and joins in vivas). Supports $O(\log N)$ point queries, leaf-to-leaf linking for range scanning, and node split/merge algorithms.

### 2.3 Query Layer (`src/query/`)
* **Parser (`parser.h/.cpp`)**: A simple tokenizer parsing standard DDL/DML (e.g. `CREATE TABLE`, `INSERT`, `SELECT * WHERE`, `JOIN`, `DELETE`).
* **Optimizer (`optimizer.h/.cpp`)**: Cost-based optimizer that estimates costs using table stats (number of pages, records). Chooses `INDEX_SCAN` when selectivity is high (point/range query), and falls back to `TABLE_SCAN` for full table fetches.
* **Executor (`executor.h/.cpp`)**: Volcano-style query executor implementing table scans, primary-key searches, and nested-loop joins.

### 2.4 Transaction & Lock Manager (`src/transaction/`)
* **Lock Manager (`lock_manager.h/.cpp`)**: Implements **Strict Two-Phase Locking (Strict 2PL)**. Locks can be acquired in `SHARED` or `EXCLUSIVE` modes.
* **Deadlock Detection**: Rebuilds the waits-for dependency graph of transactions and runs DFS cycle detection. If a cycle is detected, the transaction causing the deadlock is aborted with a `DeadlockException`.
* **Transaction Manager (`tx_manager.h/.cpp`)**: Tracks transaction states (`ACTIVE`, `COMMITTED`, `ABORTED`) and manages undo/redo states.

### 2.5 Write-Ahead Logging & Recovery (`src/recovery/`)
* **WAL (`wal.h/.cpp`)**: A thread-safe, append-only logger writing binary `WALRecord` structures to disk before any changes are written to database files (Write-Ahead rule). `COMMIT` forces an fsync.
* **Recovery Manager (`recovery.h/.cpp`)**: Performs ARIES-like Redo-only recovery. Replays committed transactions and discards uncommitted transactions on startup.

### 2.6 Replication Layer (`src/replication/`)
* **Primary (`primary.h/.cpp`)**: Tracks primary WAL state and streams committed log records into `minidb_replication.log` representing the network replication channel.
* **Replica (`replica.h/.cpp`)**: Runs on a separate database instance. Periodically polls the replication log, buffers incoming records, and applies them to its local database file once a `COMMIT` record is observed.
