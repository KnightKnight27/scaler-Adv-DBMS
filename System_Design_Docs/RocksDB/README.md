# RocksDB Architecture: LSM-Tree Deep Dive

## 1. Overview

RocksDB is an embeddable, persistent key-value store developed by Facebook (Meta) based on Google's LevelDB. It uses a **Log-Structured Merge-Tree (LSM-Tree)** storage engine, which is fundamentally different from B-tree based engines like InnoDB or PostgreSQL's heap. LSM-trees are optimized for **write-heavy workloads** by converting random writes into sequential I/O.

---

## 2. LSM-Tree Architecture

### 2.1 High-Level Design

```
┌─────────────────────────────────────────────────────────────┐
│                      Write Path                             │
│                                                             │
│  Client Write                                               │
│       │                                                     │
│       ├──▶ WAL (Write-Ahead Log)                            │
│       │     Append-only log on disk                         │
│       │     Ensures durability before MemTable              │
│       │                                                     │
│       └──▶ MemTable (In-Memory Sorted Structure)            │
│             Red-black tree / Skip list                      │
│             All writes buffered here first                  │
│                   │                                         │
│                   │ (when full, typically 64MB)             │
│                   ▼                                         │
│             Immutable MemTable                              │
│                   │                                         │
│                   │ (flushed to disk)                       │
│                   ▼                                         │
│             SSTable (Sorted String Table)                   │
│             Stored in Level 0 (L0)                          │
│                                                             │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                      Read Path                                │
│                                                             │
│  Client Read                                                │
│       │                                                     │
│       ├──▶ MemTable (check first)                           │
│       │                                                     │
│       ├──▶ Immutable MemTables (check next)                   │
│       │                                                     │
│       ├──▶ Bloom Filters (eliminate SSTables)               │
│       │                                                     │
│       ├──▶ L0 SSTables (newest, may overlap)                │
│       │                                                     │
│       ├──▶ L1 SSTables (sorted, no overlap)                 │
│       ├──▶ L2 SSTables                                      │
│       └──▶ ... Ln SSTables (oldest, largest)                │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 Core Components

#### MemTable

The **MemTable** is an in-memory sorted data structure that buffers all writes:

- **Structure**: Typically a skiplist (O(log n) insert/lookup) or red-black tree
- **Size**: Configurable (default ~64MB)
- **Sorted**: Maintains keys in sorted order for efficient flush to SSTable
- **Mutable**: Active writes go here
- **Concurrency**: Single-writer (all writes serialized through a mutex), multi-reader

**Why Skiplist?**
- Lock-free concurrent reads
- Simple implementation, no rebalancing like B-trees
- Efficient range scans
- Good cache locality for sequential access

#### Immutable MemTable

When the MemTable reaches its size limit:
1. It becomes **read-only** (Immutable MemTable)
2. A new empty MemTable is created for subsequent writes
3. The Immutable MemTable is flushed to disk as an SSTable in the background

**Why Immutable?**
- Simplifies concurrency: readers can access old MemTable while new writes go to fresh one
- Background flush doesn't block writes
- Multiple Immutable MemTables can exist during high write load

#### SSTable (Sorted String Table)

**SSTables** are immutable, sorted files on disk:

```
SSTable File Structure:
┌────────────────────────────────────────┐
│ Data Blocks (16KB default)            │
│  ┌──────────────────────────────────┐  │
│  │ Block 1: Key-Value Pairs        │  │
│  │  - Sorted by key                │  │
│  │  - Prefix compression           │  │
│  │  - Restart points every 16 keys   │  │
│  └──────────────────────────────────┘  │
│  ┌──────────────────────────────────┐  │
│  │ Block 2: Key-Value Pairs          │  │
│  └──────────────────────────────────┘  │
│  ...                                    │
├────────────────────────────────────────┤
│ Index Block                             │
│  - Key → Block offset mapping           │
│  - Binary search to find block          │
├────────────────────────────────────────┤
│ Filter Block (Bloom Filter)             │
│  - "Is key possibly in this SSTable?"   │
│  - Eliminates unnecessary disk reads     │
├────────────────────────────────────────┤
│ Meta Block                              │
│  - Compression dictionary               │
│  - Properties (size, count, etc.)        │
├────────────────────────────────────────┤
│ Footer (fixed size)                     │
│  - Offsets to index, filter, meta       │
└────────────────────────────────────────┘
```

**Key Properties:**
- **Immutable**: Once written, never modified (simplifies caching and concurrency)
- **Sorted**: Enables efficient range scans and binary search
- **Block-based**: Data organized in compressed blocks for I/O efficiency
- **Index**: Sparse index allows skipping irrelevant blocks

#### WAL (Write-Ahead Log)

The WAL ensures durability before data reaches the MemTable:

```
WAL Structure:
┌────────────────────────────────────────┐
│ Record 1: (seq_num, key, value, type) │
│ Record 2: (seq_num, key, value, type) │
│ ...                                     │
└────────────────────────────────────────┘

