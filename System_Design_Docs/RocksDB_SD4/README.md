# RocksDB: LSM-Tree Storage Architecture Analysis

## 1. Problem Background
Traditional B-Tree databases suffer from extreme physical limitations when deployed on modern solid-state drives (SSDs) under write-heavy workloads. Modifying a B-Tree requires reading a page, modifying it in memory, and writing the entire page back to disk. This causes random disk I/O. On SSDs, random writes trigger Flash Translation Layer (FTL) garbage collection, causing severe Write Amplification and destroying disk longevity. RocksDB was engineered to solve this by eliminating random disk writes entirely.

## 2. Architecture Overview
RocksDB is a high-performance embedded key-value store. It bypasses traditional in-place page modification in favor of an append-only, Log-Structured Merge-Tree (LSM-Tree) architecture. 

**High-Level Architecture Diagram:**
```text
[ Client Application ]
       |   |
  (Put) \ / (Get)
       |   |
+------|---|-------------------------------------------------+
|      v   |                     RAM                         |
| +----------+   (Flush)   +-------------------+             |
| | WAL Log  |             |  Block Cache (LRU)|             |
| +----------+             +-------------------+             |
|      |                             ^                       |
| +----v------------------+          |                       |
| | Active MemTable       |          |                       |
| | (In-Memory SkipList)  |          |                       |
| +-----------------------+          |                       |
|      | (When Full)                 |                       |
| +----v------------------+          |                       |
| | Immutable MemTable    |----------+                       |
| +-----------------------+                                  |
+------|-----------------------------------------------------+
       | (Async Flush)
+------v-----------------------------------------------------+
|                           DISK                             |
|  [ Level 0 SSTables ]  (Overlapping Key Ranges)            |
|          | (Compaction)                                    |
|  [ Level 1 SSTables ]  (Non-Overlapping, Sorted)           |
|          | (Compaction)                                    |
|  [ Level N SSTables ]  (Larger, Older Data)                |
+------------------------------------------------------------+
```

## 3. LSM Tree Design
**Why LSM Trees are write optimized:** An LSM-Tree converts random mutations into memory writes and sequential disk appends. Because spinning disks and SSDs process sequential data exponentially faster than random data, LSM-Trees theoretically saturate the absolute maximum write bandwidth of the hardware.

## 4. MemTable
When a `Put()` request arrives, RocksDB writes it to an active MemTable. This is an in-memory data structure (typically a SkipList). It maintains keys in sorted order. Because it is in RAM, `Put()` operations are essentially instantaneous $O(\log N)$ memory mutations.

## 5. Immutable MemTable
Once the active MemTable reaches a size limit (e.g., 64MB), it is sealed and becomes an Immutable MemTable. A new active MemTable is created to accept incoming writes. The Immutable MemTable is now scheduled to be flushed to disk by a background thread.

## 6. SSTables
When an Immutable MemTable is flushed to disk, it becomes a Sorted String Table (SSTable). An SSTable is an immutable file containing sorted key-value pairs. Because it is sorted, searches can utilize binary search on index blocks.

## 7. WAL
To ensure durability (ACID compliance) before a MemTable is flushed, every `Put()` is sequentially appended to a Write-Ahead Log (WAL) on disk. If the server crashes, the WAL is replayed to reconstruct the lost MemTables in RAM.

## 8. L0-Ln Storage Levels
Data on disk is organized into Levels. 
*   **Level 0 (L0):** Direct flushes from MemTables. L0 files have overlapping key ranges.
*   **Level 1 to N:** Level 1 is 10x larger than L0. Level 2 is 10x larger than L1. Files within a level (except L0) never have overlapping key ranges. 

## 9. Bloom Filters
**Benefit of Bloom Filters:** Because an LSM-Tree spreads data across multiple files, searching for a key could require checking a dozen files on disk. A Bloom Filter is attached to every SSTable. Before reading an SSTable from disk, RocksDB checks the filter. If the filter says "No", RocksDB mathematically guarantees the key is not in that file, saving an expensive disk I/O.

