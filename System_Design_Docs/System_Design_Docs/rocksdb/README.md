# RocksDB Architecture: Log-Structured Merge Trees in Practice

**Author:** Bhavya Jain  
**Roll Number:** 23bcs10088

## Problem Background

For many years, B-trees were the undisputed champions of on-disk storage. Their balanced design offered a reliable middle ground for both reads and writes. However, as hardware evolved, the fundamental cost of in-place updates became more apparent. Modifying even a small piece of data in a B-tree often requires a "read-modify-write" cycle of an entire page. On modern SSDs, this doesn't just mean latency; it translates to write amplification that can significantly shorten the lifespan of the underlying flash memory.

Log-Structured Merge (LSM) trees offer a radical alternative. Instead of in-place updates, LSM trees treat every write as a sequential append to an in-memory buffer. These buffers are eventually flushed to disk as immutable files. The heavy lifting — merging data and reclaiming space — is deferred to a background process known as compaction. This shift in philosophy yields write speeds that far outstrip traditional B-trees, though it introduces new complexities in read management and compaction scheduling.

RocksDB, a project matured at Facebook and now part of the Apache family, is the industry standard for LSM tree implementations. As an embedded key-value store, it powers some of the world's most critical data infrastructure, from MyRocks in MySQL to the state backends of Apache Flink.

## Architecture Overview

RocksDB maintains a hierarchy of sorted data structures. The hottest data resides in an in-memory skip list called the MemTable. When the MemTable reaches a size threshold, it is frozen (becoming an Immutable MemTable) and flushed to disk as an SSTable (Sorted String Table) file at Level 0. Level 0 files may have overlapping key ranges because each file is a direct dump of a MemTable. Level 1 and all deeper levels are sorted and non-overlapping — each level covers the entire key space exactly once. As data ages, compaction merges files from higher levels into lower levels, maintaining the sorted invariants.

```
                     +-----------------------+
                     |    MemTable           |   ← mutable, in memory (skip list)
                     +-----------+-----------+
                                 |
                     +-----------v-----------+
                     | Immutable MemTable    |   ← frozen, waiting for flush
                     +-----------+-----------+
                                 |
          +----------------------v----------------------+
          |                L0 SSTables                  |   ← overlapping key ranges
          +----------------------+----------------------+
                                 |
          +----------------------v----------------------+
          |                L1 SSTable                   |   ← sorted, non-overlapping
          +----------------------+----------------------+
                                 |
          +----------------------v----------------------+
          |                L2 SSTables                  |   ← sorted, non-overlapping
          +----------------------+----------------------+
                                 |
                                ...
                                 |
          +----------------------v----------------------+
          |                Ln SSTables                  |   ← largest, oldest data
          +---------------------------------------------+
```

The write path is append-only and sequential. The read path searches through each layer from hottest to coldest. Compaction runs asynchronously to prevent the hierarchy from accumulating excessive overlap and stale versions.

## Internal Design

### MemTable and Write-Ahead Log

Every write operation lands first in the active MemTable, an in-memory skip list. Skip lists are probabilistic balanced data structures composed of multiple levels of linked lists with forward pointers. They provide O(log n) average-case insertion and lookup with simpler lock-free concurrency properties than balanced binary trees. The MemTable is mutable and all writer threads insert into it concurrently.

Before the MemTable is modified, the write is appended to a Write-Ahead Log (WAL) file on disk. The WAL exists purely for crash recovery — if the process terminates while data exists only in memory, the WAL is replayed on restart to reconstruct the MemTable state.

The WAL write is the only synchronous disk operation in the write path. Once the WAL record is durable, the write returns to the caller. MemTable insertion, flushing, and compaction all proceed asynchronously on background threads.

### Immutable MemTable and Flushing

When the active MemTable reaches a configurable size threshold, it is frozen and becomes an Immutable MemTable. A new, empty MemTable takes over for active writes. This double-buffering ensures that write latency remains predictable — the brief freeze is negligible compared to the time required to serialize and write an SSTable to disk.

A background thread flushes the Immutable MemTable to disk as a new L0 SSTable. Once the flush completes, the corresponding WAL segment is no longer needed and is deleted.

### SSTables

An SSTable is an immutable file on disk containing sorted key-value pairs. RocksDB uses a block-based internal format. The file is divided into data blocks (actual key-value pairs, compressed with Snappy, Zstd, or LZ4), index blocks (offsets to data blocks for binary search), and filter blocks (bloom filters for probabilistic membership testing).

```
SSTable Internal Layout:

+------------------+------------------+------------------+
|   Index Block    |  Filter Block    |  Data Blocks     |
|  (offsets into   |  (bloom filters  |  (sorted key-    |
|   data blocks)   |   per block)     |   value pairs)   |
+------------------+------------------+------------------+
```

