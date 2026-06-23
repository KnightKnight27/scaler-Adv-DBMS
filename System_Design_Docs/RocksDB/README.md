# RocksDB Architecture: Design Discussion

## 1. Problem Background
RocksDB is an embedded, persistent key-value store developed by Facebook, forked from Google's LevelDB. It was built to maximize the potential of fast storage hardware (like Flash drives/SSDs) and multi-core processors. The primary problem it solves is handling extremely high write workloads where traditional B-Tree based databases suffer from high write amplification and random I/O bottlenecks.

## 2. Architecture Overview
RocksDB relies on a Log-Structured Merge-Tree (LSM-tree) architecture. Instead of updating data in place on disk, all writes are appended in memory and later flushed to disk in immutable files.

```mermaid
graph TD
    Client[Client Writer] --> MemTable[MemTable]
    Client --> WAL[Write-Ahead Log]
    MemTable -->|Full| ImmutMemTable[Immutable MemTable]
    ImmutMemTable -->|Flush| L0[Level 0 SSTables]
    
    subgraph On-Disk Storage (LSM Tree)
        L0 -->|Compaction| L1[Level 1 SSTables]
        L1 -->|Compaction| L2[Level 2 SSTables]
        L2 -->|Compaction| Ln[Level N SSTables]
    end
```

## 3. Internal Design

### Data Structures
- **MemTable:** An in-memory data structure (typically a SkipList) that stores the most recent writes. It keeps keys sorted.
- **Write-Ahead Log (WAL):** Sequential log on disk ensuring durability. Before data is added to the MemTable, it is written to the WAL.
- **Immutable MemTable:** When a MemTable reaches a certain size, it becomes immutable and a new MemTable is created. The immutable MemTable is placed in a queue to be flushed to disk.
- **SSTables (Sorted String Tables):** Immutable files on disk storing sorted key-value pairs. They are organized into levels (L0 to Ln).

### The LSM Tree and Levels (L0 to Ln)
- **Level 0 (L0):** Contains SSTables flushed directly from MemTables. Because they are dumped sequentially, keys in L0 files can overlap.
- **Levels 1 to N:** SSTables in these levels are strictly sorted and partitioned. There are no overlapping key ranges between files in the same level (for L1 and below). Each level is exponentially larger than the previous one (usually 10x).

### The Write Path
1. Write request arrives.
2. Appended to the WAL.
3. Inserted into the active MemTable.
4. Returns success to the client. (Extremely fast, purely sequential disk I/O and memory write).

### The Read Path
Reads must check structures in order of freshness:
1. Active MemTable.
2. Immutable MemTables.
3. L0 SSTables (must check all files where the key range overlaps).
4. L1 to Ln SSTables.
To speed this up, RocksDB uses **Bloom Filters**—probabilistic data structures attached to SSTables that quickly determine if a key is *not* in a file, saving expensive disk reads.

### Compaction
Compaction is a background process that merges overlapping SSTables from a higher level (e.g., L0) into a lower level (e.g., L1), removing deleted keys (tombstones) and resolving overwrites. It keeps the tree healthy and read operations fast.

## 4. Design Trade-Offs

### Amplification Metrics
LSM-trees are evaluated on three competing metrics:
1. **Write Amplification (WA):** Ratio of data written to disk vs. data written by the application. (Compaction causes WA since the same data is rewritten multiple times as it moves down levels).
2. **Read Amplification (RA):** Number of disk reads required to satisfy a single logical read. (Searching multiple levels and L0 files causes RA).
3. **Space Amplification (SA):** Ratio of disk space used vs. logical data size. (Old versions of keys take up space until compacted).

### Why LSM Trees for Write-Heavy Workloads?
B-Trees require random I/O for in-place updates and page splits. LSM-trees convert random writes into sequential writes (appending to WAL and flushing MemTables). This drastically improves write throughput on SSDs.

### Trade-offs
- **Advantage:** Unmatched write performance.
- **Advantage:** Highly tunable for different storage media.
- **Limitation:** Compaction can cause I/O spikes and CPU overhead, sometimes stalling foreground writes.
- **Limitation:** Read performance is generally slower than B-Trees due to checking multiple levels.

## 5. Experiments / Observations
**Benchmarking Amplification Under Compaction Strategies:**
When simulating workloads using `db_bench` (RocksDB's benchmarking tool):
- **Leveled Compaction (Default):** Provides a good balance. Low Space Amplification and reasonable Read Amplification, but higher Write Amplification.
- **Universal Compaction:** Designed for write-heavy workloads. It reduces Write Amplification significantly but increases Space Amplification (files are merged less frequently) and Read Amplification.

## 6. Key Learnings
- **The Core Compromise:** Database engineering is a zero-sum game of amplification. You can optimize for Write Amplification (Universal Compaction) or Space/Read Amplification (Leveled Compaction), but you cannot optimize all three simultaneously.
- **Bloom Filters are Magic:** Without Bloom Filters, the read performance of an LSM-tree would be catastrophically slow. They perfectly complement the "write-optimized" nature of the architecture by mitigating its read penalties.
- **Sequential is King:** RocksDB proves that adapting software architecture to respect hardware constraints (SSDs excel at sequential writes, struggle with random writes) yields massive performance gains.
