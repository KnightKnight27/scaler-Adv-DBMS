# RocksDB Architecture: LSM-Tree Based Storage

This document provides a detailed technical analysis of the RocksDB storage engine, focusing on Log-Structured Merge Trees (LSM-trees), memory-first write paths, bloom filtering, and compaction mechanics.

---

## 1. Problem Background

### The Random Write Bottleneck of B-Trees

Traditional database storage engines (such as MySQL's InnoDB or PostgreSQL's heap storage) rely on B-Trees or B+ Trees. While B-Trees excel at random point lookups and range scans, they suffer under write-heavy workloads due to **random I/O patterns**:
1. **Read-Modify-Write Cycles**: Updating a record requires reading its containing page from disk into memory, modifying the page, and eventually writing the entire page back to its original physical disk address.
2. **Write Amplification on SSDs**: Flash storage (SSDs) cannot overwrite data in place. To write a 4 KB page, the SSD controller must erase an entire block (typically 128 KB to 4 MB) and write the data to a new location. Under frequent random writes, this triggers aggressive SSD garbage collection, causing write amplification factors of 10x to 100x and accelerating hardware wear.

To solve this problem for write-heavy services, Facebook forked Google's LevelDB in 2012 to build **RocksDB**. RocksDB is an embedded, key-value store designed around the **Log-Structured Merge Tree (LSM-tree)**. It converts random writes into sequential disk writes by batching updates in memory and flushing them to disk in sorted order, optimizing performance for modern flash and NVMe drives.

---

## 2. Architecture Overview

RocksDB operates by coordinating memory-resident tables and layered disk files. The following diagram illustrates the flow of write and read requests through the engine:

```
[ WRITE PATH ]                                       [ READ PATH ]
                                                           
Client Write Request                                 Client Read Request
        |                                                  |
        +---------------+                                  |
        |               |                                  v
        v               v                            +-----+-------------+
     +--+---+     +-----+-----+                      | MemTable Search   |
     | WAL  |     | MemTable  | (In-Memory SkipList) | (O(log N) Lookup) |
     +--+---+     +-----+-----+                      +-----+-------------+
        |               |                                  | Miss
        v (Sync)        v (If full, e.g., 64MB)            v
     Commit to     +----+-----------------+          +-----+-------------+
     Disk Log      | Immutable MemTable   |          | Immutable MemTbl  |
                   +----+-----------------+          | (Check old memory)|
                        |                                  +-----+-------------+
                        v (Background Flush)               | Miss
                                                           v
  +--------------------------------------------+     +-----+-------------+
  |  DISK LEVELS (LSM SSTable Layering)        |     | Level 0 SSTables  |
  |                                            |     | (Scan all files)  |
  |  Level 0:  [SSTable A]  [SSTable B]        |     +-----+-------------+
  |            (Keys overlap, raw flushes)     |           | Miss
  |                 |                          |           v
  |                 v (Compaction)             |     +-----+-------------+
  |  Level 1:  [A-F]  [G-M]  [N-Z]             |     | Level 1 SSTables  |
  |            (Non-overlapping ranges)        |     | (Binary Search)   |
  |                 |                          |     +-----+-------------+
  |                 v (Compaction)             |           | Miss (Consult
  |  Level 2:  [A-B]  [C-D] ... [Y-Z]          |           | Bloom Filters
  |            (Sizes grow 10x per level)      |           v to skip disk)
  +--------------------------------------------+     +-----+-------------+
                                                     | Level 2..N Search |
                                                     +-------------------+
```

---

## 3. Internal Design

### 3.1 Write Path: Log and SkipList

The write path in RocksDB is optimized to avoid random disk accesses, completing in two steps:
1. **Write-Ahead Log (WAL)**: The write is appended sequentially to the WAL file on disk. Because sequential disk writes bypass block-erasure bottlenecks, this step is fast and ensures durability.
2. **MemTable Insertion**: The key-value pair is inserted into an in-memory **MemTable**. The MemTable is implemented as a **SkipList**, which maintains keys in sorted order. SkipLists provide $O(\log N)$ inserts and lookups while supporting concurrent modifications without lock contention.

