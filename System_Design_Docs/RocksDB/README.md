# RocksDB Architecture: LSM-Tree Based Storage

> **Author:** Pranay | Roll No: 24BCS10133  
> **Course:** Advanced DBMS — System Design Discussion

---

## 1. Problem Background

RocksDB was created at Facebook in 2012, forked from Google's LevelDB. The origin story tells you everything about its design goals: Facebook needed a storage engine for flash (SSD) storage that could handle hundreds of millions of writes per day at very high throughput. Their existing solutions (MySQL/InnoDB, LevelDB) either weren't fast enough or didn't scale to their multi-terabyte datasets.

The fundamental insight: **B-Tree storage engines are optimized for spinning disk I/O patterns (seek → sequential read). SSDs have completely different characteristics: random reads are fast, but random writes cause write amplification and wear the SSD prematurely.** LSM-trees convert random writes into sequential writes — the optimal I/O pattern for both SSDs and HDDs.

RocksDB powers: Facebook's social graph, MySQL at Uber (MyRocks), TiKV (distributed KV store), CockroachDB (storage layer), Apache Kafka (log storage), and many more. It's perhaps the most influential storage engine of the 2010s.

**The core trade-off RocksDB makes:** Optimize writes at the cost of read complexity. Accept a "later cleanup" cost (compaction) in exchange for extremely fast, sequential writes now.

---

## 2. Architecture Overview

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                         RocksDB Architecture                                 │
│                                                                              │
│  WRITE PATH:                                                                 │
│                                                                              │
│  Client Write                                                                │
│      │                                                                       │
│      ├──► WAL (Write-Ahead Log)  ← always written first for durability      │
│      │                                                                       │
│      └──► MemTable (in-memory, sorted skiplist)                              │
│                 │                                                             │
│                 │ when full (default 64MB)                                   │
│                 ▼                                                             │
│           Immutable MemTable (still in memory, being flushed)               │
│                 │                                                             │
│                 │ background flush thread                                    │
│                 ▼                                                             │
│  ┌──────────────────────────────────────────────────────────────┐           │
│  │                    SST Files on Disk                         │           │
│  │                                                              │           │
│  │  Level 0 (L0): 4 SST files (max, then trigger compaction)   │           │
│  │  [SST_a: a-m] [SST_b: e-z] [SST_c: b-k] [SST_d: c-p]      │           │
│  │   ↑ Note: L0 files can have overlapping key ranges!         │           │
│  │                                                              │           │
│  │  Level 1 (L1): 10MB budget, NO key overlap                  │           │
│  │  [SST_1: a-c] [SST_2: d-f] [SST_3: g-j] [SST_4: k-z]     │           │
│  │                                                              │           │
│  │  Level 2 (L2): 100MB budget, NO key overlap                 │           │
│  │  [SST_1: a-b][SST_2: c-d][...many more...][SST_N: y-z]    │           │
│  │                                                              │           │
│  │  Level 3+: 10x larger each level                            │           │
│  │  ...                                                         │           │
│  └──────────────────────────────────────────────────────────────┘           │
│                                                                              │
│  READ PATH:                                                                  │
│  MemTable → Immutable MemTable → L0 (all files) → L1 → L2 → ... → Ln      │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Internal Design

### 3.1 MemTable — The Write Entry Point

Every write goes first to the MemTable, which is an **in-memory sorted data structure** (by default a skiplist).

```
MemTable (SkipList):
Key range: ["alice", "bob", "carol", "dave", ...]

SkipList structure:
Level 3: ─────── "carol" ──────────────────────────────── END
Level 2: ─────── "carol" ─────────── "dave" ──────────── END
Level 1: ─────── "carol" ─────────── "dave" ─── "eve" ── END
Level 0: "alice" "bob" "carol" "dave" "eve" "frank" ...
```

**Why a skiplist?** Concurrent writes need concurrent access. A skiplist supports lock-free or fine-grained concurrent inserts much more easily than a B-Tree (which requires rebalancing). Hash maps are fast for point lookups but don't support range scans. SkipList gives O(log n) insert, O(log n) lookup, AND O(k) range scan — the perfect fit for an ordered write buffer.

**MemTable internals per entry:**

```
Each KV entry:
┌──────────────────────────────────────────────────────┐
│ Internal Key:                                        │
│  [user_key | sequence_number (56 bits) | type (8 bits)] │
│                                                      │
│ type = Put (0x1), Delete (0x0), Merge (0x2)         │
│                                                      │
│ Value:                                               │
│  [value data] (or empty for Delete)                 │
└──────────────────────────────────────────────────────┘
```

