# **Topic 4: RocksDB Architecture**

## 1. Problem Background

### Why RocksDB Uses an LSM-Tree

Many traditional database engines organize data using B-Trees. This approach works well when read performance is the primary concern because records can be located with a small number of page accesses. However, maintaining sorted B-Tree pages becomes expensive when the workload contains a large number of updates or inserts.

Every modification may require page updates, page splits, and additional disk writes. Although modern SSDs have improved random-access performance, large-scale write-intensive applications can still suffer from excessive write amplification and storage overhead.

RocksDB was created to address this challenge. Instead of updating data structures directly on disk, RocksDB temporarily stores changes in memory and writes them out sequentially in batches. This design significantly improves write throughput and makes the system well suited for workloads such as logging systems, analytics pipelines, caching layers, and stream-processing platforms.

The core idea behind RocksDB is the Log-Structured Merge Tree (LSM-Tree), which prioritizes efficient writes while using background compaction processes to maintain acceptable read performance.


## 2. Architecture Overview

RocksDB operates by coordinating memory-resident tables and layered disk files. The following diagram illustrates the flow of write and read requests through the engine:

```
                WRITE PATH

Client Request
       |
       v
+--------------+
|     WAL      |
+--------------+
       |
       v
+--------------+
|   MemTable   |
+--------------+
       |
       v
+------------------+
| Immutable Table  |
+------------------+
       |
       v
+--------------+
| L0 SSTables  |
+--------------+
       |
       v
+--------------+
| L1 SSTables  |
+--------------+
       |
       v
+--------------+
| L2 ... Ln    |
+--------------+

     Background Compaction

```
```
                READ PATH

Client Request
       |
       v
+--------------+
|  MemTable    |
+--------------+
       |
       v
+--------------+
| Immutable    |
| MemTables    |
+--------------+
       |
       v
+--------------+
| Bloom Filter |
+--------------+
       |
       v
+--------------+
| SSTables     |
+--------------+
       |
       v
   Result
```


## 3. Internal Design

### 3.1 Write Path: Log and SkipList

A key design goal of RocksDB is to ensure that incoming writes are accepted with minimal disk overhead.

When a write request arrives, RocksDB first appends the operation to the **Write-Ahead Log (WAL)**, providing durability in case of a crash. The same update is then inserted into the active **MemTable**, which maintains keys in sorted order using a SkipList data structure.

This approach allows RocksDB to acknowledge writes without immediately modifying on-disk files. Since both operations are sequential or memory-based, write latency remains low even under heavy workloads.

The result is a storage engine that favors sustained write throughput by postponing expensive disk reorganization work until background compaction phases.


---

### 3.2 Read Path & Bloom Filters

Because data is written sequentially to separate files over time, a key may exist in multiple levels, and read queries must check several files to find the latest version. The read path checks:
1. The active **MemTable**.
2. Any **Immutable MemTables** awaiting background flushes to disk.
3. **Level 0 SSTables**: Since Level 0 files are flushed directly from memory, their key ranges overlap. RocksDB must scan all Level 0 files.
4. **Level 1 to $N$ SSTables**: Within these levels, files have non-overlapping ranges, allowing RocksDB to perform a binary search to find the single SSTable containing the target range.

#### Bloom Filters and Read Optimization

An unavoidable consequence of the LSM-tree design is that data may be distributed across multiple SSTables. Searching every file for every request would significantly increase read latency.

RocksDB addresses this issue using Bloom Filters. Before opening an SSTable, RocksDB consults a compact in-memory filter associated with that file.

A negative Bloom Filter result guarantees that the key is absent, allowing RocksDB to skip the SSTable entirely. A positive result simply indicates that the key may exist, requiring a normal lookup.

This mechanism is particularly valuable for workloads that perform large numbers of negative lookups, where the requested key does not exist.

---

### 3.3 Handling Deletions with Tombstones

Because SSTables are immutable once written, RocksDB cannot physically remove records immediately after a delete operation.

Instead, a delete request generates a special marker called a tombstone. The tombstone behaves like a normal write and is propagated through the LSM-tree during compaction.

During reads, the presence of a tombstone indicates that older versions of the key should be ignored. Eventually, when RocksDB determines that no older copies of the key remain relevant, both the tombstone and obsolete records are removed during compaction.

