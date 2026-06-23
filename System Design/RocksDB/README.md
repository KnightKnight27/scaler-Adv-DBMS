# Topic 4: RocksDB Architecture

## 1. Problem Background
Traditional B-Tree based storage engines suffer from "write amplification" when subjected to high-throughput random writes, as modifying a small record requires rewriting an entire page (e.g., 8KB) to disk. RocksDB was built by Facebook (based on Google's LevelDB) to optimize for fast storage hardware (SSDs) and write-heavy workloads using a Log-Structured Merge (LSM) tree.

## 2. Architecture Overview
Data flows through specific structures:
- **Write-Ahead Log (WAL)**: For durability.
- **MemTable**: In-memory data structure (usually a skip-list) where active writes go.
- **Immutable MemTable**: When a MemTable fills up, it becomes immutable and is flushed to disk.
- **SSTables (Sorted String Tables)**: Immutable disk files organized in levels (L0, L1, ..., Ln).

## 3. Internal Design
### The Write Path
Writes are appended to the WAL and inserted into the MemTable. This makes writes extremely fast since they are purely sequential I/O (WAL) and memory operations.

### The Read Path
Reads must check the MemTable first, then Immutable MemTables, then L0 SSTables, L1 SSTables, and so on. Since checking multiple files is slow, RocksDB uses **Bloom Filters** to quickly determine if a key *might* exist in an SSTable, skipping unnecessary disk reads.

### Compaction
To keep the number of SSTables manageable and remove deleted/overwritten data, background threads perform compaction. Compaction merges overlapping SSTables from a lower level into a new, larger SSTable at a higher level.

## 4. Design Trade-Offs
- **Advantages**: Excellent write throughput (low write amplification initially), highly tunable for different workloads, optimized for SSDs.
- **Limitations**: 
  - *Read Amplification*: A single read might need to check multiple SSTables.
  - *Space Amplification*: Old versions of data take up space until compacted.
  - *Compaction Stalls*: Heavy write workloads can cause compaction to fall behind, eventually blocking new writes.

## 5. Experiments / Observations
When benchmarking RocksDB, tweaking compaction styles (e.g., Leveled Compaction vs. Universal Compaction) dramatically alters performance characteristics. Leveled compaction generally provides better read performance and lower space amplification, but higher write amplification.

## 6. Key Learnings
RocksDB perfectly illustrates how modern databases are tailored for specific hardware and workloads. By embracing immutability and sequential writes through LSM-trees, it solves the random-write bottleneck of B-Trees, though it introduces new complexities around background compaction and read amplification.