**Sequence numbers** are the key to MVCC in RocksDB. Every write gets a monotonically increasing sequence number. A read with snapshot sequence number `S` sees only entries with sequence ≤ S. Deletes are represented as **tombstones** — a Delete entry with no value. The actual key-value isn't removed until compaction.

### 3.2 WAL (Write-Ahead Log)

Before writing to MemTable, RocksDB writes to the WAL. This ensures that even if the process crashes before MemTable is flushed to disk, the writes can be recovered.

```
WAL File Format:
┌──────────────────────────────────────────────────────┐
│ Series of log records (variable length):             │
│                                                      │
│ Record:                                              │
│  [checksum (4B)][length (2B)][type (1B)][data]      │
│                                                      │
│ type = FULL (fits in one block)                      │
│      = FIRST/MIDDLE/LAST (spans multiple blocks)    │
│                                                      │
│ data = WriteBatch:                                   │
│  [sequence_number][count][type][key_len][key]        │
│                              [value_len][value]...   │
└──────────────────────────────────────────────────────┘
```

**When is WAL truncated?** Once a MemTable is fully flushed to an SST file, its corresponding WAL segment is no longer needed for recovery and can be deleted. This is why WAL files in RocksDB are per-MemTable — a design that makes WAL lifecycle management straightforward.

### 3.3 SSTable (Sorted String Table) Files

When a MemTable fills up (default 64MB), it becomes **immutable** and a background thread flushes it to disk as an **SST file** — the immutable, sorted, on-disk data structure.

```
SST File Internal Structure:
┌──────────────────────────────────────────────────────────────────────┐
│ Data Blocks (sorted KV pairs, compressed)                            │
│  Block 1: [k1:v1][k2:v2]...[kN:vN]  (default 4KB per block)        │
│  Block 2: [kN+1:vN+1]...[kM:vM]                                     │
│  ...                                                                 │
├──────────────────────────────────────────────────────────────────────┤
│ Index Block                                                          │
│  One entry per data block: [last_key_in_block | block_offset | size] │
│  Used to find which data block might contain a key                   │
├──────────────────────────────────────────────────────────────────────┤
│ Filter Block (Bloom Filter)                                          │
│  One filter per data block (or per file, configurable)              │
│  Used to quickly reject "key not in this file" queries              │
├──────────────────────────────────────────────────────────────────────┤
│ Meta Index Block                                                     │
│  Points to filter block, properties block, etc.                     │
├──────────────────────────────────────────────────────────────────────┤
│ Footer (fixed 48 bytes)                                              │
│  - meta_index_handle: offset+size of meta index block              │
│  - index_handle: offset+size of index block                        │
│  - magic number: 0xdb4775248b80fb57                                 │
└──────────────────────────────────────────────────────────────────────┘
```

**SST files are immutable** — once written, they are never modified. This is fundamental to the LSM design:
- No need for page-level locking during reads
- Compaction reads old SST files and produces new ones (deletes old ones atomically)
- Crash safety: a partially-written SST file is simply invalid and ignored on recovery (WAL provides the data)

### 3.4 Bloom Filters — Making Reads Tolerable

The biggest weakness of LSM-trees is read performance: a point lookup must check every level until the key is found. Bloom filters are the mitigation.

```
Bloom Filter (probabilistic):
- For each SST file: maintain a bit array of m bits
- On insert: hash key K with k independent hash functions
  → set bits at positions h1(K), h2(K), ..., hk(K)
- On lookup: hash key K → check positions h1(K), ..., hk(K)
  → ALL set? → key MIGHT be in this file (false positive possible)
  → ANY not set? → key DEFINITELY NOT in this file (no false negatives)

False positive rate: (1 - e^(-kn/m))^k
Default in RocksDB: ~1% false positive rate
```

```
Read path with Bloom filter:
1. Check MemTable → not found
2. Check immutable MemTables → not found
3. For each L0 SST file (all must be checked, overlapping ranges):
   a. Check Bloom filter → negative? SKIP (save I/O!)
                         → positive? Read index block → maybe read data block
4. For L1 and below (non-overlapping): binary search to find target file
   a. Check Bloom filter → negative? SKIP → move to L2
                         → positive? Potentially read data block
```

**Impact:** Without Bloom filters, a read for a nonexistent key requires checking every SST file at every level — catastrophic for read-heavy workloads. With Bloom filters at 1% false positive rate, 99% of non-existent key lookups are short-circuited with a cheap in-memory check.

