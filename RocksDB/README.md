# RocksDB Architecture Analysis

## 1. Problem Background

Traditional relational databases often use B-Tree based storage structures. While B-Trees provide excellent read performance, they can become inefficient for write-heavy workloads because data must frequently be updated in place on disk.

As modern systems began generating massive amounts of data through logs, analytics pipelines, social media platforms, and distributed systems, database engineers needed a storage engine optimized for extremely high write throughput.

RocksDB was developed by Facebook as an embedded key-value storage engine based on the Log Structured Merge Tree (LSM Tree) architecture. It was designed to support workloads involving frequent writes, large datasets, and fast storage devices such as SSDs.

Today, RocksDB is used in distributed databases, caching systems, stream processing frameworks, and large-scale storage platforms where write performance is critical.

---

# 2. Architecture Overview

## High-Level Architecture

```text
Application
      |
      v
+----------------+
|    RocksDB     |
+----------------+
      |
      +----------------+
      |   MemTable     |
      +----------------+
      |
      +----------------+
      | Immutable MT   |
      +----------------+
      |
      +----------------+
      |   SSTables     |
      +----------------+
      |
      +----------------+
      | Compaction     |
      +----------------+
      |
      +----------------+
      | Bloom Filters  |
      +----------------+
      |
      +----------------+
      | WAL            |
      +----------------+
```

Unlike PostgreSQL or MySQL, RocksDB is an embedded storage engine. Applications communicate with RocksDB through a library rather than a separate database server.

The architecture focuses on optimizing sequential writes and minimizing random disk I/O.

---

# 3. Internal Design

## MemTable

The MemTable is an in-memory data structure that stores newly written records.

When an application writes data:

1. Data is first written to the WAL.
2. Data is inserted into the MemTable.
3. Reads can access recent records directly from memory.

Because writes occur in memory, insertion operations are extremely fast.

Advantages:

- Fast write performance.
- Low write latency.
- Reduced disk access.

---

## Write Ahead Log (WAL)

Before data enters the MemTable, RocksDB records the operation in the Write Ahead Log.

Example:

```text
PUT user123 = Active
```

This record is written to the WAL before being acknowledged.

If the system crashes before the MemTable is flushed to disk, RocksDB can replay the WAL and recover lost updates.

The WAL provides durability guarantees similar to those found in PostgreSQL and InnoDB.

---

## Immutable MemTable

The MemTable has a limited size.

Once it becomes full:

```text
MemTable
    ↓
Immutable MemTable
```

The structure becomes read-only and waits to be flushed to disk.

A new MemTable is immediately created to continue accepting writes.

This design prevents write operations from stopping while disk flushes occur.

---

## SSTables

When an Immutable MemTable is flushed, data is written to disk as an SSTable (Sorted String Table).

Characteristics:

- Immutable.
- Sorted by key.
- Optimized for sequential access.

Example:

```text
user1
user2
user3
user4
user5
```

Because SSTables are immutable, RocksDB avoids expensive in-place updates.

---

## LSM Tree Levels

RocksDB organizes SSTables into multiple levels.

```text
L0
L1
L2
L3
...
Ln
```

Characteristics:

### Level 0

- Receives newly flushed SSTables.
- Files may overlap.

### Lower Levels

- Larger storage capacity.
- Files are merged and organized.
- Less overlap.

As data moves through the levels, storage becomes increasingly optimized.

---

## Compaction

Compaction is one of the most important processes in RocksDB.

Purpose:

- Merge SSTables.
- Remove duplicate keys.
- Remove deleted records.
- Reduce read amplification.

Example:

Before:

```text
L0:
A=10
A=15
```

After compaction:

```text
L1:
A=15
```

The older version is discarded.

Advantages:

- Faster reads.
- Better storage efficiency.

Disadvantages:

- Consumes CPU.
- Consumes disk bandwidth.
- Can temporarily affect performance.

---

## Bloom Filters

A Bloom Filter is a probabilistic data structure used to reduce unnecessary disk reads.

Without Bloom Filters:

```text
Check SSTable 1
Check SSTable 2
Check SSTable 3
```

With Bloom Filters:

```text
Definitely not present
```

The search can stop immediately.

Benefits:

- Faster lookups.
- Reduced disk access.
- Improved read performance.

Bloom Filters may occasionally produce false positives but never false negatives.

---

# 4. Read Path

When RocksDB processes a read request:

1. Check MemTable.
2. Check Immutable MemTable.
3. Check Bloom Filters.
4. Search SSTables.

Data may exist in multiple levels due to the LSM design.

This increases read complexity compared to B-Tree databases.

---

# 5. Write Path

Write operations follow this sequence:

```text
Application
      ↓
WAL
      ↓
MemTable
      ↓
Immutable MemTable
      ↓
SSTable (L0)
      ↓
Compaction
      ↓
Lower Levels
```

Because writes are sequential and mostly append-only, RocksDB achieves very high write throughput.

---

# 6. Amplification Trade-Offs

## Write Amplification

Data may be rewritten multiple times during compaction.

Example:

```text
Write once
Move to L0
Move to L1
Move to L2
```

A single record may be written several times.

This increases SSD wear and disk activity.

---

## Read Amplification

Data may exist across several levels.

A read request may need to examine multiple files before locating the desired record.

Bloom Filters help reduce this overhead.

---

## Space Amplification

Temporary duplicate versions of data may exist before compaction finishes.

This increases storage consumption.

Compaction eventually removes redundant copies.

---

# 7. Design Trade-Offs

## Advantages

- Extremely fast writes.
- Efficient SSD utilization.
- High throughput.
- Excellent scalability.
- Strong durability through WAL.

## Limitations

- More complex read path.
- Expensive compaction operations.
- Higher write amplification.
- Greater tuning complexity.

---

# 8. Why LSM Trees Are Preferred for Write-Heavy Workloads

B-Tree systems perform many random disk updates.

RocksDB converts random writes into sequential writes.

This dramatically improves performance when:

- Logs are continuously generated.
- Large volumes of events are stored.
- Write throughput is more important than read latency.

For this reason, many distributed databases and storage systems adopt LSM Tree architectures.

---

# 9. Key Learnings

- RocksDB is built around the LSM Tree architecture.
- Writes first enter memory before being flushed to disk.
- WAL guarantees durability during crashes.
- SSTables are immutable and sorted.
- Compaction is necessary to maintain performance and storage efficiency.
- Bloom Filters reduce unnecessary disk reads.
- RocksDB sacrifices some read simplicity in exchange for exceptional write performance.
- Database architecture always involves trade-offs between reads, writes, storage efficiency, and operational complexity.

---

# References

1. RocksDB Official Documentation
2. Facebook Engineering Blog
3. LSM Tree Research Papers
4. RocksDB Tuning Guide

---

## Author

**Varun Uday Shet**  
Roll Number: SCALER - 24BCS10518
Advanced DBMS – System Design Discussion Assignment