type: kTypeValue (put) or kTypeDeletion (delete/tombstone)
```

**Process:**
1. Write key-value to WAL (append-only, sequential I/O)
2. fsync WAL to disk
3. Write to MemTable
4. Return success to client

**Recovery:**
- On restart, replay WAL records into MemTable
- Once MemTable is flushed to SSTable, corresponding WAL can be deleted

---

## 3. Storage Levels (L0 to Ln)

### 3.1 Leveled Compaction Structure

RocksDB organizes SSTables into **levels**, where each level is approximately 10x larger than the previous:

```
Level 0 (L0):  ~256MB total
  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐
  │SST 1│ │SST 2│ │SST 3│ │SST 4│  ← May overlap in key ranges
  └─────┘ └─────┘ └─────┘ └─────┘     (from different MemTable flushes)

Level 1 (L1):  ~2.5GB total
  ┌─────────┐ ┌─────────┐ ┌─────────┐
  │  SST 1  │ │  SST 2  │ │  SST 3  │  ← No overlap, sorted
  │ a-f     │ │ g-m     │ │ n-z     │     Each key appears in at most
  └─────────┘ └─────────┘ └─────────┘     one SSTable

Level 2 (L2):  ~25GB total
  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐
  │SST 1│ │SST 2│ │SST 3│ │SST 4│ │SST 5│ │SST 6│  ← No overlap
  └─────┘ └─────┘ └─────┘ └─────┘ └─────┘ └─────┘

Level 3 (L3):  ~250GB total
  ... (more SSTables, same non-overlapping property)

Level n: Continues growing, each level 10x larger
```

### 3.2 Level Properties

| Property | L0 | L1+ |
|----------|-----|-----|
| **Key Overlap** | Allowed (files may overlap) | Not allowed (each key in max 1 file) |
| **Size Limit** | ~4 files (configurable) | 10x previous level |
| **Source** | MemTable flushes | Compaction from previous level |
| **Read Cost** | Check all files | Binary search to find file, then block |

### 3.3 Compaction Process

**When L0 reaches its limit (too many files):**
```
L0 Compaction (L0 → L1):
  1. Select all L0 files (they all may overlap)
  2. Find all L1 files that overlap with selected L0 files
  3. Merge-sort all these files together
  4. Write new L1 files (non-overlapping)
  5. Delete old L0 and L1 files

Example:
  L0: [a-c], [b-d], [d-f]  → Select all
  L1: [a-e], [f-j]         → Select [a-e] (overlaps)

  Merge: [a-c] + [b-d] + [d-f] + [a-e] → [a-b], [c-d], [e-f]

  New L1: [a-b], [c-d], [e-f], [f-j] (still non-overlapping)
```

**When L1 reaches its limit:**
```
L1 → L2 Compaction:
  1. Select one L1 file (e.g., [c-d])
  2. Find all L2 files that overlap (e.g., [a-f])
  3. Merge-sort together
  4. Write new L2 files
  5. Delete old files
```

**Key Insight:** Higher levels compact fewer files at a time (one file from Ln, overlapping files from Ln+1), making compactions more incremental.

---

## 4. Bloom Filters

### 4.1 Why Bloom Filters?

Without Bloom Filters, a point lookup (`Get("key123")`) must:
1. Check MemTable
2. Check Immutable MemTables
3. Check every L0 SSTable (may overlap)
4. Check one SSTable per level in L1+ (binary search)

For a key that doesn't exist, this means checking **many SSTables** — expensive!

### 4.2 How Bloom Filters Work

A Bloom Filter is a probabilistic data structure that answers: "Is this key possibly in this SSTable?"

```
Structure: Bit array of m bits, k hash functions

Insert "key123":
  h1("key123") → set bit 5
  h2("key123") → set bit 17
  h3("key123") → set bit 42