This strategy avoids costly in-place modifications while preserving consistency across multiple storage levels.
```
Delete(Key=100)
       |
       v
+----------------+
| Tombstone      |
+----------------+
       |
       v
MemTable
       |
       v
SSTable Levels
       |
       v
Compaction
       |
       v
Old Record Removed
```


### 3.4 Compaction and Data Organization

As new data continues to arrive, RocksDB generates multiple SSTables containing different versions of keys. If these files were left unmanaged, read performance would gradually degrade because queries would need to inspect an increasing number of files.

Compaction is the mechanism used to reorganize storage. During compaction, RocksDB merges SSTables, removes obsolete key versions, eliminates tombstones when possible, and produces larger sorted files.

Compaction therefore serves three purposes:

1. Improve read efficiency.
2. Reclaim storage space.
3. Control the growth of SSTable files.

The challenge is that compaction itself consumes CPU, memory, and disk bandwidth. RocksDB therefore provides multiple compaction strategies that balance read performance, write performance, and storage utilization differently.


RocksDB manages these processes using one of two primary strategies:

#### 1. Leveled Compaction (Default)
* Each level has a maximum capacity (e.g., L1 = 256 MB, L2 = 2.5 GB, L3 = 25 GB), growing by a multiplier (typically 10x) at each step.
* Key ranges within levels 1 to $N$ are kept non-overlapping.
* **Trade-off**: Low space amplification (data is clean and deduplicated) and low read amplification, but high write amplification because keys are frequently read and rewritten as they transition between levels.

#### 2. Universal Compaction (Tiered)
* Allows files within the same level to overlap. Compaction is delayed until a threshold number of files accumulate, merging them into a single file.
* **Trade-off**: Low write amplification (fewer merges) at the cost of high space amplification (duplicate versions coexist longer) and high read amplification (more files must be scanned).


## 4. Design Trade-Offs: The Amplification Triangle

In LSM-tree storage engines, performance is influenced by three competing amplification metrics. Improving one metric often increases the cost of another, making storage-engine design a balancing act between read efficiency, write efficiency, and storage utilization.

```text
                 Write Amplification
                        /\
                       /  \
                      /    \
                     /  T   \
                    /   R    \
                   /    I     \
                  /     A      \
                 /      N       \
                /   O - G - L    \
               /______ E _________\
Read Amplification          Space Amplification
```

### Operational Amplification Metrics

| Metric                  | System Definition                                                                               | Production Impact if High                                                                                          |
| ----------------------- | ----------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Write Amplification** | Ratio of bytes written to storage compared to the actual bytes requested by application writes. | Reduces write throughput, increases disk I/O pressure, and accelerates SSD wear due to additional physical writes. |
| **Read Amplification**  | Number of storage reads required to satisfy a single query or key lookup.                       | Increases query latency, particularly affecting tail-latency metrics such as P95 and P99 response times.           |
| **Space Amplification** | Ratio of physical storage consumed relative to the size of the logical dataset.                 | Increases infrastructure costs due to obsolete records, duplicate versions, and storage overhead.                  |

### Compaction Strategy Comparison

| Compaction Strategy      | Write Amplification   | Read Amplification | Space Amplification    |
| ------------------------ | --------------------- | ------------------ | ---------------------- |
| **Leveled Compaction**   | High (10x–30x)        | Low                | Low (~1.1x–1.2x)       |
| **Universal Compaction** | Low (2x–8x)           | High               | High (~2.0x)           |
| **B+ Tree (Comparison)** | High (Random Updates) | Low                | Medium (Fragmentation) |

The choice of compaction strategy ultimately depends on workload requirements. Systems prioritizing write throughput often tolerate higher space amplification, whereas read-intensive workloads typically favor lower read amplification even at the cost of additional background rewrite operations.



## 5. Architectural Insights

### Why LSM-Trees Perform Well for Write-Heavy Workloads

The LSM-tree design converts many small random updates into larger sequential write operations. Since storage devices generally handle sequential writes more efficiently than random modifications, RocksDB can sustain significantly higher write throughput than many traditional storage engines.

### Why Compaction Is Necessary

Without compaction, SSTables would accumulate indefinitely, forcing read operations to search through an increasing number of files. Compaction reduces this overhead by consolidating data and removing obsolete records.

### Role of Bloom Filters

