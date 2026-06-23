# Advanced Database Management Systems (CS-504)
## System Design Discussion Documents

Welcome to the System Design Discussion submissions repository for CS-504: Advanced DBMS. This repository contains detailed architectural analyses, design trade-off evaluations, internal data-flow studies, and practical query-plan/benchmark experiments for modern database engines.

The submissions have been structured according to the course guidelines inside the `System_Design_Docs/` directory.

---

## 📂 Submission Directory Structure

```
System_Design_Docs/
├── PostgreSQL_vs_SQLite/   # Topic 1: Architecture Comparison (Client-Server vs Embedded)
│   └── README.md
├── PostgreSQL_Internals/   # Topic 2: Storage, Buffer Sweep, Lehman-Yao B-Trees, MVCC, and WAL
│   └── README.md
├── MySQL_InnoDB/           # Topic 3: Clustered Indexing, Log Management, and MVCC Differences
│   └── README.md
└── RocksDB/                # Topic 4: LSM-Trees, Bloom Filters, and Amplification Benchmarks
    └── README.md
```

---

## 🚀 Navigation & Topic Index

### [Topic 1: PostgreSQL vs SQLite Architecture Comparison](file:///C:/Users/ankit/.gemini/antigravity/scratch/System_Design_Docs/PostgreSQL_vs_SQLite/README.md)
* **Focus**: Process model comparison (multi-process client-server daemon vs. in-process single-file library), storage engines (heap files vs. B+Tree clustered files), concurrency lock escalation, and write-ahead logging (WAL mode vs. rollback journals).
* **Key Visuals**: PostgreSQL multi-process diagram, SQLite VDBE & VFS design flow, slotted heap vs. SQLite cell layouts.
* **Benchmark Analysis**: Connection count scaling under write workloads and RAM utilization comparisons.

### [Topic 2: PostgreSQL Internal Architecture](file:///C:/Users/ankit/.gemini/antigravity/scratch/System_Design_Docs/PostgreSQL_Internals/README.md)
* **Focus**: Buffer manager clock sweep replacement, Lehman & Yao right-link B-Trees (`nbtree` page splits), MVCC visibility rules (tuple headers, xmin/xmax, snapshot isolation), and WAL torn-page protection (full-page writes).
* **Case Study**: Detailed analysis of a multi-table join plan (`EXPLAIN ANALYZE`), highlighting hash joins, memoization, index scans, selectivity estimation, and `pg_statistic` histogram usage.

### [Topic 3: MySQL / InnoDB Storage Engine](file:///C:/Users/ankit/.gemini/antigravity/scratch/System_Design_Docs/MySQL_InnoDB/README.md)
* **Focus**: Index-organized clustered tables, secondary index double-seek penalty, Buffer Pool structures (LRU midpoint sublist separation, change buffer, doublewrite buffer), redo log durability, and undo log rollback segments.
* **Locking Internals**: Record locks, Gap locks, and Next-key locks for preventing phantom reads in Repeatable Read isolation.
* **Comparison**: Comparative table evaluating InnoDB (in-place MVCC with Undo rollback) against PostgreSQL (append-only MVCC with Vacuuming).

### [Topic 4: RocksDB Architecture](file:///C:/Users/ankit/.gemini/antigravity/scratch/System_Design_Docs/RocksDB/README.md)
* **Focus**: Log-Structured Merge-tree (LSM-tree) lifecycle (MemTable SkipLists, Immutable MemTables, WAL, leveled disk levels), block-based SSTable file specifications, and Bloom filter membership testing.
* **Paths**: Detailed flow of get/put operations.
* **Benchmark Analysis**: Empirical comparison of Leveled Compaction, Size-Tiered Compaction, and FIFO Compaction under Write Amplification (WA), Read Amplification (RA), and Space Amplification (SA) constraints.

---

## 🎯 Verification and Conceptual Alignment
All documents are structured around answering:
1. *Why* the system was designed this way (design motivations).
2. *What* physical storage layouts, memory structures, and transaction locks implement the design.
3. *What* trade-offs were accepted during implementation (performance, durability, complexity).
4. *How* these design decisions directly impact observable query planner performance and benchmark metrics.