**The memory cost:** Bloom filters typically use ~10 bits per key. For 1 billion keys, that's ~1.25 GB of memory just for Bloom filters. RocksDB stores them in the SST file footer and caches them in the block cache — they don't all have to be in memory simultaneously.

### 3.5 Compaction — The Necessary Cost

Compaction is how RocksDB reclaims space, removes tombstones, and maintains the invariant that levels L1+ have non-overlapping key ranges.

```
Compaction Example (L1 + L2):

Input from L1:  [SST_A: keys 100-200]
Input from L2:  [SST_X: keys 80-150] [SST_Y: keys 151-250]

Compaction reads all three files, merges (sorted merge), produces:
New L2 files:   [SST_new1: keys 80-150] [SST_new2: keys 151-250]
(with duplicates/tombstones removed, latest version wins)

Old files (SST_A, SST_X, SST_Y) are atomically deleted.
```

**Compaction strategies:**

```
1. Leveled Compaction (default, Google's LevelDB style):
   - L1..Ln have strict size budgets (10x each level)
   - When L(n) exceeds budget: pick an SST file, compact with overlapping L(n+1) files
   - Result: minimal read amplification (1 file per level for a key, except L0)
   - Cost: higher write amplification (data rewritten multiple times as it moves down levels)
   
2. Tiered/Size-Tiered Compaction (STCS, Cassandra style):
   - SST files accumulated in tiers by size
   - When enough same-size files exist: merge them into a larger file
   - Result: lower write amplification (fewer rewrites)
   - Cost: higher read amplification (many large files to search) and space amplification
   
3. FIFO Compaction:
   - Just delete oldest SST files when total size exceeds budget
   - No merging
   - Only for data that expires (time-series logs, cache data)
   - Extremely fast writes, basically no compaction cost

4. Universal Compaction:
   - Hybrid: accumulate sorted runs, compact when size ratio exceeds threshold
   - Better write amplification than leveled, better read than tiered
   - Used by Facebook internally for some workloads
```

### 3.6 The Three Amplification Factors

RocksDB configuration is fundamentally about managing three competing amplification factors:

```
Write Amplification (WA):
  = total bytes written to disk / bytes written by user
  
  Every byte you write → may be rewritten during compaction at each level
  Leveled: WA ≈ level_count × level_size_ratio ≈ 10-30x
  Example: 1 GB of user data → 10-30 GB of actual disk writes
  
  Lower WA = good for SSD longevity, write throughput
  
Read Amplification (RA):
  = number of I/Os per user read
  
  Without Bloom filters: RA = number of SST files checked
  With Bloom filters: RA ≈ 1-2 for existing keys, ~0 for non-existent
  Leveled: RA ≈ level_count (each level has at most 1 file for a key except L0)
  
  Lower RA = faster reads
  
Space Amplification (SA):
  = total disk space used / size of unique live data
  
  Dead data (tombstones, old versions) takes space until compaction removes it
  Leveled: SA ≈ 1.1 (10% overhead typical)
  Tiered: SA ≈ 2.0+ (old and new tiers coexist during compaction)
  
  Lower SA = cheaper storage
```

**The triangle:** You can optimize any two at the cost of the third. There is no configuration that minimizes all three simultaneously. This is the fundamental engineering trade-off in LSM-tree design.

```
┌─────────────────────────────────────────────────────┐
│              The RocksDB Amplification Triangle     │
│                                                     │
│           Write Amplification                       │
│                  △                                  │
│                 /|\                                 │
│  Leveled (good │ │ good)                            │
│  compaction   / │ \                                 │
│              /  │  \                                │
│             /   │   \                               │
│            ▼   │    ▼                              │
│  Read ─────┘   │    └──── Space                    │
│  Amplification │        Amplification              │
│            Tiered (bad read, bad space)            │
│            FIFO (terrible read, OK space)          │
│                                                     │
│  No configuration wins on all three corners.       │
└─────────────────────────────────────────────────────┘
```

---

## 4. Design Trade-Offs

### LSM-Tree vs B-Tree (Core Trade-Off)

| Property | LSM-Tree (RocksDB) | B-Tree (PostgreSQL/InnoDB) |
|---|---|---|
| Write pattern | Sequential (optimal for SSDs) | Random (seeks to right page) |
| Write throughput | 10-100x higher for small writes | Limited by random I/O latency |
| Read latency (latest) | Higher (check multiple levels) | Lower (single B-Tree traversal) |
| Read latency (non-existent key) | Very low with Bloom filters | Very low (B-Tree says "not here") |
| Space efficiency | Lower (tombstones, old versions until compaction) | Higher (in-place updates) |
| Background I/O | Compaction (unpredictable bursts) | Checkpoint (more predictable) |
| Crash recovery | WAL replay (fast, bounded by WAL size) | WAL replay from last checkpoint |

