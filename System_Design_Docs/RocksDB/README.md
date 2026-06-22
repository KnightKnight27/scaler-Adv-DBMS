# RocksDB Architecture

## 1. Problem Background

### The Write Amplification Problem

Traditional B-tree based databases (PostgreSQL, MySQL/InnoDB) optimize for **read performance** at the cost of write efficiency. When you update a small record in a B-tree, the database must:
1. Read the entire page containing the record (typically 4-16 KB)
2. Modify the record in memory
3. Write the entire page back to disk

This means a 100-byte update can trigger a 16KB page write — a **160x write amplification**. For write-heavy workloads (time-series data, event logging, messaging systems), this becomes a significant performance bottleneck.

### Enter LSM-Trees

The **Log-Structured Merge-Tree (LSM-Tree)**, proposed by Patrick O'Neil et al. in 1996, inverts the traditional trade-off: it optimizes for **write throughput** by converting random writes into sequential writes, at the cost of read performance.

**RocksDB** is Facebook's production implementation of an LSM-tree based key-value store. It was forked from Google's LevelDB in 2012 because Facebook needed:
- Higher write throughput for their social graph storage
- Better multi-core utilization
- More tunable compaction strategies
- Production-grade features (backups, statistics, rate limiting)

### Where RocksDB Is Used

RocksDB is not a standalone database — it's an **embeddable storage engine** used as the foundation for larger systems:

| System | Role of RocksDB |
|---|---|
| **CockroachDB** | Storage engine for distributed SQL |
| **TiKV** (TiDB) | Distributed key-value layer |
| **Apache Flink** | State backend for stream processing |
| **Meta/Facebook** | MyRocks (MySQL + RocksDB), ZippyDB |
| **Apache Kafka Streams** | State store for stream processing |
| **Yugabyte** | Document and key-value storage layer |

---

## 2. Architecture Overview

### High-Level Data Flow

```
                          Write Path
                              │
                              ▼
                    ┌──────────────────┐
                    │   Write Ahead    │    Sequential append (durable)
                    │   Log (WAL)      │
                    └────────┬─────────┘
                             │
                    ┌────────▼─────────┐
                    │    MemTable      │    In-memory sorted structure
                    │  (Active, mutable│    (SkipList by default)
                    │   write target)  │
                    └────────┬─────────┘
                             │ (when full → becomes immutable)
                    ┌────────▼─────────┐
                    │ Immutable        │    Read-only, waiting to be
                    │ MemTable(s)      │    flushed to disk
                    └────────┬─────────┘
                             │ (flush to disk)
                             ▼
    ┌────────────────────────────────────────────────────┐
    │                   SSTable Files                     │
    │                                                    │
    │   Level 0:  ┌────┐ ┌────┐ ┌────┐ ┌────┐           │
    │   (unsorted │SST1│ │SST2│ │SST3│ │SST4│           │
    │    by key   └────┘ └────┘ └────┘ └────┘           │
    │    range,   Ranges may OVERLAP                     │
    │    sorted                                          │
    │    within)        │ compaction                      │
    │                   ▼                                │
    │   Level 1:  ┌────┐ ┌────┐ ┌────┐                  │
    │   (sorted,  │SST │ │SST │ │SST │   Non-overlapping │
    │    parti-   │a-f │ │g-m │ │n-z │   key ranges      │
    │    tioned)  └────┘ └────┘ └────┘                  │
    │                   │ compaction                      │
    │                   ▼                                │
    │   Level 2:  ┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐     │
    │   (larger,  │  ││  ││  ││  ││  ││  ││  ││  │     │
    │    10x L1)  └──┘└──┘└──┘└──┘└──┘└──┘└──┘└──┘     │
    │                   │ compaction                      │
    │                   ▼                                │
    │   Level N:  ┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐     │
    │   (largest) ││││││││││││││││││││││││││││││││     │
    │             └┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘└┘     │
    └────────────────────────────────────────────────────┘
```

### Key Insight: Why This Architecture Is Write-Optimized

In a B-tree, a write modifies a page **in place** (random I/O):
```
B-tree write: Seek to page → Read → Modify → Write back (RANDOM I/O)
```

In an LSM-tree, a write appends to an in-memory buffer, which is later flushed as a sorted file (sequential I/O):
```
LSM write: Append to MemTable (MEMORY) → Eventually flush to SST (SEQUENTIAL I/O)
```

