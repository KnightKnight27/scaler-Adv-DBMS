# RocksDB Architecture: LSM-Tree Based Storage Engine

## 1. Problem Background

Modern applications generate enormous volumes of write-heavy workloads. Traditional B-Tree based databases often struggle when writes become extremely frequent because updating data requires random disk I/O.

RocksDB was developed to address this challenge. It is a high-performance embedded key-value store optimized for write-intensive workloads. Instead of updating data in place, RocksDB follows a Log Structured Merge Tree (LSM Tree) architecture.

This design significantly improves write throughput while accepting additional complexity in reads and background maintenance.

Applications such as Facebook services, distributed storage systems, stream processing engines, and metadata stores commonly use RocksDB.

---

# 2. Architecture Overview

Application
↓
MemTable
↓
Immutable MemTable
↓
SSTables (L0)
↓
SSTables (L1)
↓
SSTables (L2)
↓
...
↓
SSTables (Ln)

Supporting Components:

* WAL
* Bloom Filters
* Compaction Engine
* Block Cache

---

# 3. Internal Design

## MemTable

The MemTable is an in-memory sorted structure.

When a write arrives:

PUT(key,value)

The write is:

1. Written to WAL
2. Inserted into MemTable

Advantages:

* Extremely fast writes
* No immediate disk access

Trade-Off:

* Data is volatile until persisted

---

## Write Ahead Log (WAL)

Before data enters MemTable:

Write
↓
WAL
↓
MemTable

Purpose:

* Crash recovery
* Durability

If system crashes:

* Replay WAL
* Reconstruct MemTable

---

## Immutable MemTable

When MemTable becomes full:

MemTable
↓
Immutable MemTable

New writes continue in a fresh MemTable.

Background threads flush the Immutable MemTable to disk.

Benefits:

* Writes never stop
* Continuous ingestion

---

## SSTables

Data eventually becomes SSTables (Sorted String Tables).

Properties:

* Sorted
* Immutable
* Stored on disk

Example:

Level 0

File A:
1 → A
2 → B

File B:
3 → C
4 → D

Advantages:

* Fast sequential writes
* Simple storage format

Trade-Off:

* Multiple files must be searched

---

## Storage Levels

Level Structure:

L0
↓
L1
↓
L2
↓
...
↓
Ln

Higher levels:

* Larger files
* Better organization
* Less overlap

Purpose:

Reduce read cost over time.

---

## Bloom Filters

Problem:

Checking every SSTable is expensive.

Solution:

Bloom Filter

Before opening SSTable:

"Could key exist?"

If Bloom Filter says NO:

Skip file completely.

Benefits:

* Fewer disk reads
* Faster lookups

Trade-Off:

* Small memory overhead
* False positives possible

---

## Compaction

### Why Compaction Exists

Without compaction:

Many SSTables accumulate.

Read path becomes slow.

Compaction:

* Merges files
* Removes duplicates
* Organizes levels

Example:

L0:
A
B
C

Compaction

↓

L1:
Merged File

---

## Write Path

Write Request

↓

WAL

↓

MemTable

↓

Immutable MemTable

↓

SSTable L0

↓

Compaction

↓

Lower Levels

### Analysis

Most writes are sequential.

This minimizes random disk I/O.

---

## Read Path

Read Request

↓

MemTable

↓

Immutable MemTable

↓

Bloom Filter

↓

SSTables

↓

Return Value

### Analysis

Reads may need to check multiple locations.

This is the primary trade-off of LSM Trees.

---

# 4. Why LSM Trees Are Optimized For Writes

Traditional B-Trees:

Update data directly on disk.

Problem:

Random writes.

LSM Trees:

* Buffer writes in memory
* Batch writes to disk
* Use sequential I/O

Benefits:

* Higher write throughput
* Better SSD utilization

Trade-Off:

* Increased read complexity
* Background compaction cost

---

# 5. Compaction Trade-Offs

Benefits:

* Improves read performance
* Removes obsolete records
* Reduces storage fragmentation

Costs:

* CPU consumption
* Disk bandwidth usage
* Write amplification

### Why Compaction Can Become Expensive

Data may be rewritten multiple times across levels.

Example:

L0 → L1 → L2 → L3

Same record may be rewritten repeatedly.

This creates write amplification.

---

# 6. Performance Observations

## Write Amplification

Definition:

Amount of physical writing performed compared to logical writes.

Observation:

More aggressive compaction increases write amplification.

---

## Read Amplification

Definition:

Number of structures checked during reads.

Observation:

Without compaction, reads become slower because more SSTables must be searched.

---

## Space Amplification

Definition:

Extra storage consumed by duplicate or obsolete data.

Observation:

Less aggressive compaction can increase storage usage.

---

# 7. Design Trade-Offs

| Feature       | Benefit                    | Cost               |
| ------------- | -------------------------- | ------------------ |
| MemTable      | Fast writes                | Volatile           |
| WAL           | Durability                 | Extra write        |
| SSTables      | Sequential storage         | Read complexity    |
| Bloom Filters | Faster reads               | Memory usage       |
| Compaction    | Better organization        | CPU and I/O cost   |
| LSM Tree      | Excellent write throughput | Read amplification |

---

# 8. Key Learnings

1. RocksDB prioritizes write performance over read simplicity.
2. LSM Trees convert random writes into sequential writes.
3. WAL provides durability during crashes.
4. Bloom Filters significantly reduce unnecessary disk reads.
5. Compaction is essential for long-term performance.
6. Write amplification is the main cost of maintaining efficient storage levels.
7. Every storage engine represents a trade-off between read performance, write performance, storage efficiency, and operational complexity.