Query "key123":
  h1("key123") → check bit 5 ✓
  h2("key123") → check bit 17 ✓
  h3("key123") → check bit 42 ✓
  Result: "Possibly present" (may be false positive)

Query "key999":
  h1("key999") → check bit 5 ✓
  h2("key999") → check bit 12 ✗
  Result: "Definitely NOT present" (no false negatives)
```

**Properties:**
- **No false negatives**: If filter says "not present", key is definitely not there
- **Possible false positives**: If filter says "present", key may or may not be there
- **Space efficient**: ~10 bits per key → ~1.2 bytes per key
- **No deletion**: SSTables are immutable, so filters don't need deletion

### 4.3 Bloom Filters in RocksDB

- **Per SSTable**: Each SSTable has its own Bloom Filter
- **In-memory**: Filters are loaded into memory (or block cache)
- **Level-specific**: Can configure different false positive rates per level
  - L0: Higher FP rate acceptable (fewer files)
  - L6: Lower FP rate (more files, more important to skip)
- **Prefix Bloom Filters**: For range queries, filter on key prefixes

**Impact:**
- Without Bloom Filters: May need to check 10+ SSTables for a missing key
- With Bloom Filters: Check 1-2 SSTables on average (99%+ elimination rate)

---

## 5. Read Path Deep Dive

### 5.1 Point Lookup (Get)

```
Get("key123"):

Step 1: Check MemTable
  ├─ Found → Return value
  └─ Not found → Continue

Step 2: Check Immutable MemTables (newest first)
  ├─ Found → Return value
  └─ Not found → Continue

Step 3: Check L0 SSTables (newest first)
  For each L0 SSTable:
    ├─ Bloom Filter says "not present" → Skip
    └─ Bloom Filter says "maybe" → Search block index → Search data block
       ├─ Found → Return value
       └─ Not found → Continue

Step 4: Check L1+ SSTables (one per level)
  For each level L1, L2, ...:
    1. Binary search SSTable filenames to find which file contains key
    2. Check Bloom Filter
    3. If "maybe", search index block → search data block
    4. Found → Return value
    5. Not found → Continue to next level

Step 5: Key not found → Return NotFound
```

### 5.2 Range Scan (Iterator)

```
Iterator over ["apple", "banana"):

Step 1: Create heap of iterators
  - MemTable iterator
  - Immutable MemTable iterators
  - L0 SSTable iterators (all of them)
  - L1+ SSTable iterators (one per level, found via binary search)

Step 2: Merge all iterators
  - Similar to k-way merge
  - At each step, find minimum key across all iterators
  - Skip deleted keys (tombstones)
  - Skip overwritten keys (newer version wins)

Step 3: Return merged stream
```

**Cost:** Range scans are expensive in LSM-trees because they must merge many iterators.

---

## 6. Write Path Deep Dive

### 6.1 Put Operation

```
Put("key", "value"):

Step 1: Serialize write
  - Append to WAL (sequential disk I/O)
  - fsync WAL (if sync writes enabled)

Step 2: Insert into MemTable
  - Add to skiplist: O(log n)
  - If key exists, overwrite (old version still in WAL)

Step 3: Check MemTable size
  - If size < threshold → Done
  - If size >= threshold → Trigger flush

Flush Process (background):
  1. Freeze MemTable → Immutable MemTable
  2. Create new empty MemTable
  3. Write Immutable MemTable to L0 SSTable
  4. Delete corresponding WAL segment
```

### 6.2 Delete Operation

```
Delete("key"):

Step 1: Write tombstone to WAL
  type = kTypeDeletion

Step 2: Insert tombstone into MemTable
  key → special deletion marker

Step 3: During reads, tombstones hide older values

Step 4: During compaction, tombstones are dropped
  (when no older SSTable could contain the key)
