# RocksDB Architecture: LSM-Tree Based Storage

## 1. Problem Background

### The Problem B-Trees Can't Solve Well

B-trees (used by PostgreSQL, InnoDB, SQLite) are excellent for mixed read-write workloads with random access patterns. But they have a fundamental weakness: **random writes are slow on spinning disks and even SSDs.**

When you update a row in a B-tree database, the engine must:
1. Read the page containing that row into memory
2. Modify the row in memory
3. Write the modified page back to disk at the same physical location

This is a **read-modify-write** cycle. At high write volumes, the I/O pattern is random (each row update touches a different disk location). SSDs handle random writes better than HDDs, but they still generate **write amplification**: the SSD controller must erase an entire block (128KB–4MB) before writing a 4KB page, causing internal amplification of 10–100x.

By 2010, Facebook had a problem: their social graph data required hundreds of millions of writes per second. MySQL/InnoDB worked, but its B-tree storage model was hitting I/O throughput limits. They needed a storage engine optimized for **write-heavy, sequential-I/O workloads**.

The answer was to fork Google's LevelDB (2011) and build **RocksDB** (2012): a key-value store built on the **Log-Structured Merge Tree (LSM-tree)** data structure, originally proposed by O'Neil et al. in 1996.

The core insight of LSM-trees: **convert random writes into sequential writes by batching changes in memory and flushing them to disk in sorted order.** Sequential I/O is 10-100x faster than random I/O on both HDDs and SSDs.

RocksDB is now the storage engine inside: CockroachDB, TiKV, Cassandra (optionally), LinkedIn's distributed systems, and countless others.

---

## 2. Architecture Overview

### LSM-Tree Component Diagram

```
WRITE PATH                              READ PATH
──────────                              ──────────

[Client Write]                          [Client Read]
      │                                       │
      ▼                                       ▼
  WAL (Write-Ahead Log)               MemTable (check first)
  /wal/MANIFEST-000xxx                       │ miss
      │                                       ▼
      ▼                              Immutable MemTable(s)
  MemTable (in-memory,                       │ miss
  sorted skip list)                          ▼
      │ full (default 64MB)           L0 SSTables (check all)
      ▼                                       │ miss
  Immutable MemTable                          ▼
      │                              L1 SSTables (binary search)
      ▼ flush to disk                         │ miss
                                              ▼
  L0 SSTables ─────────┐            L2 SSTables (binary search)
                         │ compaction         │ miss
  L1 SSTables ─────────┤                     ▼
                         │             ... Ln SSTables
  L2 SSTables ─────────┤
                         │    [Bloom Filter consulted at each level
  ...                    │     before reading SSTable to skip miss]
  Ln SSTables ──────────┘
```

### Key Data Structures

| Structure | Location | Purpose |
|---|---|---|
| WAL | Disk (sequential write) | Crash recovery for MemTable |
| MemTable | Memory (skip list) | Absorbs writes, sorted |
| Immutable MemTable | Memory | Being flushed to L0 |
| SSTable (Sorted String Table) | Disk | Immutable sorted key-value files |
| Bloom Filter | Memory/disk | Probabilistic "key doesn't exist" check |
| MANIFEST | Disk | Version history of SSTable files |

---

## 3. Internal Design

### 3.1 Write Path: Memory-First, Sequential Disk

**Step 1: WAL append**
```
Incoming write: PUT key="user:123" value="Alice"

→ Appended to WAL (sequential write to disk)
   Record format: [sequence_number][type][key_len][key][val_len][value]
```

The WAL ensures that if the process crashes before the MemTable is flushed to disk, the write can be replayed on restart. This is the same durability guarantee as PostgreSQL's WAL, but for an LSM-tree.

**Step 2: MemTable insert**
```
→ Inserted into MemTable (in-memory skip list, sorted by key)

Skip List structure:
Level 4:  user:001 ─────────────────────────── user:999
Level 3:  user:001 ──────── user:500 ────────── user:999
Level 2:  user:001 ─ user:200 ─ user:500 ─ user:800 ─ user:999
Level 1:  user:001, user:050, user:100, user:150, user:200, ...
```

Skip lists give O(log N) insert and O(log N) lookup, but with better concurrency properties than B-trees (no page splits, no rebalancing). Multiple concurrent writers can insert into different levels simultaneously.

