# RocksDB: LSM-Tree Storage Engine

**Advanced Database Management Systems (ADBMS)**
**System Design Discussion**

**Author:** Praneeth Budati
**Roll No:** 24BCS10081

---

# Introduction

RocksDB is an embedded key-value storage engine built on the Log-Structured Merge Tree (LSM-Tree) architecture. Unlike traditional B-Tree databases that perform updates directly on disk pages, RocksDB buffers writes in memory and later writes them to disk sequentially.

This design makes RocksDB highly efficient for write-heavy workloads and SSD-based storage systems.

---

# Background

RocksDB was developed by Facebook as an extension of Google's LevelDB.

It is widely used as a storage engine in systems such as:

* MyRocks
* TiKV
* CockroachDB
* Kafka Streams

Unlike PostgreSQL or MySQL, RocksDB is not a database server. It is a storage library that applications embed directly into their processes.

---

# Why LSM Trees?

Traditional B-Trees perform in-place updates.

Problems:

* Random disk writes
* High write amplification
* Reduced SSD lifespan

LSM Trees solve this by:

1. Writing data to memory first.
2. Appending changes to a log.
3. Flushing data to disk sequentially.
4. Merging files later using compaction.

This converts expensive random writes into efficient sequential writes.

---

# High-Level Architecture

The main components of RocksDB are:

1. MemTable
2. Write-Ahead Log (WAL)
3. SSTables
4. Bloom Filters
5. Compaction Engine
6. Block Cache

These components work together to optimize write performance while maintaining durability.

---

# Write Path

When a client performs a PUT operation:

1. Data is appended to the WAL.
2. Data is inserted into the active MemTable.
3. The write is acknowledged.
4. Background threads later flush data to disk.

Process:

Write Request
↓
WAL
↓
MemTable
↓
SSTable (L0)
↓
Compaction

This approach allows low write latency.

---

# Read Path

To find a key:

1. Search the active MemTable.
2. Search immutable MemTables.
3. Search Level-0 SSTables.
4. Search lower levels if needed.

Bloom filters help eliminate unnecessary file lookups.

Benefits:

* Faster reads
* Reduced disk access
* Lower read amplification

---

# MemTable

The MemTable is an in-memory sorted data structure.

Characteristics:

* Receives all incoming writes
* Typically implemented using skip lists
* Maintains key ordering
* Supports efficient range scans

When it reaches a size limit, it becomes immutable and is flushed to disk.

---

# Write-Ahead Log (WAL)

The WAL ensures durability.

Every write is first recorded in the log before being placed into memory.

Advantages:

* Crash recovery
* Fast commits
* Sequential disk writes

If a crash occurs, RocksDB reconstructs the MemTable by replaying the WAL.

---

# SSTables

SSTable stands for Sorted String Table.

Characteristics:

* Immutable
* Sorted by key
* Stored on disk
* Created during MemTable flushes

Each SSTable contains:

| Component    | Purpose                |
| ------------ | ---------------------- |
| Data Blocks  | Actual key-value pairs |
| Index Block  | Fast block lookup      |
| Filter Block | Bloom filters          |
| Footer       | Metadata               |

Because SSTables never change, readers do not require heavy locking.

---

# LSM Levels

Data in RocksDB is organized into levels.

Level-0:

* New SSTables arrive here.
* Files may overlap.

Levels 1 and below:

* Files are sorted.
* Key ranges do not overlap.

Example:

L0 → L1 → L2 → L3 → ...

Each level is typically much larger than the previous one.

---

# Bloom Filters

Bloom filters are probabilistic data structures used to test whether a key may exist inside an SSTable.

Possible results:

* Definitely Not Present
* Possibly Present

Advantages:

* Avoid unnecessary disk reads
* Improve lookup performance
* Reduce read amplification

False positives are possible, but false negatives are not.

---

# Compaction

Compaction is one of the most important processes in RocksDB.

Purpose:

* Merge SSTables
* Remove duplicate versions
* Delete obsolete records
* Reduce read amplification
* Maintain level organization

Without compaction, performance would degrade over time.

---

# Compaction Strategies

## Leveled Compaction

Advantages:

* Lower read amplification
* Lower space amplification

Disadvantages:

* Higher write amplification

## Universal Compaction

Advantages:

* Lower write amplification
* Better for heavy write workloads

Disadvantages:

* Higher storage usage
* Higher read amplification

---

# Read, Write and Space Amplification

RocksDB involves three important trade-offs.

## Write Amplification

Extra bytes written during compaction.

## Read Amplification

Additional files or blocks accessed during reads.

## Space Amplification

Extra storage occupied by old versions and temporary files.

Improving one often increases the others.

---

# Advantages of RocksDB

* Excellent write performance
* SSD-friendly architecture
* Efficient sequential writes
* Supports snapshots
* Highly configurable
* Widely used in distributed systems

---

# Limitations of RocksDB

* Compaction overhead
* Higher read complexity
* Storage overhead between compactions
* No built-in SQL support
* Requires tuning for optimal performance

---

# RocksDB vs B-Tree Databases

| Feature           | RocksDB (LSM)         | PostgreSQL/InnoDB (B-Tree) |
| ----------------- | --------------------- | -------------------------- |
| Write Strategy    | Append + Compact      | In-place Updates           |
| Write Performance | High                  | Moderate                   |
| Read Performance  | Moderate              | High                       |
| Compaction        | Required              | Not Required               |
| Storage Model     | Immutable SSTables    | Mutable Pages              |
| Best For          | Write-heavy workloads | Read-heavy workloads       |

---

# Key Learnings

1. RocksDB converts random writes into sequential writes.
2. MemTables absorb incoming write traffic.
3. SSTables provide immutable disk storage.
4. Bloom filters reduce unnecessary reads.
5. Compaction is essential for maintaining performance.
6. LSM Trees optimize write throughput by accepting higher background maintenance costs.
7. Choosing between RocksDB and B-Tree databases depends on workload characteristics.

---

# Conclusion

RocksDB is a highly optimized storage engine designed for modern SSD-based systems and write-intensive workloads. Its LSM-tree architecture enables fast write performance by buffering updates in memory and storing data as immutable SSTables. Although compaction introduces additional complexity and overhead, the resulting write efficiency makes RocksDB a popular choice for large-scale distributed systems and storage platforms.

Understanding RocksDB also highlights the broader trade-off between write-optimized LSM Trees and read-optimized B-Tree databases, which remains one of the most important design choices in modern database systems.

---

# References

1. RocksDB Official Documentation
2. RocksDB Wiki
3. The Log-Structured Merge-Tree (LSM-Tree) Paper
4. LevelDB Documentation
5. MyRocks Documentation
6. CockroachDB Storage Documentation