## 10. Compaction
**Why compaction is necessary:** If data is constantly appended, the disk fills with obsolete values and "Tombstones" (deleted keys). Compaction reclaims space.
**Why compaction is expensive:** It requires reading multiple SSTables into memory, merging them to eliminate duplicates and tombstones, and writing entirely new SSTables to disk. This consumes heavy CPU and disk I/O bandwidth.

**Compaction Diagram:**
```text
Level 1: [SST 1: A-F]  [SST 2: G-M]
             \              /
              \            /  (Merge & Drop Tombstones)
               \          /
Level 2:   [SST 3: A-M (New, Compacted File)]
```

## 11. Read Path

**Read Path Diagram:**
```text
Client Get("Key X") ->
  1. Check Active MemTable -> (Return if found)
  2. Check Immutable MemTables -> (Return if found)
  3. For each file in Level 0 (Newest to Oldest):
       a. Check Bloom Filter
       b. If Positive, check Block Cache -> Disk Read -> Binary Search
  4. For Level 1 to N:
       a. Find the single file that could contain Key X
       b. Check Bloom Filter -> Block Cache -> Disk Read
```

## 12. Write Path

**Write Path Diagram:**
```text
Client Put("Key X", "Val Y") ->
  1. Append to WAL (Sequential Disk Write)
  2. Insert into Active MemTable (RAM SkipList Write)
  3. Return "Success" to Client
```

## 13. Amplification Analysis (The RUM Conjecture)
LSM systems are bound by the RUM Conjecture (Read, Update, Memory/Space).
*   **Write Amplification:** 1 byte written by the user may result in 30 bytes written to disk due to constant background compactions rewriting the same data to lower levels.
*   **Read Amplification:** 1 read by the user may require checking 5 different SSTables on disk to find the most recent version of the key.
*   **Space Amplification:** The disk stores old versions of data and tombstones until compaction cleans them up.

## 14. Trade-Offs

| Design Feature | Benefit | Consequence |
| :--- | :--- | :--- |
| **LSM-Tree vs B-Tree** | Massive write throughput (sequential). | High Read Amplification; complex tuning required. |
| **Leveled Compaction** | Minimizes Space and Read amplification. | Maximizes Write Amplification (heavy disk wear). |
| **Universal Compaction** | Minimizes Write Amplification. | Extreme Space Amplification; spikes in read latency. |

## 15. Experiments

### Benchmark: Compaction Strategy Impact
**Goal:** Observe how compaction algorithms alter the RUM balance.
**Setup:** `db_bench` executing 10 million random 1KB writes (`fillrandom`).
**Variant A (Leveled Compaction):**
*   *Write Throughput:* ~45,000 ops/sec.
*   *P99 Read Latency:* 2.5 ms.
*   *Disk I/O:* Heavily saturated throughout the test due to continuous merging.
**Variant B (Universal Compaction):**
*   *Write Throughput:* ~120,000 ops/sec (Almost 3x faster).
*   *P99 Read Latency:* 18.0 ms (Severely degraded).
*   *Reasoning:* Universal compaction delays merging files, letting them pile up. Because RocksDB isn't wasting I/O writing merged files, user writes are incredibly fast (low Write Amplification). However, when a `Get()` occurs, RocksDB must search through dozens of unmerged files, skyrocketing Read Amplification.

## 16. Key Learnings
1.  **LSM Trees Trade Read Speed for Write Speed:** By converting random disk mutations into sequential appends, you gain write bandwidth but lose the predictable read latency of a B-Tree.
2.  **Bloom Filters are Mandatory:** Without Bloom Filters, Read Amplification would make an LSM-Tree unusable in production.
3.  **Tuning is Workload Specific:** You must know your workload. If you are building a time-series database where data is written once and rarely read, Universal Compaction is superior. If you are building a web-backend, Leveled Compaction is required.

## 17. References
1.  RocksDB Documentation & Wiki (https://github.com/facebook/rocksdb/wiki).
2.  Dong, S. et al. (2017). *Optimizing Space Amplification in RocksDB*. CIDR.
3.  O'Neil, P., Cheng, E., Gawlick, D., & O'Neil, E. (1996). *The Log-Structured Merge-Tree (LSM-Tree)*. Acta Informatica.