### Compaction Trade-Offs

| Strategy | Best For | Worst For |
|---|---|---|
| Leveled | Read-heavy, storage-sensitive | Write-heavy (high WA) |
| Tiered/Universal | Write-heavy | Read-heavy (high RA), storage |
| FIFO | Time-series with TTL | Any workload needing old data |

### Bloom Filter Trade-Offs

| False Positive Rate | Bits per Key | Memory for 1B keys | Benefit |
|---|---|---|---|
| 10% | ~4.8 | 600 MB | Low memory, misses ~10% |
| 1% | ~9.6 | 1.2 GB | Good balance |
| 0.1% | ~14.4 | 1.8 GB | Near-perfect, higher memory |

---

## 5. Experiments / Observations

### Experiment 1: Write Amplification Under Leveled Compaction

```bash
# Using RocksDB's built-in db_bench tool
./db_bench \
  --benchmarks="fillrandom,stats" \
  --num=10000000 \
  --key_size=16 \
  --value_size=100 \
  --compression_type=lz4 \
  --compaction_style=0 \   # 0 = leveled
  --db=/tmp/rocksdb_test

# After benchmark, check stats:
./db_bench --benchmarks="stats" --db=/tmp/rocksdb_test

# Key output metrics to look for:
# Write Amplification = (Compaction bytes written) / (User bytes written)
# Typical result for leveled, 10M random writes: WA ≈ 15-25x
# Meaning: writing 1GB of data caused ~15-25GB of total disk writes
```

**Observed behavior:** Write amplification is highest when the database is near full and data spans many levels. When the database is small (fits in fewer levels), WA is lower. This is why RocksDB is often tuned with larger L1 size and more levels for large datasets.

```bash
# Tiered (Universal) compaction comparison:
./db_bench \
  --benchmarks="fillrandom,stats" \
  --num=10000000 \
  --compaction_style=1 \   # 1 = universal
  --db=/tmp/rocksdb_universal

# Typical result:
# Universal compaction WA: 5-10x (lower than leveled)
# But: read amplification 3-5x (higher than leveled)
# And: space amplification 1.5-2x (more temporary space during compaction)
```

### Experiment 2: Bloom Filter Impact on Read Performance

```python
import rocksdb

# Create two DBs: one with Bloom filters, one without
opts_with_bloom = rocksdb.Options()
opts_with_bloom.create_if_missing = True
table_factory = rocksdb.BlockBasedTableFactory(
    filter_policy=rocksdb.BloomFilterPolicy(10)  # 10 bits per key
)
opts_with_bloom.table_factory = table_factory

opts_no_bloom = rocksdb.Options()
opts_no_bloom.create_if_missing = True

db_bloom = rocksdb.DB("/tmp/bloom_db", opts_with_bloom)
db_no_bloom = rocksdb.DB("/tmp/no_bloom_db", opts_no_bloom)

# Insert 1M keys into both
for i in range(1_000_000):
    key = f"key_{i:010d}".encode()
    val = b"x" * 100
    db_bloom.put(key, val)
    db_no_bloom.put(key, val)

# Force flush to SST (so Bloom filters are on disk)
db_bloom.compact_range(None, None)
db_no_bloom.compact_range(None, None)

# Benchmark: 100k lookups for NON-EXISTENT keys (worst case for LSM)
import time

start = time.time()
for i in range(1_000_000, 1_100_000):  # keys not in DB
    db_bloom.get(f"key_{i:010d}".encode())
bloom_time = time.time() - start

start = time.time()
for i in range(1_000_000, 1_100_000):
    db_no_bloom.get(f"key_{i:010d}".encode())
no_bloom_time = time.time() - start

print(f"With Bloom: {bloom_time:.2f}s")
print(f"Without Bloom: {no_bloom_time:.2f}s")
# Typical observation:
# With Bloom:    0.4s  (Bloom filter says "not here" → no disk read)
# Without Bloom: 8.7s  (must check every SST file at every level)
# ~20x difference for non-existent key lookups
```

### Experiment 3: Compaction Stall Under Heavy Write Load

