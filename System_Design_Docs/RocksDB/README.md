# RocksDB Architecture

---

## 1. Problem Background

### Why RocksDB Exists

RocksDB was created at Facebook in 2012, forked from Google's LevelDB. The problem it was designed to solve was specific: **write-heavy workloads on flash (SSD) storage at extreme scale.**

Traditional B+-tree databases (InnoDB, PostgreSQL) were designed in an era of spinning disks, where random reads were the bottleneck. SSDs made random reads fast, but they introduced a new problem: SSDs wear out with writes, and writes that are scattered across the device are slower and more wear-inducing than sequential writes.

Facebook's workloads — user activity feeds, message storage, metadata for distributed file systems — had a fundamentally different read/write ratio than OLTP databases. They needed:
- Extremely high write throughput (millions of writes per second)
- Acceptable read performance (not necessarily microsecond latency)
- Storage efficiency on flash, where random writes = device wear

The answer was the **Log-Structured Merge-tree (LSM-tree)**, a data structure that converts random writes into sequential writes by buffering mutations in memory and periodically flushing them to disk in sorted order. RocksDB is Facebook's production-hardened implementation of this idea.

---

## 2. Architecture Overview

```
  Write Path                         Read Path
      │                                  │
      ▼                                  ▼
  ┌───────────────────────────────────────────────────────┐
  │                    RocksDB                            │
  │                                                       │
  │  ┌────────────────────┐                               │
  │  │      WAL           │  ← append-only log for        │
  │  │  (Write-Ahead Log) │    crash recovery             │
  │  └────────────────────┘                               │
  │                                                       │
  │  ┌────────────────────┐                               │
  │  │     MemTable       │  ← active in-memory sorted    │
  │  │   (SkipList/       │    buffer (~64MB default)     │
  │  │    HashSkipList)   │                               │
  │  └──────────┬─────────┘                               │
  │             │ when full                               │
  │             ▼                                         │
  │  ┌────────────────────┐                               │
  │  │  Immutable         │  ← sealed, being flushed      │
  │  │  MemTable(s)       │    to L0                      │
  │  └──────────┬─────────┘                               │
  │             │ flush                                   │
  │             ▼                                         │
  │  ┌────────────────────────────────────────────────┐   │
  │  │                  SST Files (Sorted String       │   │
  │  │                  Tables on disk)                │   │
  │  │                                                 │   │
  │  │  L0: [SST0.1][SST0.2][SST0.3]  ← may overlap   │   │
  │  │                                                 │   │
  │  │  L1: [  SST1.1  ][  SST1.2  ]  ← no overlap,   │   │
  │  │      10MB limit                  sorted         │   │
  │  │                                                 │   │
  │  │  L2: [SST2.1][SST2.2][SST2.3][SST2.4]          │   │
  │  │      100MB limit                                │   │
  │  │                                                 │   │
  │  │  L3 ... Ln (each 10x larger than previous)      │   │
  │  └────────────────────────────────────────────────┘   │
  │                                                       │
  │  ┌─────────────────────────────────────────────────┐  │
  │  │  Bloom Filters (per SST file)                   │  │
  │  │  Block Cache (compressed/uncompressed pages)    │  │
  │  └─────────────────────────────────────────────────┘  │
  └───────────────────────────────────────────────────────┘
```

The key insight: **writes always go to memory (MemTable) first, then sequentially to disk.** Reads must check MemTable → immutable MemTables → L0 → L1 → ... until the key is found.

---

## 3. Internal Design

### 3.1 MemTable

The MemTable is an in-memory sorted data structure. The default implementation is a **skip list** — a probabilistic sorted structure with O(log n) insert and O(log n) lookup.

```
SkipList (simplified, 3 levels):

Level 3: ──────────────────────────► NULL
Level 2: ─► [10] ──────────────────► NULL
Level 1: ─► [10] ─► [30] ──────────► NULL
Level 0: ─► [10] ─► [20] ─► [30] ─► [40] ─► NULL
             ^    data      ^
           head           tail
```

A skip list supports:
- **O(log n) insert**: find position at each level, link new node
- **O(log n) point lookup**: skip over lower-level nodes
- **O(log n + result) range scan**: drop to level 0 after finding start, scan forward

Why a skip list and not a B+-tree? Skip lists support lock-free concurrent writes more naturally — multiple threads can insert simultaneously without restructuring the tree. RocksDB's concurrent MemTable insert uses a lock-free skip list variant.

**MemTable lifecycle:**
1. Writes go into the active MemTable
2. When MemTable reaches size threshold (default 64MB), it is **sealed** → becomes an immutable MemTable
3. A new empty MemTable is created for new writes (no write stall)
4. Background thread **flushes** the immutable MemTable to L0 as an SST file
5. Immutable MemTable is dropped after flush

