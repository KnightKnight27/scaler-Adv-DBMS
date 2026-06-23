# RocksDB Architecture: An LSM-Tree Storage Engine

## 1. Problem Background

### Why RocksDB Exists
Facebook forked Google's LevelDB to create RocksDB, targeting the performance characteristics of modern hardware—specifically SSDs and multi-core processors.

### What Problem It Solves
B-Tree storage engines such as InnoDB and PostgreSQL's heap suffer from high write amplification when handling write-intensive workloads. Updating even a single small field forces the engine to read, modify, and rewrite an entire page (typically 8–16 KB).

RocksDB replaces random page writes with sequential log appends by adopting a Log-Structured Merge-Tree (LSM-Tree) design. This dramatically raises write throughput and reduces wear on SSDs.

---

## 2. Architecture Overview

### High-Level Architecture
RocksDB is an embedded key-value store, not a full relational database. It has no SQL layer. Systems like CockroachDB, TiDB, and Kafka Streams use it as their underlying storage engine.

### Data Flow (The Write Path)
1.  **WAL (Write-Ahead Log):** Each write first goes to a sequential WAL on disk to ensure crash safety.
2.  **MemTable:** The write then enters an in-memory sorted structure (usually a SkipList).
3.  **Immutable MemTable:** When the MemTable reaches capacity, it becomes read-only and a fresh MemTable takes new writes.
4.  **Flush to Disk:** A background thread writes the immutable MemTable to disk as an SSTable (Sorted String Table) at level 0.

---

## 3. Internal Design

### Storage Structures (LSM-Tree)
Data on disk is arranged in levels numbered L0 through Ln.
*   **L0 (Level 0):** SSTables flushed from memory. Key ranges may overlap across different SSTables in this level.
*   **L1 to Ln:** Each level is roughly 10 times larger than the previous one. From L1 onward, SSTables have non-overlapping key ranges and are fully sorted.

### Compaction
Compaction merges SSTables from level *i* with overlapping ones in level *i+1*, discards stale or deleted keys, and writes fresh sorted SSTables into level *i+1*.
*   **Purpose:** Reclaims disk space, removes obsolete entries, and preserves order to keep reads efficient.

### The Read Path
Reading a key in an LSM-tree can be slower than in a B-Tree because the key may reside at any level.
1.  Search the active **MemTable**.
2.  Search **Immutable MemTables**.
3.  Search **L0 SSTables** (all of them must be consulted).
4.  Search **L1...Ln SSTables**.

### Bloom Filters
Each SSTable carries a Bloom filter—a probabilistic structure that can definitively say "this key is not in this file." When the filter reports a miss, RocksDB skips reading that SSTable entirely, saving significant disk I/O.

---

## 4. Design Trade-Offs

### Advantages
1.  **Extreme Write Performance:** All writes are an in-memory insert plus a sequential disk append. Random I/O is absent from the write path.
2.  **Storage Efficiency:** SSTables are written sequentially and never mutated, so block compression works well. Data is packed densely without the fragmentation typical of B-Trees.
3.  **SSD Optimization:** Sequential writes reduce write amplification on flash storage, extending device lifespan.

### Limitations (The Amplification Trilemma)
LSM-Tree design forces a trade-off among three metrics:
1.  **Read Amplification:** How many disk reads are needed for a single logical read. High in LSM-Trees because data is spread across levels.
2.  **Write Amplification:** Ratio of bytes actually written to disk to bytes the application wrote. Compaction rewrites data repeatedly; one application byte can cause 10–30× more disk writes.
3.  **Space Amplification:** Ratio of on-disk database size to logical data size. Old key versions stay on disk until compaction removes them.

*Trade-off:* Aggressive compaction favors reads at the cost of higher write amplification. Lazy compaction favors writes but degrades read performance and increases space usage.

---

## 5. Experiments / Observations

### Observing Amplification
Running `db_bench` with different compaction strategies shows distinct behavior:
*   **Leveled Compaction (Default):** Low space and read amplification. Write amplification is high because data is rewritten at each level.
*   **Universal Compaction:** Merges similarly sized files rather than maintaining strict levels. Write amplification drops significantly (good for write-heavy workloads), while space and read amplification rise.

---

## 6. Key Learnings

1.  **Immutability Simplifies Concurrency:** Never modifying data in place on disk lets RocksDB avoid page-level locking and complex buffer management.
2.  **The Trilemma Is Inescapable:** Read, write, and space amplification cannot all be minimized together. Workload profiling must guide compaction tuning.
3.  **Bloom Filters Are Critical:** Without them, LSM-Tree reads would require scanning too many files. They bridge the gap between write-optimized storage and acceptable read performance.