```bash
# Monitor RocksDB stats during heavy write load
./db_bench --benchmarks="fillrandom" --num=50000000 --threads=8 \
  --statistics --stats_interval_seconds=5 --db=/tmp/stress_test &

# Watch for these warning signs in the output:
# "Stall conditions met: L0 file count"
# "Compaction: stalling writes, too many L0 files"
# "Stall conditions met: pending compaction bytes"

# These indicate the write rate exceeded compaction rate
# Tuning responses:
# - Increase compaction threads: --max_background_compactions=8
# - Increase L0 file count trigger: --level0_slowdown_writes_trigger=20
# - Increase write buffer size: --write_buffer_size=256MB
# - Use compression to reduce data volume: --compression_type=zstd
```

**Observation:** Write stalls are RocksDB's backpressure mechanism. When L0 accumulates more than `level0_slowdown_writes_trigger` files, writes are artificially slowed. At `level0_stop_writes_trigger`, writes stop entirely until compaction catches up. This is the "compaction debt" that write-heavy workloads must manage.

### Experiment 4: Space Amplification Measurement

```bash
# After inserting 10GB of data and running many updates:
du -sh /tmp/rocksdb_test/
# Typical result: 15-22GB (1.5-2.2x space amplification)

# Force full compaction (removes all dead data)
./ldb --db=/tmp/rocksdb_test compact

du -sh /tmp/rocksdb_test/
# After compaction: ~11GB (1.1x space amplification — very efficient!)

# Key insight: space amplification is temporal
# It's high during heavy write periods, low after compaction settles
```

---

## 6. Key Learnings

**1. LSM-trees are an I/O pattern optimization, not just a data structure.**  
The key insight isn't "use a different index structure." It's: "convert random writes into sequential writes by buffering in memory and sorting on flush." This works because sequential I/O is 10-1000x faster than random I/O — even on SSDs, where sequential throughput vastly exceeds random throughput (GB/s vs hundreds of MB/s for random).

**2. Nothing is free — compaction is the deferred cost of cheap writes.**  
Every byte you write cheaply now will be rewritten during compaction. Write amplification is the price of the LSM-tree's write performance. This is not a bug — it's a deliberate trade. The question is whether your workload benefits from cheap writes enough to justify the compaction overhead.

**3. Bloom filters are what make LSM-trees viable for mixed workloads.**  
Without Bloom filters, every read of a non-existent key would touch every SST file — catastrophic. Bloom filters reduce "key not found" reads to a probabilistic O(1) in-memory check. Understanding the memory-accuracy trade-off of Bloom filter sizing is essential for tuning RocksDB.

**4. The three amplification factors form an unavoidable triangle.**  
Write, read, and space amplification cannot all be minimized simultaneously. Leveled compaction reduces read and space amplification at the cost of write amplification. Tiered reduces write amplification at the cost of read and space. Every RocksDB configuration is a choice about which corners of this triangle to optimize. Workload analysis must precede tuning.

**5. Compaction stalls are backpressure, not failures.**  
Write stalls are how RocksDB signals that the write rate exceeds the compaction rate. Understanding the stall conditions (L0 file count, pending compaction bytes) and their tuning knobs is essential for predictable write latency. Unchecked compaction debt leads to complete write stalls.

**6. Immutability simplifies everything else.**  
SST files are never modified after creation. This makes concurrent reads trivially safe (no locking), crash recovery simple (partially-written SST files are just invalid), and replication straightforward (ship the SST files). The B-tree approach of in-place modification requires careful locking, partial-write protection (FPW in PostgreSQL), and complex crash recovery. Immutability at the file level is a profound architectural simplification.

**7. RocksDB's influence is disproportionate to its age.**  
In 12 years, it became the storage layer for TiKV, CockroachDB, Kafka, Flink, and many others. It proved that LSM-trees at a production-quality implementation could outperform B-trees for write-heavy workloads. Understanding RocksDB is understanding modern distributed systems storage.

---

## References

- RocksDB wiki: https://github.com/facebook/rocksdb/wiki
- "Benchmarking LevelDB vs. RocksDB vs. HyperLevelDB vs. LMDB Performance for InfluxDB" — Paul Dix
- "RocksDB: Evolution of Development Priorities in a KV Store Serving Large-Scale Applications" — Facebook/Meta Engineering (2021)
- LSM-tree original paper: "The Log-Structured Merge-Tree" — Patrick O'Neil et al. (1996)
- "The Snowflake Elastic Data Warehouse" — for contrast (B-Tree at scale)
- Compaction strategies: https://github.com/facebook/rocksdb/wiki/Compaction
- Write stalls: https://github.com/facebook/rocksdb/wiki/Write-Stalls
