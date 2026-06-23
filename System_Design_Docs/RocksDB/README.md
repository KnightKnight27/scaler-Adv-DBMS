# Topic 4: RocksDB Architecture

This report explores the architecture of RocksDB, a high-performance embedded key-value storage engine based on Log-Structured Merge Trees (LSM-trees). It details memory buffers, on-disk storage layout, write and read path logic, compaction strategies, and the trade-offs of LSM-tree architectures.

---

## 1. Problem Background

Traditional database engines (like InnoDB or PostgreSQL) use B-Trees, which update data pages in-place. While B-Trees provide fast reads, writes require modifying arbitrary pages on disk, resulting in random disk I/O bottlenecks. 

RocksDB was developed by Facebook in 2012 (forked from Google's LevelDB) to optimize database engines for write-heavy workloads. By using an LSM-tree design, RocksDB converts random writes into sequential log appends and sorted flushes. This reduces write latency and matches the write performance of modern SSDs.

---

## 2. Architecture Overview

RocksDB operates as an in-process library. Its architecture coordinates in-memory buffers and layered disk files:

```
               WRITE PATH                               READ PATH
               ==========                               =========

             +------------+                            +-----------+
             | Write API  |                            | Read API  |
             +-----+------+                            +-----+-----+
                   |                                         |
         +---------+---------+                      +--------+--------+
         |                   |                      |                 |
         v                   v                      v (Step 1)        |
  +------------+      +------------+          +-----------+           |
  | Write-Ahead|      |  Active    |          |  Active   |           |
  | Log (WAL)  |      |  MemTable  |          | MemTable  |           |
  +------------+      +-----+------+          +-----+-----+           |
                            |                       | (Step 2: Miss)  |
                            v                       v                 |
                      +------------+          +-----------+           |
                      | Immutable  |          | Immutable |           v (Step 4: Search levels)
                      |  MemTable  |          | MemTable  |     +------------+
                      +-----+------+          +-----+-----+     | L0 SSTables| (Bloom filter check)
                            |                       |           +-----+------+
                            v (Flush)               v (Step 3)        |
  --------------------------|-----------------------|-----------------|---------
  DISK                      |                       |                 v (Step 5)
                            v                       v           +------------+
                      +------------+          +-----------+     | L1 SSTables| (Binary search page)
                      | L0 Levels  |--------->|   Block   |     +------------+
                      | (SSTables) |          |   Cache   |           |
                      +------------+          +-----------+           v (Step 6)
                            |                                   +------------+
                            v (Compaction)                      | L2..Ln     |
                      +------------+                            +------------+
                      | L1..Ln     |
                      | (SSTables) |
                      +------------+
```

- **Active MemTable**: An in-memory data structure (typically a Skiplist) that receives active writes.
- **Immutable MemTable**: A read-only MemTable containing data queued to be flushed to disk.
- **Write-Ahead Log (WAL)**: A sequential redo log file on disk to guarantee durability.
- **SSTables (Sorted String Tables)**: Layered, sorted data files representing the primary disk storage.
- **Block Cache**: Memory area caching uncompressed data blocks read from disk.

---

## 3. Internal Design

### In-Memory Buffers (MemTable)

- **Write Processing**: When a write query is received, RocksDB appends it sequentially to the disk-based WAL and inserts the key-value pair into the active **MemTable**.
- **SkipList Structure**: The MemTable is typically implemented as a lock-free **SkipList**, which provides $O(\log N)$ search, insertion, and deletion complexity under concurrent access.
- **Flush Trigger**: When the active MemTable reaches its configured size limit (e.g., 64MB), it is marked as an **Immutable MemTable**, and a new active MemTable is initialized. A background flush thread copies the immutable MemTable contents to disk as a Level 0 (L0) SSTable.

---

### On-Disk Files (SSTables)

- **SSTable Format**: Sorted String Tables (SSTables) are structured into blocks of sorted key-value pairs:

```
+-------------------------------------------------------------------------+
| Data Block 1 | Data Block 2 | ... | Filter Block | Index Block | Footer |
+-------------------------------------------------------------------------+
```

- **Index Block**: Contains offset and size pointers for each data block, allowing binary search lookups.
- **Filter Block (Bloom Filter)**: A space-efficient bit array created during SSTable generation. It helps optimize point lookups by determining if a key is definitely not present in the SSTable, avoiding unnecessary reads of the index and data blocks from disk.

---

### Read and Write Path Flow

#### Write Path
1. The write query is appended to the Write-Ahead Log (WAL) for durability.
2. The key-value pair is inserted into the active MemTable Skiplist.
3. Once the transaction is logged to both structures, the write is complete and control is returned to the client.

#### Read Path
1. Search the **Active MemTable**.
2. Search the **Immutable MemTables** currently queued for flushing.
3. Check the **L0 SSTables**. (Because L0 files can contain overlapping key ranges, RocksDB must search all L0 files unless checked by a Bloom filter).
4. Traverse levels **L1 through Ln**. (Because levels $L \ge 1$ do not contain overlapping key ranges, a key can reside in at most one SSTable per level. RocksDB performs a binary search on the level's index to locate the target SSTable, then reads the data block).

---

### Compaction Mechanics

As new SSTables are flushed to disk, they can accumulate multiple versions of the same key. **Compaction** is the background process of merging and sorting SSTables to reclaim disk space and optimize reads:

```
            LEVEL 1 SSTables
            [ Key: 10 - 50 ]  [ Key: 60 - 90 ]
                    \            /
                     v          v (Merge & Sort)
            LEVEL 2 SSTables
            [ Key: 10 - 30 ]  [ Key: 40 - 70 ]  [ Key: 80 - 100 ]
```

1. **Size-Tiered Compaction**: 
   Groups SSTables into size classes. When multiple SSTables of similar sizes accumulate in a level, RocksDB merges them into a single, larger SSTable. This reduces write overhead but can lead to high space and read amplification.
2. **Leveled Compaction (Default)**: 
   SSTables are organized into levels ($L0, L1, \dots, Ln$), where each level's total capacity is constrained (e.g., L1 = 10MB, L2 = 100MB, L3 = 1GB). When a level exceeds its capacity limit:
   - RocksDB selects an SSTable from that level.
   - It performs a multi-way merge-sort with all overlapping SSTables in the level below.
   - Any overwritten or deleted keys are purged during the merge.

---

## 4. Design Trade-Offs: The Three Amplifications

LSM-trees must balance three primary system resource costs:

```
                         THE LSM TRILEMMA
                         ================

                           Write Amp
                          /         \
                         /           \
                        /             \
                       /    RocksDB    \
                      /   Compaction    \
                     /                   \
            Read Amp --------------------- Space Amp
```

1. **Write Amplification (WA)**: 
   The ratio of bytes written to disk to the logical bytes requested by the application. 
   - *Cause*: Compaction reads, merges, and rewrites the same keys multiple times. 
   - *Impact*: High WA causes disk write saturation and can accelerate SSD wear.
2. **Read Amplification (RA)**: 
   The number of disk page reads required to satisfy a single read query.
   - *Cause*: Point lookups may search multiple SSTable levels, and range queries must merge iterators across all levels.
   - *Impact*: Increases read latency and CPU utilization.
3. **Space Amplification (SA)**: 
   The ratio of disk storage space to the logical size of the active database records.
   - *Cause*: Dead versions and deleted keys (tombstones) remain on disk until removed by compaction.
   - *Impact*: Increases physical storage requirements.

### Compaction Tuning Trade-offs
- **Leveled Compaction**: Maintains low space amplification (typically $\sim 1.1 - 1.2$) and low read amplification by keeping levels sorted and distinct. However, it incurs high write amplification ($\sim 10 - 30$) due to frequent page merges.
- **Universal (Size-Tiered) Compaction**: Reduces write amplification by delaying merges. However, it results in high space and read amplification because duplicate records can accumulate across multiple large files on disk.

---

## 5. Key Design Questions Resolved

### 1. Why are LSM trees preferred in write-heavy workloads?
- B-Trees require random page updates, which can exceed the random write capacity of disk systems.
- LSM-trees buffer writes in memory and append them sequentially to disk logs. This replaces random disk writes with high-throughput sequential writes.

### 2. Why can compaction become expensive?
- Compaction requires reading multiple SSTables from disk, merging and sorting their keys in memory, and writing the newly sorted SSTables back to disk.
- This process consumes significant disk I/O bandwidth and CPU cycles, which can cause latency spikes in active query processing.

### 3. How do Bloom Filters improve read performance?
- Bloom filters are space-efficient probabilistic data structures associated with each SSTable.
- They evaluate if a key is present in the file. If the filter returns false, RocksDB can bypass index and data block reads for that SSTable, reducing read amplification during point lookups.