When a key lookup reaches an SSTable file, RocksDB consults the index block to identify candidate data blocks, then checks the bloom filter for each candidate. Only when both indicate a possible match does the system decompress and scan the data block.

### Level Structure

Level 0 is distinct from all deeper levels because its files may overlap in key range. Each MemTable flush creates a new L0 file covering whatever key range was present in that MemTable. A lookup in L0 may need to check multiple files.

Levels 1 through N are sorted and non-overlapping. At each level, the key space is partitioned into files that collectively cover the entire range without gaps or overlaps. A lookup in L1+ performs a binary search on file boundaries to identify exactly one candidate file per level. Each level is typically 10× larger than the level above it; this exponential sizing keeps the total number of levels logarithmic in the dataset size even for terabyte-scale databases.

### Bloom Filters

A bloom filter is a probabilistic set-membership data structure. It consists of a bit array and a set of hash functions. To insert a key, each hash function maps the key to a bit position, and those bits are set to 1. To test membership, the same hash functions are applied; if any bit is 0, the key is definitively absent. If all bits are 1, the key is *probably* present — hash collisions can cause false positives, but false negatives are impossible.

```
Bloom filter example (3 hash functions, 10-bit array):

Insert 'hello':
  h1('hello') = 2 → set bit 2
  h2('hello') = 5 → set bit 5
  h3('hello') = 9 → set bit 9

  Array: [0,0,1,0,0,1,0,0,0,1]

Test 'world':
  h1('world') = 2 → bit 2 is 1
  h2('world') = 7 → bit 7 is 0 → DEFINITELY NOT PRESENT

Test 'foo':
  h1('foo') = 5 → bit 5 is 1
  h2('foo') = 9 → bit 9 is 1
  h3('foo') = 2 → bit 2 is 1
  All 1s → PROBABLY PRESENT (could be false positive)
```

RocksDB maintains bloom filters at the SSTable file level and optionally at the data block level. A filter check is a memory operation; a false positive leads to an unnecessary disk read. The false positive rate is tunable via the number of bits allocated per key (default 10 bits, yielding approximately 1% false positive rate). For a billion keys, 10 bits per key requires about 1.25 GB of memory — typically a worthwhile investment given the disk I/O it eliminates.

### Write Path

A `Put(key, value)` operation proceeds as follows:

1. Append the operation to the WAL file (sequential disk write).
2. Insert the key-value pair into the active MemTable (in-memory skip list).
3. Return to the caller — the write is considered durable once the WAL record is on disk.
4. (Asynchronous) When the MemTable reaches its size threshold, freeze it and start a new MemTable.
5. (Asynchronous) Flush the Immutable MemTable to a new L0 SSTable.
6. (Asynchronous) Compaction merges L0 files into L1, then L1 to L2, and so on.

### Read Path

A `Get(key)` operation searches from hottest to coldest:

1. Search the active MemTable.
2. If not found, search the Immutable MemTable (if one exists).
3. If not found, search L0 SSTables — all of them (because ranges overlap), but check each file's bloom filter first.
4. If not found, search L1 through Ln — exactly one candidate file per level (binary search on boundaries), bloom filter check, then data block read.
5. If still not found, return "not found."

If the key exists in the active MemTable, the lookup completes with zero disk I/O — a significant advantage for workloads with temporal locality.

### Compaction

Compaction is the background process that merges SSTable files to maintain the level structure, remove overwritten or deleted entries, and reclaim space. Without compaction, L0 would accumulate unbounded overlap (every MemTable flush adds a new file), reads would degrade linearly with the number of L0 files, and disk space would grow with stale versions.

```
Compaction example (L0 → L1):

Before:
  L0: [A–D] [C–F] [E–H]      ← overlapping, messy
  L1: [A–C] [D–F] [G–I]      ← sorted, non-overlapping

Compaction selects L0 files and overlapping L1 files,
merges sorted streams, removes deleted entries:

After:
  L0: (empty)
  L1: [A–D] [E–I]             ← sorted, non-overlapping
```

Compaction is the dominant source of write amplification in LSM trees. A single logical write may be rewritten at each level boundary as it descends through levels, leading to a write amplification factor of 10–30× under leveled compaction.

## Design Trade-Offs

### The RUM Conjecture

Storage systems balance three amplification factors: **Read amplification** (disk reads per logical read), **Write amplification** (disk bytes written per logical byte written), and **Space amplification** (total disk usage relative to live data size). The RUM conjecture states that optimizing any two comes at the expense of the third. RocksDB makes this trade-off explicit through its compaction strategy selection.

### Compaction Strategies

**Leveled Compaction** (default): Merges files progressively through levels. Achieves the best read amplification (at most one file per level) and best space amplification (~1.1×). Write amplification is worst (10–30×), as data is rewritten at each level boundary.