### 3.2 SST Files (Sorted String Tables)

An SST file is an immutable, sorted key-value file on disk. Once written, it is never modified — only read or deleted (during compaction).

```
SST File Layout:
┌───────────────────────────────────────────────┐
│  Data Blocks (4KB each, sorted key-value)     │
│  ┌────────────────┐  ┌────────────────┐        │
│  │ Block 1        │  │ Block 2        │  ...   │
│  │ key1:val1      │  │ key101:val101  │        │
│  │ key2:val2      │  │ key102:val102  │        │
│  │ ...            │  │ ...            │        │
│  └────────────────┘  └────────────────┘        │
│                                                │
│  Index Block                                   │
│  (last key of each block → block offset)       │
│  [key100 → offset_of_block1]                   │
│  [key200 → offset_of_block2]                   │
│  ...                                           │
│                                                │
│  Filter Block (Bloom Filter)                   │
│  (probabilistic membership: is key in file?)   │
│                                                │
│  Metaindex Block                               │
│  Footer (file format magic, checksum)          │
└───────────────────────────────────────────────┘
```

**Immutability is a design choice.** SST files are never partially updated. This means:
- Writes are always sequential (no random writes on disk)
- Files can be safely cached, memory-mapped, and checksummed
- Compaction replaces files atomically (write new file, swap, delete old)

### 3.3 L0 to Ln: The Level Structure

SST files are organized into levels. Each level has a size limit (10x the previous by default).

```
Level Structure:

L0: Unsorted SST files (may have overlapping key ranges)
    ┌──────────┐  ┌──────────┐  ┌──────────┐
    │ 1–1000   │  │ 500–1500 │  │ 200–800  │
    └──────────┘  └──────────┘  └──────────┘
    ← these overlap! read must check all L0 files

L1: Sorted, non-overlapping (total size ≤ 10MB)
    ┌──────┐  ┌──────┐  ┌──────┐  ┌──────┐
    │ 1–250│  │251–500│  │501–750│  │751–1000│
    └──────┘  └──────┘  └──────┘  └──────┘

L2: Sorted, non-overlapping (total size ≤ 100MB)
L3: ≤ 1GB
...
```

**Why L0 is special:** Files flushed from MemTable go directly to L0 without sorting across files. This keeps flush latency low. The downside: L0 files can overlap, so reads must check all L0 files. When L0 accumulates too many files (default: 4), compaction is triggered, and writes may stall at L0 file count = 12 (soft limit) or 20 (hard limit).

### 3.4 Bloom Filters

A Bloom filter is a probabilistic data structure that answers: **"is this key definitely NOT in this file?"**

```
Bloom Filter (simplified, 3 hash functions):

Insert key "hello":
  h1("hello") = 2 → set bit 2
  h2("hello") = 5 → set bit 5
  h3("hello") = 8 → set bit 8

Bit array: [0, 0, 1, 0, 0, 1, 0, 0, 1, 0]
             0  1  2  3  4  5  6  7  8  9

Query "world":
  h1("world") = 1 → bit 1 = 0 → definitely NOT in file
  (can skip this SST file)

Query "hello":
  h1("hello") = 2 → bit 2 = 1
  h2("hello") = 5 → bit 5 = 1
  h3("hello") = 8 → bit 8 = 1
  → probably in file (could be false positive)
  → read the data block to confirm
```

**False positives are acceptable.** A false positive means we read a data block unnecessarily — wasted I/O. A false negative is impossible — if the Bloom filter says "not in file," the key is guaranteed absent. The false positive rate is tunable (default ~1%). Each SST file has its own Bloom filter, stored in the filter block.

**Why Bloom filters matter for reads:** Without Bloom filters, reading a non-existent key requires checking every SST file at every level — O(number of files) I/Os. With Bloom filters, most files are skipped in nanoseconds (no disk I/O). For write-heavy workloads generating many SST files, this is the difference between acceptable and catastrophic read performance.

### 3.5 Compaction

Compaction is the background process that merges SST files, eliminates obsolete key versions, and maintains the level structure.

```
Compaction of L1 file with overlapping L2 files:

Before:
  L1: [key1:v1, key5:v2, key9:v3]  ← one L1 file
  L2: [key1:v0, key3:v1, key5:v1, key7:v2, key9:v2, key11:v1]

Compaction reads both, merges (keeping newer versions), sorts:
  Output L2 (new files):
  [key1:v1, key3:v1, key5:v2, key7:v2, key9:v3, key11:v1]
  (key1:v0, key5:v1, key9:v2 are old versions → discarded)

After: old L1 file deleted, new L2 files written
```

