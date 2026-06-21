# RocksDB Architecture & LSM-Tree Performance Engineering

This repository contains a comprehensive technical analysis of the RocksDB key-value storage engine. It details the internal mechanics of Log-Structured Merge-trees (LSM-trees), sequential memory-to-disk pipelines, Bloom filter optimization, compaction operations, and the fundamental three-way trade-off between write, read, and space amplification.

---

# 1. Problem Background

RocksDB was developed by Facebook in 2012, building upon Google’s **LevelDB** project. It was engineered to overcome severe hardware utilization and scalability bottlenecks encountered in massive, modern web-scale infrastructures.

### Why the Engine Exists
Traditional relational and key-value database systems rely on B-Tree variants for primary disk organization. In a B-Tree, updates modify data pages in-place on block storage. While highly efficient for read-intensive operations, this model results in high volumes of random disk I/O when processing dense, multi-threaded write workloads. 

### Historical Context & Core Problem Solved
As the industry transitioned toward flash memory (SSDs) and fast non-volatile storage, the random-write profile of B-Trees introduced severe write amplification, which prematurely exhausted SSD endurance and degraded I/O channels. RocksDB was engineered to handle high-throughput, write-heavy workloads while maximizing concurrent CPU usage and fast storage mediums. By adopting an append-only, Log-Structured Merge-tree (LSM-tree) design, it converts random writes into sequential disk updates, maximizing write throughput and dramatically extending SSD hardware lifespans.

---

# 2. Architecture Overview

RocksDB decomposes its architecture into an in-memory staging layer (MemTables) and a tiered, multi-level disk storage organization (SSTables), coordinated via a strict sequential write-ahead log.



## Main System Components
* **Write-Ahead Log (WAL):** A sequential append-only transaction log on persistent storage. It records incoming data modifications immediately to ensure durability before memory buffers are updated.
* **MemTable:** An in-memory, mutable sorted data structure (typically implemented as a concurrent SkipList). All incoming inserts, updates, and deletes are buffered here in sorted order.
* **Immutable MemTable:** A read-only memory buffer. When a active MemTable fills up to its threshold, it transitions to an immutable state while a background flush thread prepares to write its contents to disk.
* **SSTables (Sorted String Tables):** Static, structured files written sequentially to disk. Each SSTable contains a sorted sequence of keys, an optional index block, and accompanying data blocks.
* **Leveled Disk Hierarchy ($L_0$ to $L_n$):** RocksDB organizes SSTables into hierarchical strata. Layer $L_0$ contains direct flushes from memory and allows overlapping key ranges. Layers $L_1$ through $L_n$ maintain completely disjoint, non-overlapping key segments.

## Architectural Data Flow
```

Incoming Request ──► [Write-Ahead Log (Disk)]
│
▼
[MemTable (Memory)] ──(Fills up)──► [Immutable MemTable]
│
(Flush Thread)
│
▼
┌───────────────────┐
│ Level 0 SSTables  │ (Overlapping Ranges)
└─────────┬─────────┘
│
(Compaction)
│
▼
┌───────────────────┐
│ Level 1..n SSTs   │ (Non-overlapping)
└───────────────────┘
```
---

# 3. Internal Design

## Write Path
1. The client issues a write request. The thread appends the payload sequentially into the **WAL**.
2. The payload is inserted directly into the mutable **MemTable** (SkipList structure).
3. The write is marked successful. No random disk operations occur during the entire transaction path.

## Read Path
1. The query searches the active **MemTable**. If the target key is found, the value is returned.
2. If missed, it checks any existing in-memory **Immutable MemTables**.
3. If still missed, the search traverses down the disk levels sequentially starting at **Level 0 ($L_0$)**.
4. Within $L_0$, because key ranges overlap, the engine must inspect all $L_0$ SSTables. For levels $L_1$ to $L_n$, it performs a binary search on the level's boundary array to pick the single SSTable containing that key range.
```
              +-----------------------+
              |  Read Request (Key)   |
              +-----------+-----------+
                          |
                          v
              +-----------------------+
              |    Search MemTable    | ---> [Found: Return Data]
              +-----------+-----------+
                          | (Miss)
                          v
              +-----------------------+
              | Search Immutable MemT | ---> [Found: Return Data]
              +-----------+-----------+
                          | (Miss)
                          v
              +-----------------------+
              | Check Bloom Filters   | ---> [Negative: Skip SSTable]
              +-----------+-----------+
                          | (Positive Match / Possible Hit)
                          v
              +-----------------------+
              |  Read SSTable Block   | ---> [Confirm and Return]
              +-----------------------+

```
## Bloom Filters
To mitigate the read latency penalty introduced by checking multiple disk files, RocksDB maps an optional bit-array **Bloom Filter** inside each SSTable header. Before pulling an entire 4 KB SSTable block from disk storage into memory, RocksDB evaluates the target key against the Bloom Filter. A negative response guarantees the key is missing from that specific SSTable, allowing the engine to completely skip the disk seek.

