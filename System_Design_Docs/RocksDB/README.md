# RocksDB Architecture: An LSM-Tree Storage Engine

## 1. Problem Background

### Why RocksDB Exists
RocksDB was created by Facebook as a fork of Google's LevelDB. It was developed to fully utilize the performance capabilities of modern fast storage hardware (specifically solid-state drives - SSDs) and multi-core CPU architectures.

### What Problem It Solves
Traditional B-Tree based storage engines (like InnoDB or PostgreSQL's heap) suffer from high *write amplification* and random I/O latency when processing massive, high-throughput write workloads. When a small record is updated in a B-Tree, an entire page (e.g., 8KB or 16KB) must be read, modified, and written back to disk.

RocksDB solves this by transforming random writes into sequential writes using a Log-Structured Merge-Tree (LSM-Tree) architecture, vastly improving write throughput and extending the lifespan of SSDs.

---

## 2. Architecture Overview

### High-Level Architecture
RocksDB is an embedded key-value store. It is not a relational database, nor does it have a SQL engine. It acts as a high-performance storage engine that other databases (like CockroachDB, TiDB, or Kafka Streams) build upon.

### Data Flow (The Write Path)
1.  **WAL (Write-Ahead Log):** An incoming write is first appended sequentially to a WAL on disk for crash durability.
2.  **MemTable:** The write is then inserted into an in-memory data structure called a MemTable (typically a SkipList), where keys are kept sorted.
3.  **Immutable MemTable:** When the MemTable fills up, it is marked read-only (immutable), and a new MemTable is created for incoming writes.
4.  **Flush to Disk:** A background thread flushes the Immutable MemTable to disk, creating an SSTable (Static Sorted Table) at Level 0.

---

## 3. Internal Design

### Storage Structures (LSM-Tree)
The disk storage is organized into multiple levels (L0 to Ln).
*   **L0 (Level 0):** Contains SSTables flushed directly from memory. Keys may overlap between different SSTables in L0.
*   **L1 to Ln:** Each subsequent level is exponentially larger (typically 10x) than the previous one. In L1 and below, the data is strictly sorted, and key ranges across SSTables do not overlap.

### Compaction
Compaction is the core background process of an LSM-Tree. It reads SSTables from Level *i*, merges them with overlapping SSTables in Level *i+1*, discards deleted or overwritten keys, and writes the new sorted SSTables to Level *i+1*.
*   **Purpose:** Reclaims space, removes obsolete versions of keys, and maintains strict ordering to ensure read performance doesn't degrade.

### The Read Path
Reading a key is inherently slower in an LSM-tree than a B-Tree because the data might exist in multiple places.
1.  Search the active **MemTable**.
2.  Search **Immutable MemTables**.
3.  Search **L0 SSTables** (must check all overlapping tables).
4.  Search **L1...Ln SSTables**.

### Bloom Filters
To drastically speed up the Read Path, RocksDB attaches a Bloom Filter to each SSTable. A Bloom Filter is a probabilistic data structure that can definitively answer "Is this key NOT in this file?" If the filter says no, RocksDB skips reading that SSTable entirely, saving massive amounts of disk I/O.

---

## 4. Design Trade-Offs

### Advantages
1.  **Extreme Write Performance:** All writes are initially just an in-memory update and a sequential disk append. Random I/O is eliminated on the write path.
2.  **Storage Efficiency:** Block compression works extremely well on SSTables because they are written sequentially and are immutable. Data is packed densely without the fragmentation seen in B-Trees.
3.  **SSD Optimization:** Sequential writing minimizes wear-and-tear on solid-state drives compared to the random page overwrites of traditional databases.

### Limitations (The Amplification Trilemma)
Designing an LSM-Tree requires balancing three competing factors:
1.  **Read Amplification:** The number of disk reads required to satisfy a single logical read query. (High in LSM due to searching multiple levels).
2.  **Write Amplification:** The ratio of bytes written to disk to the bytes written by the application. (Compaction constantly rewrites data, meaning 1 byte of application data might result in 30 bytes of actual disk writes over its lifetime).
3.  **Space Amplification:** The ratio of the size of the database on disk to the size of the logical data. (Old versions of data remain on disk until compaction cleans them up).

*Trade-off:* You can configure compaction to favor read speed (aggressive compaction = high write amplification) or favor write speed (lazy compaction = high read amplification).

---

## 5. Experiments / Observations

### Observing Amplification
Running `db_bench` (RocksDB's benchmarking tool) under different compaction styles yields distinct results:
*   **Leveled Compaction (Default):** Keeps Space and Read amplification low, but Write amplification is high because data is rewritten at every level.
*   **Universal Compaction:** Merges files of similar sizes rather than strictly leveling. This drastically lowers Write amplification (great for write-heavy workloads) but increases Space and Read amplification.

---

## 6. Key Learnings

1.  **Embracing Immutability:** By never modifying data in place on disk, RocksDB sidesteps the complexities of page locks and buffer management that plague B-Tree implementations.
2.  **The Amplification Trilemma is Unavoidable:** You cannot optimize Read, Write, and Space amplification simultaneously. The DBA must profile their specific workload and tune the LSM-Tree parameters accordingly.
3.  **Bloom Filters are Essential:** Without Bloom Filters, read performance in an LSM-Tree would be unacceptably slow. They act as the crucial bridge that makes write-optimized engines viable for reads.