```

---

## 7. Compaction Strategies

### 7.1 Leveled Compaction (Default in RocksDB)

**Characteristics:**
- Each level is ~10x larger than previous
- L1+ SSTables have non-overlapping key ranges
- Compaction picks one file from Ln, merges with overlapping files in Ln+1

**Pros:**
- Excellent read amplification (at most 1 file per level)
- Low space amplification (~10% temporary overhead)
- Predictable read performance

**Cons:**
- High write amplification (10-30x)
- Compaction I/O can saturate disk
- Write stalls when compaction can't keep up

### 7.2 Universal Compaction (Size-Tiered)

**Characteristics:**
- Similar to Cassandra's STCS
- SSTables grouped by size into tiers
- When tier has enough files, merge them into next tier

**Pros:**
- Lower write amplification (~4-6x)
- Better for write-heavy workloads
- Simpler compaction scheduling

**Cons:**
- Higher read amplification (overlapping files within tiers)
- Higher space amplification (up to 2x during compaction)
- Less predictable read latency

### 7.3 FIFO Compaction

**Characteristics:**
- First-In-First-Out: oldest files deleted when size limit reached
- No merging of files
- Designed for TTL-based data (logs, time-series)

**Pros:**
- Minimal write amplification (1x — no rewriting)
- Very fast writes
- Simple implementation

**Cons:**
- No compaction means reads degrade over time
- Only suitable for data that ages out

### 7.4 Compaction Trade-offs

| Metric | Leveled | Universal | FIFO |
|--------|---------|-----------|------|
| **Write Amplification** | High (10-30x) | Medium (4-6x) | Low (1x) |
| **Read Amplification** | Low (1-2 files/level) | High (many files) | High (all files) |
| **Space Amplification** | Low (~10%) | High (~50-100%) | Low (no temp space) |
| **Best For** | Read-heavy, general | Write-heavy | TTL data |

---

## 8. Amplification Analysis

### 8.1 Write Amplification

**Definition:** Ratio of total bytes written to disk vs bytes written by application.

**Sources of Write Amplification:**
1. **WAL writes**: Every write is written to WAL (1x)
2. **MemTable flush**: MemTable written to L0 SSTable (1x)
3. **L0→L1 compaction**: Data rewritten during merge (~1-2x)
4. **Ln→Ln+1 compactions**: Data rewritten at each level (~10x cumulative)

**Total Write Amplification (Leveled):**
```
WAL:              1x
Flush to L0:      1x
L0→L1:           ~1x
L1→L2:           ~1x
L2→L3:           ~1x
... (6 levels)

Total: ~10-30x depending on configuration
```

**Why This Matters:**
- SSDs have limited write endurance (TBW — Terabytes Written)
- High write amplification reduces SSD lifespan
- Each application write becomes 10-30 physical writes

### 8.2 Read Amplification

**Definition:** Number of disk reads per logical read operation.

**Sources of Read Amplification:**
1. **MemTable checks**: 1-2 memory lookups
2. **L0 scans**: Check all L0 files (up to 4)
3. **L1+ binary search**: 1 file per level
4. **Block reads**: Load index block + data block from disk

**Point Read (existing key):**
```
MemTable:         1 memory lookup
Immutable:        0-1 memory lookup
Bloom Filters:    ~1-2 false positives across levels
SSTable reads:    1-2 data blocks from disk

Total: ~1-3 disk reads (excellent)
```

**Point Read (missing key):**
```
Without Bloom Filters: Check all files → 10+ disk reads
With Bloom Filters:    ~1-2 false positives → 1-2 disk reads
```

**Range Scan:**
```
Must merge iterators from:
  - MemTable
  - Immutable MemTables
  - All L0 files
  - One file per level in L1+

Total: O(number_of_levels + l0_files) disk seeks
```

### 8.3 Space Amplification

**Definition:** Ratio of actual disk space used vs logical data size.

**Sources of Space Amplification:**
1. **Obsolete versions**: Old values not yet compacted away
2. **Tombstones**: Delete markers waiting to be dropped
3. **Compaction temporary space**: Input + output files coexist during compaction

**Leveled Compaction:**
- At most ~10% temporary overhead during compaction
- Old versions exist in at most one extra level
- Space amplification: ~1.1-1.2x

**Universal Compaction:**
- Up to 2x space during compaction (input + output)
- Multiple versions may coexist across tiers
- Space amplification: ~1.3-2.0x

---

## 9. Why LSM Trees Are Optimized for Writes

### 9.1 The Fundamental Insight

**B-Tree Write:**
```
1. Find leaf page (random read)
2. Modify page in memory
3. Write page to disk (random write)
4. If page splits: write new page, update parent (more random I/O)

Result: Random I/O, in-place modification, page splits
```

**LSM-Tree Write:**
```
1. Append to WAL (sequential write)
2. Insert into MemTable (memory operation)
3. Later: flush MemTable to SSTable (sequential write)
4. Later: compaction merges SSTables (sequential reads + writes)