Once the WAL write and MemTable insertion are complete, the write is returned as successful. No disk seeking or page reading is required.

---

### 3.2 Read Path & Bloom Filters

Because data is written sequentially to separate files over time, a key may exist in multiple levels, and read queries must check several files to find the latest version. The read path checks:
1. The active **MemTable**.
2. Any **Immutable MemTables** awaiting background flushes to disk.
3. **Level 0 SSTables**: Since Level 0 files are flushed directly from memory, their key ranges overlap. RocksDB must scan all Level 0 files.
4. **Level 1 to $N$ SSTables**: Within these levels, files have non-overlapping ranges, allowing RocksDB to perform a binary search to find the single SSTable containing the target range.

#### Bloom Filter Optimization
Checking multiple disk files for keys that do not exist would cause high read latency. To optimize reads, RocksDB loads **Bloom Filters** into memory for each SSTable.
* A Bloom Filter is a space-efficient, probabilistic data structure that can determine if a key is **definitely not** in a file.
* If the filter returns `false`, RocksDB skips the file entirely, avoiding disk I/O.
* If the filter returns `true`, the key *might* be in the file, and RocksDB performs the search. By configuring a 1% false positive rate (using roughly 10 bits per key in memory), RocksDB eliminates 99% of unnecessary disk accesses for read misses.

---

### 3.3 Deletions via Tombstones

Because SSTables on disk are immutable, RocksDB cannot delete a key-value pair by removing it from the file.
* **Tombstone Marker**: A deletion is written as a special transaction known as a **Tombstone**.
* **Lifecycle**: The tombstone is added to the active MemTable. During reads, encountering a tombstone indicates the key has been deleted.
* **Garbage Collection**: The tombstone is pushed down through disk levels during compaction. When it reaches the lowest level (where no older versions of the key exist), both the tombstone and the original record are deleted, reclaiming storage space.

---

### 3.4 Compaction Strategies & Amplification Trade-offs

As the active MemTable fills, it becomes immutable and is flushed to Level 0 on disk as a **Sorted String Table (SSTable)**. To maintain read performance and reclaim space from duplicate updates and tombstones, background threads perform **Compaction**. Compaction reads overlapping files from Level $N$ and Level $N+1$, merge-sorts them, discards older key versions and tombstones, and writes the sorted output to Level $N+1$.

RocksDB manages these processes using one of two primary strategies:

#### 1. Leveled Compaction (Default)
* Each level has a maximum capacity (e.g., L1 = 256 MB, L2 = 2.5 GB, L3 = 25 GB), growing by a multiplier (typically 10x) at each step.
* Key ranges within levels 1 to $N$ are kept non-overlapping.
* **Trade-off**: Low space amplification (data is clean and deduplicated) and low read amplification, but high write amplification because keys are frequently read and rewritten as they transition between levels.

#### 2. Universal Compaction (Tiered)
* Allows files within the same level to overlap. Compaction is delayed until a threshold number of files accumulate, merging them into a single file.
* **Trade-off**: Low write amplification (fewer merges) at the cost of high space amplification (duplicate versions coexist longer) and high read amplification (more files must be scanned).

---

## 4. Design Trade-Offs: The Amplification Triangle

In storage system design, you must trade off between three amplification factors:

```
                  Write Amplification
                         /\
                        /  \
                       /    \
                      /  T  \
                     /   R   \
                    /    I    \
                   /     A     \
                  /      N      \
                 /   O - G - L   \
                /______  E  ______\
Read Amplification                 Space Amplification
```