**Why is this fast?** The write is complete from the client's perspective after the WAL append + MemTable insert. No reading of existing data, no random I/O. The write is sequential (WAL) + in-memory (MemTable). This is why LSM-trees have dramatically higher write throughput than B-trees.

**Step 3: MemTable flush (L0)**
When the MemTable exceeds its size threshold (default 64MB), it becomes immutable and a new MemTable is created. A background thread flushes the immutable MemTable to disk as an **SSTable file at Level 0**.

```
SSTable (Sorted String Table) internal format:
┌─────────────────────────────────────────────────────┐
│ Data Blocks (4KB each):                             │
│   [key1, val1] [key2, val2] ... sorted order        │
│                                                     │
│ Index Block:                                        │
│   [last key of block 1 → block 1 offset]           │
│   [last key of block 2 → block 2 offset]           │
│   ...                                               │
│                                                     │
│ Filter Block (Bloom Filter per data block):         │
│   bit array for quick "key not in this block" check │
│                                                     │
│ Metaindex Block: offsets of filter + index blocks   │
│ Footer: magic number, metaindex offset              │
└─────────────────────────────────────────────────────┘
```

Each SSTable is **immutable after creation**. This is the key to LSM simplicity: no in-place updates, no lock contention on disk files.

### 3.2 Level Structure and Compaction

**The Problem:** L0 SSTables overlap in key ranges (each flush produces a new file covering arbitrary keys). With many L0 files, a read must check all of them. This degrades read performance.

**The Solution: Compaction** — merge and sort SSTables into higher levels.

```
Level Structure:
                    Max Size
L0:  [ F1 ] [ F2 ] [ F3 ] [ F4 ]    ~4 files (trigger compaction)
         ↓ compaction
L1:  [A-G] [H-M] [N-S] [T-Z]        ~256 MB (non-overlapping)
         ↓ compaction
L2:  [A-B][C-D]...[Y-Z]              ~2.56 GB (non-overlapping)
         ↓ compaction
L3:                                  ~25.6 GB
...
Ln:                                  10× previous level
```

**Compaction Process:**
```
Pick SSTable(s) from level N that overlap with SSTables in level N+1
Read them all into memory
Merge-sort (like merge sort's merge step):
  - For duplicate keys: keep only the most recent version
  - Drop keys marked as deleted (tombstones, once all lower levels are past)
Write output as new SSTable(s) in level N+1
Delete input SSTables from levels N and N+1
```

This is the LSM-tree's most expensive operation. Every key written will be compacted multiple times as it moves from L0 → L1 → L2 → Ln. This is **write amplification** — the ratio of bytes written to storage vs bytes written by the application.

**Write amplification = typically 10-30x for a well-tuned RocksDB instance.**

For comparison, a B-tree has write amplification of ~2-4x. LSM-trees trade write amplification for sequential I/O patterns, which is beneficial when I/O bandwidth is the bottleneck (write-heavy workloads on HDDs or high-queue-depth SSDs).

**Compaction Strategies:**

| Strategy | Description | Best For |
|---|---|---|
| Leveled | Files within a level don't overlap; each level 10× next | Read-heavy, space-efficient |
| Universal (Tiered) | All files at a level may overlap; merge when too many | Write-heavy, faster writes |
| FIFO | Oldest files evicted when total size exceeds limit | Time-series / cache use cases |

Leveled compaction (default) minimizes read amplification and space amplification at the cost of more write I/O during compaction. Universal compaction reduces write amplification at the cost of more files per read.

### 3.3 Read Path and Bloom Filters

A read in RocksDB must check multiple locations:
```
GET key="user:123"

1. Check MemTable (skip list lookup): O(log N)
2. Check Immutable MemTable(s): O(log N) each
3. Check L0 SSTables (all of them, may overlap): O(files × log N)
4. Check L1 SSTables (binary search by key range): O(log N)
5. Check L2 SSTables: O(log N)
...
```