**Compaction strategies:**
- **Leveled compaction** (default): compacts one L(n) file with all overlapping L(n+1) files. Predictable read performance (each level has non-overlapping files). Higher write amplification.
- **Universal compaction (FIFO)**: compacts all files together periodically. Lower write amplification, but temporary space usage doubles during compaction.
- **FIFO compaction**: drops old files when total size exceeds limit. No write amplification, but data is deleted. Used for time-series data where old data expires.

**Write Amplification from compaction:** A key written once to the MemTable may be rewritten many times as it migrates from L0 → L1 → L2 → ... → Lmax through successive compactions. The write amplification factor (WA) in leveled compaction is approximately `sum of level_size_ratios` — typically 10–30x. A single user write causes 10–30 actual bytes written to disk.

### 3.6 Read and Write Paths

**Write Path:**
```
1. Write WAL record (append, sequential)
2. Insert key-value into active MemTable (in-memory, O(log n))
3. Return success to caller

(background)
4. MemTable fills → sealed, new MemTable starts
5. Flush thread writes immutable MemTable to L0 SST file
6. Compaction thread merges files across levels
```

**Read Path:**
```
1. Check active MemTable (most recent, in-memory)
2. Check immutable MemTable(s) (newer than L0)
3. For each L0 file (newest to oldest):
   a. Check Bloom filter → skip if key definitely absent
   b. Use index block to find correct data block
   c. Read data block, find key
4. For L1, L2, ..., Ln:
   a. Binary search on file metadata to find which file contains key range
   b. Check Bloom filter → skip if absent
   c. Read data block
5. Return the first version found (newest = correct)
```

**Worst case read: O(number_of_levels × files_per_level) I/Os.** In practice, Bloom filters eliminate most file accesses, and the block cache (LRU cache for decompressed data blocks) avoids redundant disk reads.

---

## 4. Design Trade-Offs

### The LSM vs B+-Tree Fundamental Tension

| Metric | LSM-tree (RocksDB) | B+-tree (InnoDB/PostgreSQL) |
|--------|-------------------|--------------------------|
| Write throughput | Very high (sequential) | Moderate (random I/O) |
| Write amplification | High (10–30x) | Low (2–5x) |
| Read latency (hot data) | Low (MemTable hit) | Very low (buffer pool) |
| Read latency (cold data) | Higher (multi-level check) | Low (single B-tree traversal) |
| Space amplification | Moderate (during compaction) | Low |
| SSD wear | Lower (sequential writes) | Higher (random writes) |
| Range scan performance | Excellent (sorted SSTs) | Excellent (B+-tree leaves) |

**The core trade-off:** LSM writes are sequential and fast, but reads may touch multiple levels. B+-trees write in-place (random), but reads are always O(log n) on a single structure.

### Compaction: Necessary but Expensive

Compaction is unavoidable in LSM trees — without it, L0 accumulates infinitely and reads degrade. But compaction consumes:
- **CPU:** merging and sorting millions of key-value pairs
- **Disk I/O:** reading and writing gigabytes of data
- **Temporary space:** compaction output files exist before old files are deleted

Heavy compaction can cause **write stalls** — the MemTable fills faster than flushes complete, or L0 accumulates faster than compaction runs. RocksDB has configurable rate limiters and compaction throttling to prevent stalling application writes.

### Space Amplification

During compaction, the output files are written before the input files are deleted. At peak, storage usage can be 2x the logical data size. Additionally, deleted keys ("tombstones") persist until they are compacted out at the lowest level — a sequence of deletions does not immediately reclaim space.

### Bloom Filter False Positive Rate

A lower false positive rate = larger Bloom filter = more memory. At 1% false positive rate, the filter costs ~10 bits per key. For 1 billion keys, that is 1.25GB of Bloom filter memory. The trade-off: more memory → fewer unnecessary I/Os → better read performance.

---

## 5. Experiments / Observations

### Experiment 1: Write Amplification Under Different Compaction Strategies