## Compaction Mechanics
Because data modifications are appended sequentially, obsolete key versions and tombstone markers for deleted records accumulate down through the disk levels. **Compaction** is a critical background thread optimization process that reads overlapping SSTables from a higher level ($L_i$), merges them with sorted ranges from a lower level ($L_{i+1}$), purges duplicated or deleted records, and writes out fresh, streamlined, sorted files.

---

# 4. Design Trade-Offs

The LSM architecture is governed by the **RUM Conjecture** (Read, Update/Write, Space amplification trade-offs):

| Architectural Choice | Core Advantages | Limitations & Structural Implications | Performance Implications |
| :--- | :--- | :--- | :--- |
| **Append-Only LSM-Tree Pipeline** | • Sequential disk writes<br>• Maximizes throughput<br>• Eliminates raw random-write I/O | • Requires background maintenance<br>• Prone to write stalls if flushes lag<br>• Structural read paths are deep | • Extremely fast, predictable writes<br>• Higher point-read latency without caching |
| **Leveled Compaction Strategy** | • Minimizes space overhead<br>• Bounds read paths for lower levels<br>• Eliminates duplicate records | • High write amplification factor<br>• Heavy background I/O consumption<br>• Competes with active workloads | • Speeds up point and range reads<br>• Can cause application latency spikes during heavy I/O |
| **In-Memory Bloom Filtering** | • Skips unnecessary disk blocks<br>• Limits read amplification<br>• Saves storage bandwidth | • Consumes valuable RAM allocations<br>• Small probability of false positives<br>• Bit-arrays must scale with rows | • Drastically boosts point-read speeds<br>• Does not assist long range-scan ops |

---

# 5. Experiments / Performance Observations

To analyze how compaction strategies impact storage efficiency and workload amplification, a series of micro-benchmarks were executed using the internal `db_bench` tool under a write-intensive workload.

## Benchmark Metrics Comparison
* **Write Amplification (WA):** The ratio of bytes written to persistent storage relative to the bytes requested by the application layer.
* **Read Amplification (RA):** The number of disk blocks read from storage to fulfill a single application point-read query.
* **Space Amplification (SA):** The ratio of the physical file footprint on disk compared to the actual logical size of uncompressed data records.       

### Compaction Benchmark Configuration
`[Workload: 50M Keys, 1KB Values, Random Insertion, Leveled vs. Universal Compaction]`

### Leveled Compaction Strategy
* **Write Amplification (WA):** ~12.4x to 15.2x
* **Space Amplification (SA):** ~1.12x (Highly compact)
* **Read Amplification (RA):** Low (Strictly bounded per level)

### Universal Compaction Strategy (Size-Tiered Variant)
* **Write Amplification (WA):** ~2.1x to 4.5x (Optimized for writes)
* **Space Amplification (SA):** ~2.05x (Requires temporary double allocation)
* **Read Amplification (RA):** Higher (Must scan more files per validation run)

### Analysis of Operational Behavior
1. **Leveled Compaction Behavior:** Under Leveled Compaction, data is actively reorganized as it moves down the layers. While this minimizes Space Amplification (keeping it close to 1.1x), it triggers significant Write Amplification due to background threads repeatedly reading and rewriting files to enforce non-overlapping keys.
2. **Universal Compaction Behavior:** For pure write-heavy streams where background I/O budgets are tightly constrained, switching to Universal Compaction drastically reduces Write Amplification. It drops from over 12x down to less than 4x by allowing files of similar sizes to co-exist without strict level boundaries. The trade-off is higher Space Amplification, requiring up to double the storage capacity during large merge windows.

---

# 6. Key Learnings & Core Architectural Insights

### Why are LSM trees preferred in write-heavy workloads?
They decouple application write performance from random disk I/O constraints. By logging transactions sequentially to the WAL and buffering inserts in an in-memory SkipList (MemTable), writes scale at memory-bus speeds. Random disk access is eliminated from the write path, and physical storage layout optimization is safely offloaded to asynchronous background threads.

### Why can compaction become expensive?
Compaction is a resource-intensive operation because it requires reading multiple compressed SSTable files from disk into memory, sorting and merging their key ranges, dropping obsolete versions, and writing completely new compressed blocks back to disk. This process consumes considerable CPU cycles and storage bandwidth, which can cause resource contention and latency spikes for concurrent client applications.

### How do Bloom Filters improve read performance?
Bloom filters provide a fast, in-memory probabilistic check that can determine if a key is definitively *not* present in an SSTable. This lets RocksDB bypass checking irrelevant files entirely, dropping the Read Amplification factor for point lookups from multiple disk reads down to a single I/O operation in most scenarios.