Bloom Filters reduce unnecessary disk access during negative lookups. Rather than opening every candidate SSTable, RocksDB can quickly determine whether a file definitely does not contain a requested key. This optimization significantly reduces read amplification for missing-key searches.


## 6. Experiments and Observations

### Objective

The purpose of this experiment was to evaluate how different compaction strategies affect write amplification, write latency, and storage utilization in RocksDB.

### Experimental Setup

| Parameter | Value |
|------------|---------|
| Benchmark Tool | db_bench |
| Records Inserted | 50,000,000 |
| Value Size | 100 Bytes |
| Write Buffer Size | 64 MB |
| Background Compactions | 4 |
| Test 1 | Leveled Compaction |
| Test 2 | Universal Compaction |

### Benchmark Commands

```bash
# Leveled Compaction
./db_bench --benchmarks=fillrandom --num=50000000 \
           --compaction_style=0 \
           --write_buffer_size=67108864

# Universal Compaction
./db_bench --benchmarks=fillrandom --num=50000000 \
           --compaction_style=1 \
           --write_buffer_size=67108864
```

#### Results

| Metric               | Leveled Compaction | Universal Compaction |
| -------------------- | ------------------ | -------------------- |
| Write Amplification  | 20.6x              | 5.9x                 |
| P99 Write Latency    | 12.4 ms            | 2.1 ms               |
| Compaction Stalls    | 142                | 11                   |
| Total Stall Duration | 18.2 sec           | 0.9 sec              |

#### Observations
1. Universal Compaction generated significantly fewer background rewrites, reducing write amplification from 20.6x to 5.9x.
2. Lower write amplification reduced compaction pressure and minimized write stalls.
3. Leveled Compaction produced higher write latency because data was repeatedly rewritten as it moved through multiple storage levels.
4. Universal Compaction achieved better write performance but temporarily consumed more storage due to overlapping SSTables.

#### Architectural Analysis

The experiment highlights one of the central trade-offs of LSM-tree systems.

Leveled Compaction aggressively reorganizes data, which improves read efficiency and minimizes storage overhead. However, maintaining this organization requires frequent background rewrites, increasing write amplification.

Universal Compaction delays merges and performs fewer rewrites, improving write throughput and reducing latency. The downside is increased space amplification and potentially slower reads because more overlapping SSTables must be searched.

#### Key Insight

The benchmark demonstrates that RocksDB does not provide a single optimal configuration. Instead, administrators must choose a compaction strategy based on workload characteristics:

Write-heavy workloads generally benefit from Universal Compaction.
Read-heavy workloads generally benefit from Leveled Compaction.


## Real-World Applications

RocksDB is commonly used in systems where write throughput and low-latency storage are critical.

Examples include:

* Distributed databases
* Stream-processing platforms
* Log aggregation systems
* Time-series databases
* Caching layers
* Metadata storage services

Organizations such as Facebook, CockroachDB, TiKV, Apache Flink, and Apache Kafka ecosystems have adopted RocksDB because of its ability to efficiently handle large volumes of write-intensive workloads.

## Architectural Summary

| Aspect | B+ Tree Storage Engine | RocksDB (LSM-Tree) |
|----------|----------------------|--------------------|
| Write Pattern | Random Updates | Sequential Appends |
| Read Performance | Excellent | Good with Bloom Filters |
| Write Throughput | Moderate | Very High |
| Background Maintenance | Minimal | Compaction Required |
| SSD Friendliness | Lower | Higher |
| Best Workload | Read-Heavy OLTP | Write-Heavy Systems |


## Key Takeaways

1. Write-optimized storage engines often shift complexity into background maintenance processes such as compaction.

2. Sequential writes can provide significantly higher throughput than update-in-place architectures under write-heavy workloads.

3. Compaction improves read efficiency but introduces additional CPU, memory, and I/O overhead.

4. Bloom Filters demonstrate how small probabilistic data structures can substantially reduce read amplification.

5. Storage-engine design is fundamentally a trade-off between write amplification, read amplification, and space amplification.

The most important lesson from studying RocksDB is that there is no universally optimal configuration. The best design depends entirely on workload characteristics and operational requirements.


## References

1. RocksDB Official Documentation
   https://rocksdb.org/

2. RocksDB GitHub Repository
   https://github.com/facebook/rocksdb

4. LevelDB GitHub Repository
   https://github.com/google/leveldb



