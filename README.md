# Advanced Database Management Systems — System Design & Internals

Welcome to the **Advanced Database Management Systems (DBMS) System Design & Internals** repository. This repository contains a collection of deeply technical, first-principles architectural analyses of modern database engine designs. 

Each document explores the trade-offs, internal data structures, concurrency controls, durability guarantees, and execution models of different database engines.

---

## 📂 Repository Structure

The system design documentation is organized into topic-specific directories inside the `System_Design_Docs/` directory, following the course guidelines exactly:

```
System_Design_Docs/
├── PostgreSQL_vs_SQLite/
│   └── README.md         # Process-per-connection vs In-process embedded comparison
├── PostgreSQL_Internals/
│   └── README.md         # Buffer Manager, nbtree (Lehman-Yao B-tree), MVCC (CLOG), WAL, Planner
├── MySQL_InnoDB/
│   └── README.md         # Clustered B+tree storage, Buffer Pool midpoint LRU, Undo/Redo logs, Gap locking
└── RocksDB/
    └── README.md         # Log-Structured Merge Tree (LSM-tree), MemTable (SkipList), SSTables, Compaction
```

---

## 🎯 Topic Summaries

### 1. [PostgreSQL vs SQLite Architecture Comparison](System_Design_Docs/PostgreSQL_vs_SQLite/README.md)
* **Focus**: The fundamental architectural split between client-server (process-per-connection) and embedded (in-process library) database engines.
* **Key Internals Covered**: 
  - PostgreSQL's Postmaster, backend process fleet, and `shared_buffers` IPC model.
  - SQLite's Virtual Database Engine (VDBE) bytecode interpreter and single-file database format.
  - Granular page layout comparison (PostgreSQL's line pointer indirection vs. SQLite's key-sorted cell array).
  - Transaction and locking comparisons (PostgreSQL's fine-grained row locks and advisory locks vs. SQLite's file locks and WAL mode).
* **Experiments**: Concurrent write conflicts (`SQLITE_BUSY` vs. row-level wait), storage page density overheads, and multi-table join query plan analysis.

### 2. [PostgreSQL Internal Architecture](System_Design_Docs/PostgreSQL_Internals/README.md)
* **Focus**: Deep dive into the core subsystems of the PostgreSQL storage engine.
* **Key Internals Covered**:
  - **Buffer Manager**: Shared buffer descriptors, clock-sweep replacement algorithm, sequential scan buffer rings, and the checkpoint protocol.
  - **nbtree Indexing**: The Lehman-Yao B-tree concurrent modification algorithm (right-link chains, page high-keys, lock-free descent), duplicate deduplication, and the visibility map for index-only scans.
  - **MVCC & CLOG**: Row headers (`xmin`/`xmax`/`ctid`), transaction commit log status bits, Autovacuum reclamation, and Heap Only Tuple (HOT) redirect optimizations.
  - **WAL Subsystem**: Write-ahead log structure, Log Sequence Numbers (LSN), full-page writes for torn-page safety, checkpointing, and crash recovery replay.
  - **Query Planner**: Cost-based optimizer, statistics collection (`pg_statistic`), join strategy selection, and selectivity estimation.

### 3. [MySQL InnoDB Storage Engine](System_Design_Docs/MySQL_InnoDB/README.md)
* **Focus**: The design, storage organization, and locking mechanisms of MySQL's default transactional engine.
* **Key Internals Covered**:
  - **Clustered Indexes**: Why the table *is* the primary key B+tree, and the secondary index double-lookup (bookmark lookup) penalty.
  - **Buffer Pool**: Midpoint insertion LRU list (defense against scan pollution), Change Buffer for non-unique secondary index batching, and Adaptive Flushing.
  - **Undo & Redo Logs**: Redo logs (circular WAL buffer, mini-transactions, checkpoint LSN) for durability; Undo logs (rollback segments, version chain reconstruction) for atomicity and MVCC consistent reads.
  - **Locking & Concurrency**: Record locks, Gap locks (phantom read protection), Next-Key locks, Insert Intention locks, and wait-for graph deadlock detection.
  - **Doublewrite Buffer**: Mitigating operating system torn-write pages by writing pages to a contiguous doublewrite buffer first.

### 4. [RocksDB LSM-Tree Storage Engine](System_Design_Docs/RocksDB/README.md)
* **Focus**: The architecture of write-optimized Log-Structured Merge Trees (LSM-trees).
* **Key Internals Covered**:
  - **Write Path**: Writes to the sequential WAL and the in-memory active MemTable (implemented as a lock-free SkipList).
  - **SSTables (Sorted String Tables)**: Block-based structure, prefix compression, binary search restart points, and block indexes.
  - **Leveled Storage Structure**: Level 0 (overlapping ranges, direct flush) vs. Levels 1 to N (non-overlapping key ranges, 10x size multiplier).
  - **Bloom & Ribbon Filters**: Probabilistic set membership algorithms to short-circuit negative reads and prevent random I/O.
  - **Compaction**: Leveled, Universal, and FIFO compaction strategies, write/read/space amplification trade-offs, and tombstone propagation.

---

## 📈 Key Insights & Architectural Trade-offs

Database systems are engineering trade-offs materialized. These documents compare and analyze:
* **Write Optimization vs. Read Optimization**: B-trees (PostgreSQL/InnoDB) prioritize read path simplicity (single traversal, clustered order) at the expense of random write performance. LSM-trees (RocksDB) trade read latency (multiple files to search) for extremely high sequential write throughput.
* **MVCC Implementations**: PostgreSQL chooses **append-only storage** (new tuple versions placed in the heap), which simplifies index-to-heap mapping but relies on an asynchronous `VACUUM` daemon to reclaim dead space. InnoDB chooses **in-place updates + undo log chains**, which keeps the main page compact and avoids vacuuming, but makes reads traverse index-pointer chains when looking at old snapshots.
* **Process vs. Thread Concurrency**: PostgreSQL's multi-process model offers strict connection isolation (a backend crash doesn't corrupt others) but incurs high memory and context-switching overhead. SQLite/RocksDB's embedded in-process models eliminate IPC entirely but bind execution to the calling application's lifecycle and single-writer concurrency limits.

---

*This repository is a submission for the Advanced Database Management Systems curriculum. All system design documents, diagrams, and experimental analyses contained herein represent original, independent research and architectural analysis.*
