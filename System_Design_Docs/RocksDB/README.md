# Topic 4: RocksDB Architecture

This report explores the architecture of **RocksDB**, a high-performance, embedded key-value store optimized for fast, low-latency storage. RocksDB is based on the Log-Structured Merge-tree (LSM-tree) data structure, designed to maximize write throughput and exploit the high parallel write capabilities of modern SSDs.

---

## 1. LSM-Tree Architecture & Components

RocksDB organizes data into a hierarchy of components across memory and disk. This structure converts random write operations into sequential disk access.

```
                     ┌──────────────────────────────────┐
                     │          Write Request           │
                     └────────────────┬─────────────────┘
                                      │
                         ┌────────────┴────────────┐
                         ▼                         ▼
            ┌────────────────────────┐ ┌────────────────────────┐
            │ Write-Ahead Log (WAL)  │ │  Active MemTable (RAM)  │
            │      (Disk Log)        │ │  (SkipList / concurrent)│
            └────────────────────────┘ └───────────┬────────────┘
                                                   │ (Full)
                                                   ▼
                                       ┌────────────────────────┐
                                       │  Immutable MemTable    │
                                       └───────────┬────────────┘
                                                   │ (Flush)
                                                   ▼
    Disk Storage:                     ┌────────────────────────┐
    Level 0 (Overlap keys allowed)    │  SSTable  │  SSTable   │
                                      └───────────┬────────────┘
                                                   │ (Compaction)
                                                   ▼
    Level 1 (SSTs sorted, no overlap) ┌────────────────────────┐
                                      │  SSTable  │  SSTable   │
                                      └────────────────────────┘
```

### Memory Components
* **Write-Ahead Log (WAL)**: Written sequentially to disk before writing to memory. It ensures durability; if the process crashes, the WAL is replayed to reconstruct the in-memory state.
* **MemTable**: An in-memory write buffer. By default, it is implemented as a **SkipList**, which allows concurrent reads and writes with $O(\log N)$ search and insertion times.
* **Immutable MemTable**: When the active MemTable reaches its capacity limit (configured by `write_buffer_size`), it is marked as immutable, and a new active MemTable is allocated. A background thread flushes the immutable MemTable to disk as a Level 0 Sorted String Table (SSTable) file.

### Disk Components (SSTables)
Data on disk is organized into Sorted String Tables (SSTables).
* **Sorted Structure**: An SSTable file stores keys in sorted order. It is divided into data blocks, an index block (mapping the last key of each data block to its file offset), and filter blocks (Bloom filters).
* **Leveled Storage Structure**: SSTables are organized into levels ($L_0, L_1, L_2, \dots, L_n$):
  * **Level 0 ($L_0$)**: Contains direct flushes from the MemTable. Because files are written directly, **key ranges of $L_0$ files can overlap**. A key may exist in multiple $L_0$ files.
  * **Levels $L_1$ to $L_n$**: Key ranges are completely partition-sorted; **no two files at the same level overlap**. The maximum size of each level is capped (typically growing by a factor of 10, e.g., $L_1 = 10\text{ MB}$, $L_2 = 100\text{ MB}$, $L_3 = 1\text{ GB}$).

---

## 2. Read and Write Path Flow

### The Write Path (Sequential Append)
Writing to RocksDB is extremely fast because it requires no disk seeks or in-place page updates:
1. The write request is appended to the active **WAL** file on disk (providing durability).
2. The key-value pair is inserted into the active in-memory **MemTable**.
3. Once inserted, the write returns.
4. When the MemTable fills up, it becomes an **Immutable MemTable** and is flushed sequentially to Level 0 on disk.

### The Read Path (Multi-Level Search)
Because keys are spread across memory and multiple SSTable levels on disk, reading a key requires a structured search:
1. **MemTable Search**: Check the active MemTable. If found, return.
2. **Immutable MemTables**: Check any immutable MemTables in memory.
3. **Level 0 Search**: Check all $L_0$ files on disk. Because $L_0$ files have overlapping key ranges, RocksDB must search *all* $L_0$ files unless filtered out by Bloom filters.
4. **Leveled Search ($L_1 \dots L_n$)**: For each subsequent level, perform a binary search on the level's non-overlapping file ranges to locate the single SSTable file that could contain the key. Search the file's index block and read the data block.
5. **Caching**: Reads utilize the **Block Cache** (caching uncompressed blocks in memory) and the OS page cache.

---

## 3. Bloom Filters & Read Optimization