**Sequential writes are 10-100x faster than random writes** on both HDDs (no seek time) and SSDs (aligned to erase blocks, reduced write amplification at the flash layer).

---

## 3. Internal Design

### 3.1 MemTable

The MemTable is RocksDB's **write buffer** — an in-memory sorted data structure that absorbs writes.

```
MemTable Architecture:

    Put(key="user:100", value="Alice")
    Put(key="user:200", value="Bob")
    Delete(key="user:50")
    Put(key="user:150", value="Carol")
                │
                ▼
    ┌──────────────────────────────────┐
    │         Active MemTable          │
    │       (SkipList / HashSkipList)   │
    │                                  │
    │   Level 3:  ─────── user:200 ──────────
    │   Level 2:  ── user:100 ── user:200 ───
    │   Level 1:  user:50(DEL) user:100 user:150 user:200
    │   Level 0:  user:50(DEL) user:100 user:150 user:200
    │                                  │
    │   Size: write_buffer_size        │
    │   (default: 64MB)               │
    └──────────────────────────────────┘
```

**Why SkipList?**
- O(log n) insert, lookup, and iteration
- Lock-free concurrent reads (using atomic operations)
- Ordered iteration is natural (needed for sorted flush to SST)
- Simpler to implement concurrently than balanced BSTs

**MemTable lifecycle:**
```
1. Active MemTable receives writes
2. When size reaches write_buffer_size → switch:
   - Current MemTable becomes IMMUTABLE
   - New empty MemTable becomes active
3. Background thread flushes Immutable MemTable to L0 SST
4. After flush completes, Immutable MemTable memory is freed

    Active MemTable ──switch──► Immutable MemTable ──flush──► L0 SST File
       (writable)                  (read-only)                (on disk)
```

**Configuration**: `max_write_buffer_number` controls how many MemTables can exist simultaneously (1 active + N-1 immutable). If all buffers are full and flush can't keep up, writes **stall** — a critical backpressure mechanism.

### 3.2 Write-Ahead Log (WAL)

Every write to the MemTable is first recorded in the WAL for durability:

```
Write Operation:
1. Append write record to WAL file (sequential I/O + fsync)
2. Insert into MemTable (in-memory)
3. Return success to caller

WAL File Structure:
┌──────────────────────────────────────────┐
│  Block 0 (32KB)                          │
│  ┌────────────────────────────────────┐  │
│  │ Record: Put(user:100, Alice)       │  │
│  │ Record: Put(user:200, Bob)         │  │
│  │ Record: Delete(user:50)            │  │
│  │ Record: WriteBatch[...]            │  │
│  └────────────────────────────────────┘  │
├──────────────────────────────────────────┤
│  Block 1 (32KB)                          │
│  ┌────────────────────────────────────┐  │
│  │ Record: Put(user:150, Carol)       │  │
│  │ ...                                │  │
│  └────────────────────────────────────┘  │
└──────────────────────────────────────────┘
```

**WAL lifecycle**: When a MemTable is successfully flushed to an SST file, the corresponding WAL file is **deleted** (or archived). The WAL only needs to survive long enough to protect unflushed MemTable data.

**Trade-off: `sync_wal` option**:
- `sync_wal = true`: fsync on every write → durable but slower
- `sync_wal = false`: OS decides when to flush → faster but may lose recent writes on crash
- **WriteBatch**: Group multiple operations into a single WAL record for atomic, efficient writes

### 3.3 SSTables (Sorted String Tables)

SST files are the **immutable, on-disk storage format**. Each file contains sorted key-value pairs with indexing metadata.