Result: Sequential I/O only, no in-place modification
```

### 9.2 Hardware Alignment

Modern storage is optimized for sequential I/O:

| Operation | HDD | SSD |
|-----------|-----|-----|
| Random 4KB read | 10ms | 0.1ms |
| Sequential 4KB read | 0.01ms | 0.01ms |
| Random 4KB write | 10ms | 0.1ms |
| Sequential 4KB write | 0.01ms | 0.01ms |

LSM-trees convert ALL writes to sequential I/O, achieving:
- **HDDs**: 100-1000x better write throughput than B-trees
- **SSDs**: Better endurance (sequential writes are more efficient)

### 9.3 Write Buffering

The MemTable acts as a write buffer:
- Absorbs write spikes
- Batches writes into larger sequential flushes
- Sorts keys before writing to disk (enables efficient compaction)

---

## 10. Why Compaction Becomes Expensive

### 10.1 The Compaction Debt Problem

Compaction is **deferred work** — it doesn't happen during the write, but it MUST happen eventually:

```
Write Rate: 100 MB/s
Compaction can't keep up: 80 MB/s

Result:
  L0 files accumulate → read amplification increases
  Disk fills up with uncompacted data
  Eventually: Write stalls (no space, too many L0 files)
```

### 10.2 Write Amplification vs. Write Rate

As write rate increases:
- More MemTable flushes → more L0 files
- More L0 files → more frequent L0→L1 compactions
- Larger L1 → more frequent L1→L2 compactions
- Cascade effect: compaction I/O grows with write rate

**The Catch-22:**
- Faster writes → more compaction needed
- Compaction uses disk I/O → competes with writes
- If compaction falls behind → write stalls

### 10.3 Strategies to Manage Compaction Cost

1. **Rate Limiting**: `rate_limiter` controls compaction I/O bandwidth
2. **Subcompactions**: Split large compactions into parallel smaller ones
3. **Tiered Storage**: Hot data in fast SSD, cold data in slow disk
4. **Dynamic Level Sizes**: Adjust level multiplier based on workload
5. **Periodic Manual Compaction**: Force compaction during low-traffic periods

---

## 11. Key Architectural Insights

1. **LSM-trees trade read performance for write performance**: By deferring and batching writes, they achieve extraordinary write throughput at the cost of read complexity and compaction overhead.

2. **Immutability is the key design principle**: SSTables are never modified. This eliminates locking during reads, simplifies caching, and enables efficient snapshots.

3. **Bloom Filters are essential for read performance**: Without them, point lookups on missing keys would be O(number of SSTables). With them, it's O(1) disk reads.

4. **Compaction is the Achilles' heel**: It's background work that must keep up with foreground writes. If it falls behind, the system degrades or stalls.

5. **The three amplifications are in tension**: You can optimize for two at the expense of the third. Leveled optimizes read + space; universal optimizes write + simplicity.

6. **WAL + MemTable + SSTable is a universal pattern**: This architecture appears in Cassandra, HBase, Bigtable, ScyllaDB, and many other systems. Understanding RocksDB teaches you all of them.

---

## 12. Real-World Usage of RocksDB

- **MyRocks**: MySQL storage engine using RocksDB (Facebook uses this for massive scale)
- **CockroachDB**: Distributed SQL database built on RocksDB
- **TiKV**: Distributed key-value store (TiDB's storage layer)
- **Apache Kafka**: Uses RocksDB for state stores in Kafka Streams
- **Flink**: RocksDB state backend for stream processing
- **Meta (Facebook)**: Used in ZippyDB (distributed KV), Laser (ML feature store)

---

## 13. References

- RocksDB GitHub: https://github.com/facebook/rocksdb
- "LSM Trees — The Complete Guide": https://medium.com/@harshithgowdakt/lsm-trees-the-complete-guide-to-wal-memtables-sstables-compaction-bloom-filters-7ddde77935f4
- "The Engineering Behind LSM Trees": https://dev.to/agp_marka_62a62d1cdadad70/distributed-database-internals-the-engineering-behind-log-structured-merge-lsm-trees-2258
- "An In-depth Discussion on the LSM Compaction Mechanism": https://www.alibabacloud.com/blog/an-in-depth-discussion-on-the-lsm-compaction-mechanism_596780
- "LSM Trees Explained": https://newsletter.systemdesigncodex.com/p/lsm-trees-explained