**Universal (Size-Tiered) Compaction**: Groups files of similar sizes into tiers and merges them less frequently. Write amplification is lower (3–6×) because data is rewritten fewer times. Read amplification is worse because files within a tier may overlap, requiring more files to be checked per lookup. Space amplification is higher (1.3–2.0×).

**FIFO Compaction**: No merging. Files are deleted when they exceed a time or size threshold. Write amplification is near 1× for fresh data; reads for expired data fail; space amplification is bounded by the retention policy. Suitable for caching and time-series data where old data naturally expires.

### Bloom Filter Memory Trade-Off

Bloom filters reduce read amplification by eliminating unnecessary SSTable file reads for absent keys. More bits per key → lower false positive rate → fewer wasted disk reads → more memory consumed. The default 10 bits per key balances these concerns for general workloads, but the parameter is tunable for applications where memory is either scarce or abundant relative to the dataset size.

## Experiments and Observations

RocksDB includes a benchmarking tool, `db_bench`, which was used to evaluate performance characteristics on a local NVMe SSD.

**Write throughput under leveled compaction**: The `fillrandom` benchmark (inserting keys in random order) initially achieved approximately 500,000 operations per second. Throughput decreased in stepwise drops as L0 filled and compaction began consuming disk I/O bandwidth. This is characteristic LSM behavior — the system transitions from memory-bound (fast) to compaction-bound (slower) as the dataset grows beyond the MemTable capacity.

**Read latency distribution**: `readrandom` exhibited a bimodal latency distribution. Hits in the active MemTable: approximately 1 microsecond. Hits in shallow SSTable levels with warm page cache: 10–50 microseconds. Hits in deep levels with cold page cache: 50–200 microseconds. The long tail is a consequence of the multiple levels a lookup must traverse for keys not resident in memory.

**Write stalls**: When compaction could not keep pace with the incoming write rate, RocksDB intentionally throttled foreground writes. This backpressure mechanism prevents L0 from growing without bound, which would cause read latency to spike as every point lookup would need to check an ever-growing number of L0 files.

**Compaction strategy impact**: 
- Leveled compaction: write amplification ~15×, read amplification low (1 file per level), space amplification ~1.1×.
- Universal compaction: write amplification ~4×, read amplification moderate (multiple files per tier), space amplification ~1.5×.
- FIFO compaction: write amplification ~1×, space bounded by retention, old data unreadable.

**Bloom filter effectiveness**: Without bloom filters, universal compaction read latency degraded significantly when the key did not exist — every SSTable file was opened and checked. With bloom filters at 10 bits per key, negative lookups required opening only 1–2 files on average.

## Production Deployments

**MyRocks**: Facebook developed MyRocks as an InnoDB replacement for their largest MySQL deployments. It reduced storage usage by approximately 50% and improved write throughput for their social-graph workloads. The trade-off was slightly higher read latency for some queries.

**TiKV**: The distributed key-value layer of TiDB uses RocksDB on each node for local persistence. RocksDB handles single-node storage and compaction; TiKV layers distributed transactions and replication on top.

**Apache Flink**: Flink's state backend uses RocksDB for storing keyed state that exceeds available JVM heap. The LSM architecture handles the high-velocity updates characteristic of stream processing.

In each case, the workload is write-intensive, can tolerate moderate read amplification, and has sufficient memory for bloom filters and block caches — the conditions under which LSM trees outperform B-trees.

## Key Learnings

**LSM trees represent a fundamental shift in storage assumptions.** While B-trees strive for a balance between reads and writes, LSM trees prioritize write efficiency above all else, deferring the "cleanup" cost. This makes them ideal for the write-heavy, append-only workloads that dominate modern web and telemetry data.

**The real engineering magic of an LSM tree lies in its compaction logic.** While the write path is elegantly simple, the background compaction process determines the system's overall health. Tuning an LSM-based engine like RocksDB is, at its heart, an exercise in finding the right balance for compaction frequency and strategy.

**Bloom filters are the "secret sauce" that makes LSM reads viable.** Without these probabilistic structures, the cost of checking multiple levels for a missing key would be prohibitive. My analysis highlights how a tiny amount of memory spent on bloom filters can eliminate the vast majority of unnecessary disk I/O.

**The MemTable-to-SSTable pipeline is a robust architectural pattern for modern ingest.** By separating fast, in-memory updates from structured, on-disk persistence and background optimization, this pattern (seen in LevelDB and Cassandra as well) provides a scalable blueprint for high-frequency data handling.

**LSM trees aren't a "silver bullet," but they are a powerful tool for specific niches.** They excel in scenarios with high write velocity and temporal locality (where recent data is most relevant). For read-heavy apps with strict latency bounds, the predictable performance of a B-tree might still be preferable. Understanding these trade-offs is the mark of a true storage engineer.