```
SST File Structure:

┌──────────────────────────────────────────────────┐
│                                                  │
│  Data Blocks:                                    │
│  ┌────────────────────────────────────────────┐  │
│  │ Data Block 0 (default 4KB)                 │  │
│  │ ┌──────────────────────────────────────┐   │  │
│  │ │ Shared Key Prefix                   │   │  │
│  │ │ Entry: key=user:100, val=Alice      │   │  │
│  │ │ Entry: key=user:101, val=Bob        │   │  │
│  │ │ Entry: key=user:102, val=Carol      │   │  │
│  │ │ (delta/prefix compressed)           │   │  │
│  │ └──────────────────────────────────────┘   │  │
│  ├────────────────────────────────────────────┤  │
│  │ Data Block 1                               │  │
│  │ ...                                        │  │
│  ├────────────────────────────────────────────┤  │
│  │ Data Block N                               │  │
│  └────────────────────────────────────────────┘  │
│                                                  │
│  Meta Blocks:                                    │
│  ┌────────────────────────────────────────────┐  │
│  │ Filter Block (Bloom Filter)                │  │
│  │ - One Bloom filter per data block (or file) │  │
│  │ - Enables fast negative lookups             │  │
│  ├────────────────────────────────────────────┤  │
│  │ Stats Block                                │  │
│  │ - Number of entries, data size, etc.       │  │
│  ├────────────────────────────────────────────┤  │
│  │ Compression Dictionary (optional)          │  │
│  └────────────────────────────────────────────┘  │
│                                                  │
│  Index Block:                                    │
│  ┌────────────────────────────────────────────┐  │
│  │ Block 0: last_key=user:102 → offset 0     │  │
│  │ Block 1: last_key=user:250 → offset 4096  │  │
│  │ Block 2: last_key=user:400 → offset 8192  │  │
│  │ ...                                        │  │
│  └────────────────────────────────────────────┘  │
│                                                  │
│  Footer:                                         │
│  ┌────────────────────────────────────────────┐  │
│  │ Metaindex block handle                     │  │
│  │ Index block handle                         │  │
│  │ Magic number                               │  │
│  └────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────┘
```

**Key design properties of SST files:**
- **Immutable**: Once written, never modified. This simplifies concurrency (no locks needed for reads) and enables efficient caching.
- **Sorted**: Keys are in sorted order within each file, enabling binary search and efficient merges.
- **Block-based**: Data is divided into blocks for efficient I/O and caching (read one block at a time).
- **Prefix compression**: Adjacent keys with common prefixes share storage, reducing file size.

### 3.4 Level Structure and Compaction

#### Level Organization

```
Level 0 (L0): Special
  - SST files flushed directly from MemTable
  - Key ranges CAN OVERLAP between files
  - A read for key K must check ALL L0 files
  - Max files: level0_file_num_compaction_trigger (default: 4)

Level 1 (L1): First sorted level
  - Non-overlapping key ranges
  - Total size: max_bytes_for_level_base (default: 256MB)

Level 2 (L2) through Level N:
  - Non-overlapping key ranges
  - Each level is max_bytes_for_level_multiplier (default: 10x) larger
  
  L0: 64MB × 4 files = 256MB
  L1: 256MB
  L2: 2.56GB
  L3: 25.6GB
  L4: 256GB
  L5: 2.56TB
  L6: 25.6TB
```

#### Compaction Strategies

Compaction is the process of **merging SST files** from one level into the next, removing duplicates and tombstones. It is the most important and most expensive operation in an LSM-tree.

**Leveled Compaction (default):**

```
Trigger: L0 has too many files, or Ln exceeds size limit

Leveled Compaction from L0 to L1:

L0:  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐
     │a-m   │ │c-z   │ │b-p   │ │a-f   │   (overlapping ranges)
     └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘
        │        │        │        │
        └────────┴────────┴────────┘
                    │
              MERGE + SORT
                    │
                    ▼
L1:  ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐
     │a-d │ │e-h │ │i-l │ │m-p │ │q-t │ │u-z │  (non-overlapping)
     └────┘ └────┘ └────┘ └────┘ └────┘ └────┘

Leveled Compaction from L1 to L2:
  Pick one L1 file → find overlapping L2 files → merge → write new L2 files

L1:  ┌────┐ ┌────┐ ┌────┐
     │a-d │ │e-h │ │i-l │  ← pick "e-h"
     └────┘ └─┬──┘ └────┘
              │
L2:  ┌──┐ ┌──┤──┐ ┌──┐ ┌──┐     ← find overlapping: "d-f" and "f-j"
     │a-c│ │d-f│f-j│ │k-m│ │n-p│
     └──┘ └──┴──┘ └──┘ └──┘
              │
         MERGE (e-h) + (d-f) + (f-j)
              │
              ▼
L2:  ┌──┐ ┌──┐ ┌──┐ ┌──┐ ┌──┐ ┌──┐
     │a-c│ │d-e│ │f-g│ │h-j│ │k-m│ │n-p│   (new non-overlapping L2 files)
     └──┘ └──┘ └──┘ └──┘ └──┘ └──┘
```