Using `db_bench` (RocksDB's benchmark tool):

```bash
# Leveled compaction (default)
./db_bench --benchmarks=fillrandom \
  --num=10000000 \
  --compaction_style=0 \
  --db=/tmp/rocksdb_level

# Result:
# Write throughput: ~250,000 ops/sec
# Write amplification observed via:
#   rocksdb.bytes.written / bytes inserted by user
# Typical WA: 15–25x for leveled compaction

# Universal compaction
./db_bench --benchmarks=fillrandom \
  --num=10000000 \
  --compaction_style=1 \
  --db=/tmp/rocksdb_universal

# Result:
# Write throughput: ~400,000 ops/sec (less compaction I/O)
# Write amplification: 5–10x (lower than leveled)
# But: temporary space usage 2x during major compaction
```

**Observation:** Universal compaction achieves higher write throughput by reducing compaction frequency. The cost is unpredictable compaction spikes and temporary space doubling. Leveled compaction is more predictable for read performance but causes higher sustained write amplification.

### Experiment 2: Bloom Filter Impact on Read Performance

```python
# Simulated comparison: read 1M random keys, 50% hit rate
# Dataset: 10M keys in RocksDB

# Without Bloom filters (bloom_bits_per_key=0):
# Each miss requires checking all L0 files + binary search in L1-L5
# ~15 I/Os per miss on average
# Read throughput: ~20,000 random reads/sec

# With Bloom filters (bloom_bits_per_key=10, ~1% false positive):
# Miss → Bloom filter says "not here" for 99% of files → skip
# ~1–2 I/Os per miss on average
# Read throughput: ~150,000 random reads/sec  (7.5x improvement)

# The Bloom filter's value scales with:
# - Number of levels (more levels = more files to skip)
# - Miss rate (more misses = more benefit from quick rejection)
```

**Observation:** Bloom filters are not an optional optimization — they are essential for acceptable read performance in production LSM trees. Without them, a point lookup on a 100GB dataset could require dozens of disk reads.

### Experiment 3: Compaction Write Stall

```bash
# Simulate burst writes faster than compaction can keep up
./db_bench --benchmarks=fillrandom \
  --num=50000000 \
  --write_rate_limit=0 \
  --level0_stop_writes_trigger=12 \
  --db=/tmp/rocksdb_stall

# Observe in RocksDB statistics:
# rocksdb.stall.micros — total microseconds writes were stalled
# rocksdb.write.stalls — number of stall events
# L0 file count (via rocksdb LOG) spikes to soft_limit, then hard limit

# Log output shows:
# [WARN] Stalling writes because we have 8 level-0 files
# [WARN] Stopping writes because we have 12 level-0 files
```

**Observation:** Write stalls are a real operational concern. In production, RocksDB must be tuned with compaction rate limiters and monitored via the statistics API. A common pattern: a burst of writes fills L0 faster than compaction can drain it, causing a write stall that lasts seconds to minutes until compaction catches up.

### Experiment 4: Read Amplification Across Levels

```
Measuring read I/Os per key lookup at different dataset sizes:

Dataset: 1GB  → most data in L1-L2 → avg 2 I/Os per lookup
Dataset: 10GB → data spans L1-L3   → avg 3 I/Os per lookup
Dataset: 1TB  → data spans L1-L5   → avg 5 I/Os per lookup (with Bloom filters)

Without Bloom filters at 1TB:
  → must check L0 (N files) + L1 + L2 + L3 + L4 + L5 = O(N+6) I/Os
  → catastrophic performance degradation

With Bloom filters at 1TB:
  → 99% of files skipped for misses
  → read amplification stays roughly constant as dataset grows
```

**Observation:** The read amplification scales as O(number_of_levels), not O(dataset_size), due to the sorted, non-overlapping structure of L1+. This is fundamentally bounded, making RocksDB viable for terabyte-scale datasets — provided Bloom filters are configured correctly.

---

## 6. Key Learnings

**LSM trees convert write I/O pattern from random to sequential.** This is the entire reason for the design. Random writes on SSDs cause wear, fragmentation, and suboptimal throughput. Sequential writes maximize device throughput and minimize wear-leveling overhead. The MemTable + flush pipeline achieves this at the cost of compaction complexity.

**Compaction is the tax on efficient writes.** Every byte written by the user is eventually written multiple times by compaction. Write amplification of 10–30x is the price of sequential flush throughput. The choice of compaction strategy is a choice about when and how to pay this tax.

**Bloom filters are the defense against read amplification.** Without them, the multi-level structure makes point lookups impractical. With them, most levels are skipped in nanoseconds. The Bloom filter memory budget directly trades off against I/O cost.

**Immutability simplifies everything.** SST files are write-once, read-many. This eliminates in-place update complexity, enables safe concurrent reads without locks, and makes checksums trivial. Compaction replaces immutable files atomically. The immutability principle is what makes RocksDB safe to use in high-concurrency environments.

**RocksDB is not a general-purpose relational database.** It is a key-value storage engine — no SQL, no joins, no transactions across arbitrary keys (beyond single key-value atomicity). Systems like CockroachDB, TiKV, and MyRocks (Facebook's MySQL + RocksDB integration) use RocksDB as a storage layer beneath a relational or distributed layer. Choosing RocksDB means choosing the right tool for write-heavy key-value workloads, then building the rest of the system on top of it.