Because the read path must query multiple files on disk, random reads can suffer from high I/O latency (known as the **read amplification** bottleneck). RocksDB optimizes this using **Bloom Filters**:
* **Mechanism**: A Bloom filter is a space-efficient probabilistic data structure used to test set membership. It can return either "possibly in set" or "definitely not in set".
* **SSTable Filters**: Each SSTable contains an associated Bloom filter block. Before opening and searching an SSTable file on disk, RocksDB queries its Bloom filter in memory.
* **Impact**: If the filter returns "definitely not in set", RocksDB skips searching that SSTable file entirely, saving a disk read. This reduces the number of random disk reads to nearly one per lookup, even in databases containing billions of records.

---

## 4. Compaction: Purpose and Strategies

As new data is written and flushed, duplicate updates, deletes (marked by placeholder values called **tombstones**), and old versions of keys accumulate across levels. To clean up space and maintain read performance, RocksDB runs a background process called **Compaction**.

```
    Level 1:  [  Key 10 - 50  ]  [  Key 60 - 90  ]
                     │
                     ▼ (Merged & Sorted)
    Level 2:  [ Key 10 - 30 ] [ Key 35 - 70 ] [ Key 75 - 100 ]
```

### Why Compaction is Required
1. **Space Reclamation**: Merges updates and discards older key versions and tombstones.
2. **Read Performance**: Reduces the number of files that need to be searched for a single key.
3. **Write Stalls**: If the rate of MemTable flushes exceeds the compaction rate, RocksDB will restrict or pause incoming writes (a write stall) to prevent $L_0$ file count explosion.

### Compaction Strategies
* **Leveled Compaction (Default)**:
  * When a level $L_i$ exceeds its size limit, one or more files from $L_i$ are selected and merged with overlapping files at the next level $L_{i+1}$ (performing a merge-sort).
  * *Trade-off*: Minimizes space amplification (only ~10% space overhead) and improves read performance, but causes high write amplification.
* **Universal Compaction**:
  * Focuses on reducing write amplification. It merges all SSTable files of similar sizes together, regardless of level.
  * *Trade-off*: Faster writes, but higher space amplification (up to 100% temporary space overhead during merge).

---

## 5. Storage Amplification Metrics

LSM-trees trade resource consumption across three dimensions, known as the **RUM Conjecture** (Read, Update/Write, Memory/Space).

```
                     Write Amplification
                             /\
                            /  \
                           /    \
                          /      \
                         /________\
            Read Amplification     Space Amplification
```

### 1. Write Amplification Factor (WAF)
$$\text{WAF} = \frac{\text{Bytes written to storage}}{\text{Bytes written by application}}$$
* In RocksDB, modifying a 100-byte key-value pair requires writing 100 bytes to the WAL. When compacted, that data is read and written multiple times as it migrates down the levels. In Leveled Compaction, the WAF can easily reach **10x to 30x**.

### 2. Read Amplification Factor (RAF)
$$\text{RAF} = \frac{\text{Disk bytes read}}{\text{Logical bytes requested by query}}$$
* A point query seeks a single key. In the worst case (without Bloom filters), RocksDB might need to read the index blocks and data blocks of multiple SSTable files, yielding an RAF of **5x to 20x**. Bloom filters and block caches reduce this closer to **1x**.

### 3. Space Amplification Factor (SAF)
$$\text{SAF} = \frac{\text{Physical database size on disk}}{\text{Logical size of user data}}$$
* Because deletes and updates do not overwrite data in-place, dead keys and tombstones consume space until a compaction occurs. In Leveled Compaction, SAF is typically low (**1.1x to 1.2x**). In Universal Compaction, it can reach **2.0x**.

---

## 6. Suggested Questions Answered

### Q1: Why are LSM trees preferred in write-heavy workloads?
LSM-trees convert random write workloads into sequential writes. In relational databases like MySQL/InnoDB, inserting a row requires updating index pages spread across random locations on disk, causing slow, random disk seeks. In contrast, RocksDB writes sequentially to the WAL and appends to an in-memory MemTable. Random writes are buffered and converted into sequential page flushes, which aligns with the raw sequential write bandwidth of SSDs and hard disks.

### Q2: Why can compaction become expensive?
Compaction is a heavy disk I/O operation. To merge-sort files from level $L_i$ to level $L_{i+1}$, RocksDB must:
1. Read multiple SSTable files from disk into memory.
2. Sort and merge the keys, removing duplicates and tombstones.
3. Write the new sorted SSTable files back to disk.
This process consumes substantial disk read/write bandwidth and CPU cycles (for decompression/compression), which can interfere with the performance of concurrent user queries (causing latency spikes).

### Q3: How do Bloom Filters improve read performance?
Bloom filters prevent unnecessary disk reads. In an LSM-tree, a key could reside in any level or file. Without Bloom filters, looking up a non-existent key (a negative query) would force RocksDB to read the index blocks of many SSTable files on disk. A Bloom filter stored in memory can determine with high probability (e.g., 99%) that a key does not exist in a given file. RocksDB can skip searching that file entirely, reducing disk reads and dramatically speeding up lookup operations.