**Properties of Leveled Compaction:**
- **Write amplification**: ~10-30x (each key may be rewritten multiple times as it moves through levels)
- **Read amplification**: Low (at most one file per level to check)
- **Space amplification**: Low (~10-20% overhead)

**Size-Tiered Compaction (Universal):**

```
Instead of levels, group files of similar size together:

Tier 1 (small):   ┌──┐ ┌──┐ ┌──┐ ┌──┐
                   └──┘ └──┘ └──┘ └──┘  → merge when enough same-size files
                             │
                             ▼
Tier 2 (medium):  ┌────────┐ ┌────────┐
                  └────────┘ └────────┘  → merge when enough same-size files
                             │
                             ▼
Tier 3 (large):   ┌──────────────────┐
                  └──────────────────┘
```

**Properties of Size-Tiered Compaction:**
- **Write amplification**: Lower (~4-10x)
- **Read amplification**: Higher (more files to check)
- **Space amplification**: Higher (up to 2x — needs space for old + new files during compaction)

**FIFO Compaction:**
- Simply drops the oldest SST files when total size exceeds a limit
- For time-series data where old data can be discarded
- Minimal write amplification but data is lost

### 3.5 Read Path

Reading from an LSM-tree is more complex than B-tree reads because data may exist in multiple locations:

```
Get(key="user:150"):

Step 1: Check Active MemTable
        ┌────────────────┐
        │  MemTable      │──── Found? → Return value
        └────────┬───────┘
                 │ Not found
Step 2: Check Immutable MemTable(s)
        ┌────────────────┐
        │  Immutable MT  │──── Found? → Return value
        └────────┬───────┘
                 │ Not found
Step 3: Check L0 SST files (ALL of them, newest first)
        ┌────────────────┐
        │  L0 SST files  │
        │  ┌───┐ ┌───┐   │
        │  │   │ │   │   │──── For each file:
        │  └───┘ └───┘   │     1. Check Bloom filter → definitely not here? Skip
        │  ┌───┐ ┌───┐   │     2. Binary search index block
        │  │   │ │   │   │     3. Read data block
        │  └───┘ └───┘   │     4. Found? → Return value
        └────────┬───────┘
                 │ Not found
Step 4: Check L1 SST files (binary search to find the RIGHT file)
        ┌────────────────┐
        │  L1:           │     Only ONE file can contain this key
        │  ┌─┐┌─┐┌─┐┌─┐ │     (non-overlapping ranges)
        │  │ ││ ││ ││ │ │──── 1. Binary search file boundaries
        │  └─┘└─┘└─┘└─┘ │     2. Check Bloom filter
        └────────┬───────┘     3. Binary search within file
                 │ Not found
Step 5: Check L2, L3, ... LN (same as L1)
        ...
                 │ Not found in any level
                 ▼
        Return: KEY NOT FOUND
```