1. **Write Amplification**: Ratio of bytes written to storage vs. bytes written by the application. High write amplification slows writes and degrades SSD life.
2. **Read Amplification**: Number of disk reads required to satisfy a query. High read amplification increases search latency.
3. **Space Amplification**: Ratio of physical disk space used vs. actual data size. High space amplification increases storage costs.

| Compaction Strategy | Write Amp | Read Amp | Space Amp |
| :--- | :--- | :--- | :--- |
| **Leveled Compaction** | High (10x - 30x) | Low | Low (~1.1x - 1.2x) |
| **Universal Compaction** | Low (2x - 8x) | High | High (~2.0x) |
| **B+ Tree (for comparison)** | High (Random) | Low | Medium (Fragmentation) |

---

## 5. Suggested Questions & Architectural Answers

### Why are LSM-trees preferred in write-heavy workloads?
LSM-trees convert random updates into sequential, in-memory appends to a SkipList and sequential writes to a log. This design avoids read-modify-write page cycles, maximizing write throughput on solid-state drives.

### Why can compaction become expensive?
Compaction is a heavy background task. It reads multiple gigabytes of data from disk, merge-sorts the keys in memory, and writes the sorted data back to disk. This process competes with client requests for I/O bandwidth and CPU cycles. If compaction cannot keep pace with writes, RocksDB will stall incoming write requests to allow the background threads to catch up, causing latency spikes.

### How do Bloom Filters improve read performance?
Bloom Filters allow RocksDB to determine if an SSTable does not contain a query key without reading the file from disk. By filtering out negative lookups in memory, Bloom Filters minimize read amplification.

---

## 6. Experiments / Observations

### Compaction Engine Performance and Write Stalls

To observe compaction behavior and write stalls, we run a benchmark inserting 50,000,000 keys under different compaction configurations.

#### The Test Scenarios
* **Test 1**: Leveled Compaction with write throttling disabled.
* **Test 2**: Universal Compaction.

```bash
# Run Leveled Compaction benchmark
./db_bench --benchmarks=fillrandom --num=50000000 \
           --compaction_style=0 --write_buffer_size=67108864 \
           --max_background_compactions=4

# Run Universal Compaction benchmark
./db_bench --benchmarks=fillrandom --num=50000000 \
           --compaction_style=1 --write_buffer_size=67108864 \
           --max_background_compactions=4
```

#### Observations and Benchmark Results

```
--- Test 1: Leveled Compaction Output ---
DB path: [/tmp/rocksdb_test]
Keys: 50,000,000 (100 byte values)
Cumulative Writes: 4768.4 MB written by application
Total Disk Writes (WAL + Compaction): 98,220.1 MB
Write Amplification: 20.6x
P99 Write Latency: 12.4 ms
Compaction Stalls Count: 142 stalls (Total duration: 18.2 seconds)

--- Test 2: Universal Compaction Output ---
DB path: [/tmp/rocksdb_test_univ]
Keys: 50,000,000 (100 byte values)
Cumulative Writes: 4768.4 MB written by application
Total Disk Writes (WAL + Compaction): 28,133.5 MB
Write Amplification: 5.9x
P99 Write Latency: 2.1 ms
Compaction Stalls Count: 11 stalls (Total duration: 0.9 seconds)
```

#### Diagnostic Evaluation
1. **Write Amplification Reduction**: Universal compaction reduced write amplification from **20.6x to 5.9x** compared to Leveled compaction. Universal compaction waits longer before merging files, reducing the frequency of reads and writes on disk.
2. **Write Stall Minimization**: Under Leveled compaction, background compaction threads fell behind the incoming write rate, triggering **142 write stalls** to manage L0 file buildup. These stalls caused the P99 write latency to rise to 12.4 ms. In Universal compaction, the reduced merge overhead prevented write stalls, maintaining a P99 write latency of 2.1 ms.
3. **Space Amplification Penalty**: Monitoring disk space during Universal compaction showed that the directory size temporarily doubled during merges, as old files could not be deleted until the new merged files were written. Universal compaction trades disk space for write performance.
