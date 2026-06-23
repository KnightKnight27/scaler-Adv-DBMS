# RocksDB Architecture

## 1. Problem Background

Traditional B-Tree databases perform random I/O writes, which can cause significant latency on write-heavy workloads. This is because updating a B-Tree requires modifying page blocks dispersed across disk storage.

RocksDB was designed to handle high-frequency, write-intensive applications (such as logging systems, caching layers, and time-series streaming). To achieve high write throughput, RocksDB abandons the B-Tree architecture and implements a **Log-Structured Merge (LSM) Tree**, which converts random writes into fast sequential disk writes.

---

## 2. Architecture Overview

RocksDB separates writes into memory buffering and background disk sorting stages:

```text
+-----------------------+
|   Application Write   |
+-----------------------+
      | Write to WAL (Disk) & MemTable (RAM)
      v
+-----------------------+
|    MemTable (RAM)     |
+-----------------------+
      | Fills up (~64MB)
      v
+-----------------------+
|  Immutable MemTable   |
+-----------------------+
      | Sequential Flush
      v
+-----------------------+
|    L0 SSTables        | <--- Bloom Filters skip lookups
+-----------------------+
      | Background Compaction
      v
+-----------------------+
|    L1..Ln SSTables    |
+-----------------------+
```

*   **Write Path**: Writes are appended to a Write-Ahead Log (WAL) on disk for durability, then added to an in-memory **MemTable**.
*   **Flush Path**: When the MemTable is full, it is frozen (becomes an **Immutable MemTable**) and written sequentially to disk as a Level 0 (L0) Sorted String Table (SSTable).
*   **Compaction**: Background threads merge overlapping SSTable files from lower levels to higher levels to keep data organized and purge deleted keys.

---

## 3. Internal Design

### 3.1 MemTable & WAL
*   **MemTable**: An in-memory sorted index (implemented as a Skip List). Incoming `PUT` or `DELETE` operations are written directly here. Because writes happen in RAM, latency is extremely low.
*   **Write-Ahead Log (WAL)**: An append-only log file on disk. Every write is appended to the WAL before being inserted into the MemTable to prevent data loss in a crash.

### 3.2 SSTables (Sorted String Tables)
SSTables are immutable files on disk containing sorted key-value pairs:
*   Because they are immutable, reads do not require complex locking.
*   Each SSTable contains data blocks, an index block (for binary search), and a **Bloom Filter**.

### 3.3 Bloom Filters
The read path in an LSM tree requires searching the MemTable and then checking multiple SSTable files on disk. To avoid checking every file, RocksDB uses Bloom Filters:
*   A Bloom Filter is a compact, memory-resident probabilistic data structure.
*   It checks whether a key is present in an SSTable. If it returns false, the key is definitely not in the file, and RocksDB skips reading it from disk.

### 3.4 Compaction
As new SSTables are flushed, the database files accumulate. RocksDB uses **Leveled Compaction**:
*   Level 0 files have overlapping key ranges.
*   Levels 1 to $N$ contain non-overlapping files.
*   When a level exceeds its size limit, background threads merge and sort its files with overlapping files in the next level, purging old versions and tombstone markers.

---

## 4. Design Trade-Offs

*   **Write Throughput vs. Write Amplification**: By writing sequentially, RocksDB achieves high write speeds. However, because compaction continually rewrites files, a single key-value pair is written to disk multiple times (Write Amplification), consuming disk I/O.
*   **Write Optimization vs. Read Amplification**: Writes are immediate, but reads may need to search multiple levels to locate a key (Read Amplification). RocksDB uses Bloom Filters in memory to minimize this read penalty.
*   **In-Place Update vs. Space Amplification**: Updates and deletes are appends (deletes write a "tombstone" marker). The database contains duplicate older keys until compaction merges them, leading to temporary Space Amplification.

---

## 5. Experiments & Observations

### 5.1 Monitoring Cache and Bloom Filters
In RocksDB, we monitor internal statistics to analyze read behavior:

```text
// Retrieve RocksDB Statistics
std::string stats;
db->GetProperty("rocksdb.stats", &stats);
```

**Observation:**
The statistics indicate the hit rates of the Block Cache and Bloom Filters. High Bloom Filter filter-avoidance rates show that disk reads are bypassed for non-existent keys.

### 5.2 Compaction Write Stalls
When incoming writes exceed the speed of the background compaction threads:
*   Level 0 file counts increase.
*   RocksDB intentionally stalls incoming writes to allow compaction to catch up, protecting the read path from degrading.

---

## 6. Key Learnings

1.  **Sequential Writes are Faster**: Converting random updates into sequential log appends and memory writes provides significant write performance gains.
2.  **Read Complexity is Shifted to Background Tasks**: LSM trees trade write latency for read latency and disk wear, shifting the I/O cost to background compaction threads.
3.  **Auxiliary Structures are Vital**: A read-path on an LSM tree is slow without in-memory helpers like Bloom Filters and Block Caches, which eliminate unnecessary disk seeks.