**Read Amplification Problem**: In the worst case (key doesn't exist), a read must check:
- 1 active MemTable
- N immutable MemTables
- All L0 files
- 1 file per level (L1 through LN)

This is why reads in LSM-trees are inherently slower than B-tree reads, where a single tree traversal suffices.

### 3.6 Bloom Filters

Bloom filters are **critical** for LSM-tree read performance. They answer the question: "Is this key **definitely NOT** in this SST file?"

```
Bloom Filter: A space-efficient probabilistic data structure

Building (during SST file creation):
    For each key in the SST file:
        Hash the key with k hash functions: h1(key), h2(key), ..., hk(key)
        Set bits at positions h1, h2, ..., hk in a bit array

    Bit Array (m bits):
    [0][1][0][0][1][0][1][0][0][1][0][0][1][0][0][1]

Querying:
    Hash the lookup key with same k functions
    Check all k bit positions:
        ALL bits are 1 → Key MIGHT exist (possible false positive)
        ANY bit is 0   → Key DEFINITELY does NOT exist

    False Positive Rate: ≈ (1 - e^(-kn/m))^k
    where n = number of keys, m = bits, k = hash functions

    With 10 bits per key: ~1% false positive rate
    With 15 bits per key: ~0.1% false positive rate
```

**Impact on read performance:**

```
Without Bloom filters:
    Point lookup for non-existent key:
    → Must read index block + data block from EVERY SST file
    → If 100 SST files: 100+ disk reads

With Bloom filters (10 bits/key, ~1% FP rate):
    Point lookup for non-existent key:
    → Check Bloom filter for each SST file (in-memory, fast)
    → 99% of files eliminated immediately
    → ~1 disk read instead of 100
    → 100x improvement!
```

### 3.7 Write Path (Complete)

```
Put(key, value) — Complete Write Path:

    Client calls db->Put(key, value)
              │
              ▼
    ┌─────────────────────┐
    │ 1. Acquire write    │    (WriteBatch groups multiple ops)
    │    mutex / join     │
    │    write group      │
    └─────────┬───────────┘
              │
    ┌─────────▼───────────┐
    │ 2. Write to WAL     │    Append record to WAL file
    │    (sequential I/O)  │    Optional: fsync for durability
    └─────────┬───────────┘
              │
    ┌─────────▼───────────┐
    │ 3. Write to MemTable│    Insert into SkipList
    │    (in-memory)       │    O(log n) operation
    └─────────┬───────────┘
              │
    ┌─────────▼───────────┐
    │ 4. Return success   │    Write is complete from client's perspective
    └─────────────────────┘

    Background (asynchronous):
    
    ┌─────────────────────────────────────────┐
    │ 5. MemTable full → becomes immutable    │
    │    New MemTable allocated                │
    │                                          │
    │ 6. Flush immutable MemTable to L0 SST   │
    │    - Sort entries (already sorted)        │
    │    - Build data blocks with compression   │
    │    - Build Bloom filters                  │
    │    - Build index blocks                   │
    │    - Write SST file                       │
    │    - Delete corresponding WAL             │
    │                                          │
    │ 7. Compaction (triggered by L0 count     │
    │    or level size thresholds)              │
    │    - Merge SST files across levels        │
    │    - Remove duplicates & tombstones       │
    │    - Write new SST files                  │
    │    - Delete old SST files                 │
    └─────────────────────────────────────────┘
```

---

## 4. Design Trade-Offs

### The Three Amplification Factors

LSM-tree design involves balancing three types of amplification. Improving one typically worsens another:

```
                    Write                Read               Space
                    Amplification        Amplification       Amplification
                    ─────────────        ─────────────       ─────────────
Definition:         Total bytes          Number of           Ratio of actual
                    written to disk      disk reads          space used to
                    ÷ user data bytes    per logical read    logical data size

Leveled             HIGH (10-30x)        LOW (1-2 reads     LOW (10-20%)
Compaction:         Each key rewritten   per level)
                    per level transition

Size-Tiered         LOW (4-10x)          HIGH (many files   HIGH (up to 2x)
Compaction:         Fewer merge passes   may overlap)

FIFO                MINIMAL (1x)         HIGH               MINIMAL
Compaction:         No rewriting         Time-based only

B-Tree              MEDIUM (10-30x       LOW (1 tree        LOW (25-50%
(comparison):       due to page writes)  traversal)          internal frag.)
```

### LSM-Tree vs. B-Tree: Complete Comparison

| Dimension | LSM-Tree (RocksDB) | B-Tree (PostgreSQL/InnoDB) |
|---|---|---|
| **Write pattern** | Sequential (append to WAL + MemTable flush) | Random (in-place page updates) |
| **Write throughput** | **Higher** (10-100x for sustained writes) | Lower (limited by random I/O) |
| **Point read latency** | Higher (check multiple levels) | **Lower** (single tree traversal) |
| **Range scan** | Moderate (merge from multiple levels) | **Efficient** (sequential leaf pages) |
| **Space efficiency** | Better (compression, no internal fragmentation) | Worse (page fragmentation, MVCC overhead) |
| **Predictability** | Lower (compaction can cause latency spikes) | **Higher** (more consistent latency) |
| **CPU usage** | Higher (compression + compaction) | Lower |
| **SSD friendliness** | **Better** (sequential writes, less write amplification at flash level) | Worse (random writes, more flash wear) |

### Why Compaction Can Become Expensive

Compaction is the **Achilles' heel** of LSM-trees:

```
Problem: Compaction I/O competes with foreground operations

    Foreground:  READ → SSD ← READ → SSD ← READ → SSD
    Background:  ████████ COMPACTION (reading + writing GBs) ████████
                 
    During heavy compaction:
    - Foreground reads slow down (I/O bandwidth shared)
    - Write stalls possible if L0 backs up
    - CPU consumed by compression/decompression
    - Temporary space amplification (old + new files exist simultaneously)

Mitigation strategies:
    1. Rate limiting: max_compaction_bytes limits I/O rate
    2. Subcompaction: Parallelize within a single compaction job
    3. Prioritization: L0→L1 compaction gets highest priority (prevent write stalls)
    4. Dynamic leveling: Adjust level sizes based on workload
```

### Write Stall: The Critical Backpressure Scenario

```
When writes are faster than compaction:

    Writes ──────────────────────────────────────►
    MemTable │ Full → Immutable │ Full → Immutable │ Full → ??? 
                                                          │
    Flush ───────────────────────────►                    │
    L0 accumulates:  [SST][SST][SST][SST][SST]           │
                     └── Too many L0 files! ──────────────┘
                                                          │
    Compaction can't keep up ──────────────►               │
                                                          ▼
                                                    WRITE STALL!
                                                    
    RocksDB response:
    1. Slow down writes (delayed writes)
    2. If still backing up: STOP writes entirely
    3. Log: "Stalling writes because..." 
    
    Key thresholds:
    - level0_slowdown_writes_trigger (default: 20 files)
    - level0_stop_writes_trigger (default: 36 files)
```

---

## 5. Experiments / Observations

### Experiment 1: Measuring the Three Amplifications

Using RocksDB's built-in benchmark tool (`db_bench`):

```bash
# Write-heavy benchmark (sequential keys)
./db_bench \
    --benchmarks="fillseq,stats" \
    --num=10000000 \
    --value_size=100 \
    --compression_type=none \
    --statistics=true

# Write-heavy benchmark (random keys)  
./db_bench \
    --benchmarks="fillrandom,stats" \
    --num=10000000 \
    --value_size=100 \
    --compression_type=none \
    --statistics=true

# Read benchmark (point lookups)
./db_bench \
    --benchmarks="readrandom" \
    --num=10000000 \
    --reads=1000000 \
    --statistics=true
```

**Expected observations from statistics:**

```
Key metrics to examine:

1. Write Amplification:
   rocksdb.compact.write.bytes / rocksdb.bytes.written
   
   Leveled compaction: typically 10-30x
   Universal compaction: typically 4-10x

2. Read Amplification:
   rocksdb.read.amp.estimate.useful.bytes / rocksdb.read.amp.total.bytes
   
   Also: rocksdb.bloom.filter.useful (how many reads were saved by Bloom filters)
   
3. Space Amplification:
   Total SST file size / logical data size
   
   Leveled: ~1.1x (10% overhead)
   Universal: up to 2x during compaction
```

### Experiment 2: Compaction Strategy Comparison

```bash
# Leveled Compaction (default)
./db_bench \
    --benchmarks="fillrandom,readrandom,stats" \
    --num=5000000 \
    --compaction_style=0 \      # 0 = Leveled
    --statistics=true

# Universal (Size-Tiered) Compaction
./db_bench \
    --benchmarks="fillrandom,readrandom,stats" \
    --num=5000000 \
    --compaction_style=1 \      # 1 = Universal
    --statistics=true

# FIFO Compaction
./db_bench \
    --benchmarks="fillrandom,readrandom,stats" \
    --num=5000000 \
    --compaction_style=2 \      # 2 = FIFO
    --compaction_options_fifo="{max_table_files_size=1073741824}" \
    --statistics=true
```

**Expected comparison results:**

| Metric | Leveled | Universal | FIFO |
|---|---|---|---|
| Write throughput | Moderate | **Highest** | **Highest** |
| Read latency (p99) | **Lowest** | Higher | Highest |
| Space usage | **Most efficient** | 2x peaks | Drops data |
| Write amplification | ~15-25x | ~5-10x | ~1x |
| Compaction CPU | High | Medium | Minimal |

### Experiment 3: Bloom Filter Effectiveness

```bash
# Without Bloom filters
./db_bench \
    --benchmarks="fillrandom,readrandom" \
    --num=10000000 \
    --reads=100000 \
    --bloom_bits=0 \
    --statistics=true

# With 10 bits/key Bloom filters (~1% FP rate)
./db_bench \
    --benchmarks="fillrandom,readrandom" \
    --num=10000000 \
    --reads=100000 \
    --bloom_bits=10 \
    --statistics=true
```

**Expected results:**
- Without Bloom filters: ~3-5x more SST file reads per lookup
- With 10 bits/key: ~99% of non-matching SST files skipped
- Space overhead: ~10 bits per key (~1.25 bytes), minimal compared to value sizes
- Read latency improvement: 2-5x for point lookups (especially for missing keys)

### Experiment 4: Monitoring Write Stalls

```bash
# Stress test to trigger write stalls
./db_bench \
    --benchmarks="fillrandom" \
    --num=50000000 \
    --value_size=1000 \
    --level0_slowdown_writes_trigger=10 \
    --level0_stop_writes_trigger=15 \
    --max_background_compactions=1 \    # Intentionally limit compaction
    --statistics=true

# Monitor in real-time:
# Watch for these statistics:
# rocksdb.stall.micros — total time spent stalled
# rocksdb.l0.slowdown.micros
# rocksdb.l0.num.files.stall.micros
```

---

## 6. Key Learnings

### 1. LSM-Trees Trade Read Efficiency for Write Efficiency
The fundamental insight of LSM-trees is that **converting random writes to sequential writes** is worth the cost of more complex reads. This trade-off makes sense for write-heavy workloads (logging, time-series, social feeds) but not for read-dominant OLTP workloads. Understanding this trade-off is essential for choosing the right storage engine.

### 2. Compaction Is the Heart (and the Bottleneck)
Compaction determines every performance characteristic of an LSM-tree system. The choice between leveled and tiered compaction is effectively a choice between read performance (leveled) and write performance (tiered). There is no free lunch — every compaction strategy accepts a different combination of amplification factors.

### 3. Bloom Filters Are Not Optional
Without Bloom filters, LSM-tree read performance degrades dramatically because every SST file must be checked. Bloom filters convert this from O(number of files) disk reads to approximately O(1) at the cost of ~10 bits per key in memory. This is one of the highest-ROI data structures in all of computer science.

### 4. Write Stalls Are the Operational Challenge
The most common production issue with LSM-tree systems is **write stalls** — when the MemTable fills up and compaction can't keep up. Understanding the backpressure mechanisms (`level0_slowdown_writes_trigger`, `level0_stop_writes_trigger`) and monitoring compaction progress is essential for running RocksDB in production.

### 5. SST Immutability Enables Powerful Features
The immutable nature of SST files enables several features that would be complex with mutable storage:
- **Consistent snapshots**: Just record which SST files exist at a point in time
- **Backup**: Copy SST files (they'll never change)
- **Cache-friendly**: SST file blocks can be cached indefinitely without invalidation
- **Concurrent reads**: No locks needed to read immutable files

This design philosophy — **immutable storage with periodic reorganization** — is a powerful pattern that appears in many modern systems (data lakes, log-structured file systems, event sourcing).

### 6. The Amplification Triangle Is Universal
The write/read/space amplification trade-off isn't specific to RocksDB — it's a fundamental constraint in storage system design. B-trees accept write amplification (page rewrites) for low read amplification. LSM-trees accept read and space amplification for low write amplification. Understanding this triangle helps in evaluating any storage system.

---

## References

1. RocksDB Documentation: https://rocksdb.org/docs/
2. RocksDB GitHub: https://github.com/facebook/rocksdb
3. RocksDB Wiki — Architecture Guide: https://github.com/facebook/rocksdb/wiki
4. Patrick O'Neil et al., "The Log-Structured Merge-Tree (LSM-Tree)" — Acta Informatica, 1996
5. Luo, C. and Carey, M.J., "LSM-based Storage Techniques: A Survey" — VLDB Journal, 2020
6. Mark Callaghan, "The Three Amplification Factors" — Percona Live Talk
7. Siying Dong et al., "Optimizing Space Amplification in RocksDB" — CIDR 2017
8. RocksDB Tuning Guide: https://github.com/facebook/rocksdb/wiki/RocksDB-Tuning-Guide
9. RocksDB Performance Benchmarks: https://github.com/facebook/rocksdb/wiki/Performance-Benchmarks