In the worst case (key doesn't exist), this checks every level. This is **read amplification** — the ratio of actual I/O done vs the minimum needed.

**Bloom Filters eliminate most unnecessary disk reads.**

A Bloom filter is a probabilistic data structure that answers: "Is this key definitely NOT in this SSTable?" It never produces false negatives (if the key is there, it says "maybe") but has a tunable false positive rate (says "maybe" for keys that aren't there).

```
For each SSTable, RocksDB keeps a Bloom filter in memory:

Query: "Is user:456 in this SSTable?"
Bloom filter says "NO" → skip reading the SSTable entirely (0 disk I/O)
Bloom filter says "maybe" → read the SSTable's index, then block

With 1% false positive rate and Bloom filter in memory:
  99% of SSTable checks for non-existent keys → 0 disk I/O
  1% of SSTable checks → read the SSTable (disk I/O)
```

Bloom filters are why RocksDB's read performance is acceptable despite the multi-level architecture. Without them, a missing-key lookup would require reading dozens of SSTable index blocks across all levels.

**Space vs precision trade-off:** A Bloom filter with 1% false positive rate needs ~10 bits per key. For 100 million keys, that's ~125 MB — acceptable in memory. Reducing to 0.1% FPR doubles the memory requirement. This is a tunable parameter in RocksDB (`bloom_bits_per_key`).

### 3.4 Deletions: Tombstones

RocksDB (and all LSM-trees) cannot immediately delete data by removing it. SSTables are immutable. Instead, a **tombstone** record is written:

```
DELETE key="user:123"
→ Write to MemTable: {key="user:123", type=DELETION, seq=456}
→ Flush to SSTable as a tombstone marker
```

During reads, if a tombstone is found, the key is treated as deleted. During compaction, when a tombstone reaches the lowest level (Ln) and there are no older versions of the key below it, the tombstone and all older versions are dropped.

**The danger:** If compaction lags significantly, tombstones accumulate. Reads must traverse all tombstones for a key before reaching the current value (or confirming deletion). This is called **tombstone accumulation** and degrades read performance in delete-heavy workloads.

### 3.5 Amplification Trade-offs

The three amplification factors form a fundamental triangle — you can optimize for two, but the third suffers:

```
         Write Amplification
                /\
               /  \
              /    \
             /      \
            /  Trade \
           /   -off   \
          /  Triangle  \
Read ────────────────── Space
Amplification          Amplification
```

| Compaction | Write Amp | Read Amp | Space Amp |
|---|---|---|---|
| Leveled | High (10-30×) | Low (L levels) | Low (~1.1×) |
| Universal | Low (1-10×) | High (all files) | High (2×) |
| No compaction | 1× | O(all files) | Very high |

**Real-world implication:** A write-heavy application (e.g., logging, event streaming, metrics) should use Universal compaction to minimize write amplification. A read-heavy application (e.g., user profile lookups) should use Leveled compaction to minimize read amplification.

---

## 4. Design Trade-offs

### LSM vs B-Tree: When Each Wins

**LSM-trees win when:**
- Write throughput is the primary concern
- Workload is write-heavy (more writes than reads)
- Sequential I/O is cheap (HDD or high-throughput SSD)
- Compression is important (SSTable blocks compress well; B-tree pages don't)
- Space efficiency matters (no B-tree page fragmentation)

**B-trees win when:**
- Read latency is critical (single B-tree traversal vs multi-level SSTable check)
- Workload is read-heavy or mixed
- Random I/O is fast (NVMe SSD with low latency)
- Predictable latency is required (compaction causes latency spikes)

### Compaction's Hidden Cost: Latency Spikes

The most operationally tricky aspect of RocksDB is **compaction-induced latency spikes.** When a large compaction runs, it reads and writes gigabytes of data. This saturates I/O bandwidth, causing concurrent read and write latencies to spike dramatically. This is called **write stall** when RocksDB deliberately slows down incoming writes to allow compaction to catch up.

B-tree databases don't have this problem — writes cause small, localized changes (one or a few pages). LSM-tree compaction causes large bursts of I/O. This makes RocksDB unsuitable for workloads requiring consistently low latency (e.g., payment processing where P99 latency matters), but fine for high-throughput workloads that can tolerate occasional stalls (e.g., log ingestion).

### Space Amplification

During compaction, old and new SSTable versions of the same data coexist temporarily. For leveled compaction, this is brief (the input SSTables are deleted after compaction). But for universal compaction, multiple overlapping files at the same level mean the same key may exist in multiple SSTables simultaneously, consuming 2× or more space.

---

## 5. Experiments / Observations

### Write Amplification Benchmark (db_bench)

```bash
# RocksDB's built-in benchmark tool
./db_bench --benchmarks=fillrandom --num=10000000 \
           --compression_type=snappy \
           --compaction_style=0  # 0=Leveled, 1=Universal

# Monitor with:
./db_bench --benchmarks=stats
# Look for:
#   Cumulative writes: X ops, Y MBs written by app
#   Cumulative WAL: Z MBs written to WAL
#   Cumulative compaction: W MBs read/written
# Write amplification = (WAL_bytes + compaction_written_bytes) / app_bytes
```

Typical results:
- Leveled compaction on 10M random keys: Write Amp ≈ 25×, Read Amp ≈ 5×, Space Amp ≈ 1.1×
- Universal compaction: Write Amp ≈ 8×, Read Amp ≈ 15×, Space Amp ≈ 2×

### Bloom Filter Impact

```bash
# Compare read performance with/without Bloom filters:
./db_bench --benchmarks=readrandom --num=1000000 --bloom_bits=10
# vs
./db_bench --benchmarks=readrandom --num=1000000 --bloom_bits=0

# Expected: 10-50× faster reads with Bloom filters for missing-key lookups
```

### Compaction Stats

```
# From RocksDB stats output:
Compactions:
 Level  Files  Size(MB)  Score  Read(MB)  Rn(MB) Rnp1(MB)  Write(MB) Wnew(MB) RW-Amp
  L0      4      256       1.0      0        0        0       256       256      1.0
  L1      8     2048       1.1    2304     256     2048      2048         0      9.0
  L2     80    20480       1.0   23000    2048    20480     20480         0     11.2

# Rn = bytes read from level N
# Rnp1 = bytes read from level N+1 (overlap)
# Write amplification per level visible in RW-Amp column
```

### Tombstone Accumulation

```python
# Workload: insert 1M keys, delete 900K of them
# Without compaction:
#   Read latency for existing keys increases linearly with tombstone count
#   RocksDB must scan past tombstones to find live key

# Trigger manual compaction to force tombstone cleanup:
db.CompactRange(b'', b'\xff')  # compact all keys
# After compaction: read latency returns to baseline
```

---

## 6. Key Learnings

**LSM-trees are not universally better than B-trees — they're better for specific workloads.** The decision to use RocksDB over PostgreSQL or InnoDB should be driven by workload analysis: if writes dominate and you can tolerate occasional compaction-induced latency spikes, RocksDB wins on throughput. If reads dominate or latency predictability matters, B-trees win.

**Sequential I/O is the architectural insight.** The entire LSM-tree design — MemTable buffering, SSTable immutability, multi-level compaction — exists to convert random writes into sequential disk I/O. This is a profound insight: you can often trade more I/O for better-shaped I/O and come out ahead.

**Compaction is not just a background maintenance task — it's central to correctness.** Without compaction, read performance degrades unboundedly (more SSTable files to check), space amplification grows (old versions accumulate), and tombstones are never cleaned up. Compaction is what makes the "log-structured" part of LSM-trees work in practice. Getting compaction tuning right is essential for production RocksDB deployments.

**Bloom filters bridge the gap between write optimization and read performance.** Without Bloom filters, LSM-tree read performance for negative lookups (key doesn't exist) would be catastrophic. Bloom filters allow 10 bits per key to eliminate 99% of unnecessary SSTable reads. This is an elegant example of using a probabilistic data structure where the cost of false positives (occasional unnecessary disk read) is vastly lower than the cost of false negatives (impossible — we never miss an existing key).

**Write amplification is unavoidable — the question is where it happens.** B-trees have amplification in the form of page reads and writes during insert/update. LSM-trees have amplification during compaction. Choosing between them is choosing where you want the amplification to happen and when. LSM-trees batch it into background compaction; B-trees spread it across every write.

**RocksDB's influence has been enormous.** Its architecture is now the basis for storage engines in multiple distributed databases (CockroachDB, TiKV, YugabyteDB). Understanding RocksDB means understanding a significant fraction of modern distributed database internals.

---

*References: RocksDB documentation (rocksdb.org/docs), "An Analysis of Write Amplification in SSDs" (USENIX FAST), O'Neil et al. "The Log-Structured Merge-Tree" (1996), Facebook Engineering Blog posts on RocksDB, LevelDB source code (for context), RocksDB source (github.com/facebook/rocksdb)*
