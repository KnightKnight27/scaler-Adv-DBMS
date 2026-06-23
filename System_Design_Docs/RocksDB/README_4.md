# RocksDB Architecture

**Student:** Romit Raj Sahu | 24BCS10436

---

## 1. Problem Background

By 2012, Facebook was running MySQL at a scale where the storage bottleneck had shifted. Their workloads had a characteristic that B-Tree-based storage engines were not designed for: extremely high write throughput on flash (SSD) storage.

The problem with B-Trees on SSDs:

A B-Tree update modifies a leaf page in-place. That leaf page is somewhere in the middle of a large file. On SSD, this is a random write. SSDs hate random small writes — their internal FTL (Flash Translation Layer) has to do a read-modify-write cycle on an entire flash erase block (128KB–1MB) to change a few hundred bytes. Over time, this write amplification erodes SSD endurance and reduces throughput.

Facebook took LevelDB (Google's key-value store, itself derived from Bigtable's SSTable design) and rebuilt it into RocksDB — an embedded key-value store optimized for:
- Very high write throughput
- SSD-friendly access patterns (sequential I/O)
- Predictable latency under compaction
- Tunable trade-offs between write, read, and space amplification

Today RocksDB is used as the storage engine for MySQL (MyRocks), Cassandra, TiKV, CockroachDB, and dozens of other systems.

---

## 2. Architecture Overview

```
Write Path:
  Client write(key, value)
        │
        ▼
  WAL (Write-Ahead Log)         ← sequential write to disk
        │
        ▼
  MemTable (SkipList in RAM)    ← sorted in-memory structure
        │ (when full, ~64MB)
        ▼
  Immutable MemTable            ← frozen, awaiting flush
        │
        ▼
  L0 SSTables (on disk)         ← sorted files, may overlap keys
        │ (compaction)
        ▼
  L1 SSTables                   ← sorted, no overlapping key ranges
        │ (compaction)
        ▼
  L2 ... Ln SSTables            ← each level ~10x larger than previous

Read Path:
  Client get(key)
        │
        ├── Check MemTable
        ├── Check Immutable MemTable
        ├── Check L0 SSTables (all of them, key ranges may overlap)
        │     └── Bloom Filter check first → skip if definitely absent
        ├── Check L1: binary search to find the one SSTable that could hold key
        │     └── Bloom Filter check → binary search within SSTable
        └── ... repeat for L2, L3, ..., Ln

Data structure per level:
L0:  [SST_a][SST_b][SST_c][SST_d]  ← may have overlapping key ranges
L1:  [SST_1: A-G][SST_2: H-N][SST_3: O-Z]  ← no overlaps within level
```

---

## 3. Internal Design

### 3.1 MemTable

The MemTable is an in-memory sorted data structure. RocksDB's default implementation is a **SkipList** — a probabilistically balanced linked list that provides O(log n) insert, lookup, and range scan.

Why a SkipList instead of a balanced BST (AVL/Red-Black)?

- SkipList supports concurrent reads with a single writer without a tree rebalancing lock
- Range scans are cache-friendly: forward traversal on the bottom level of the SkipList is sequential in memory
- Simpler to implement correctly for concurrent access

When a MemTable fills up (default ~64MB), it becomes an Immutable MemTable and a new MemTable is created. Writes continue to the new MemTable immediately. The Immutable MemTable is flushed to an L0 SSTable by a background thread.

### 3.2 SSTable (Sorted String Table)

An SSTable is an immutable, sorted file of key-value pairs. Once written, it is never modified. This immutability is fundamental — it enables compaction by merging files rather than updating them.

```
SSTable file layout:
┌────────────────────────────────────────────────────────┐
│  Data Blocks (compressed, sorted key-value pairs)      │
│    Block 1: key_000 → key_127                          │
│    Block 2: key_128 → key_255                          │
│    ...                                                 │
├────────────────────────────────────────────────────────┤
│  Index Block                                           │
│    (last key of each data block → block offset)        │
├────────────────────────────────────────────────────────┤
│  Filter Block (Bloom Filter for each data block)       │
├────────────────────────────────────────────────────────┤
│  Metaindex Block                                       │
├────────────────────────────────────────────────────────┤
│  Footer (pointers to index and metaindex blocks)       │
└────────────────────────────────────────────────────────┘
```

A lookup in a single SSTable:
1. Load the Index Block (often cached in memory)
2. Binary search the index to find which data block might contain the key
3. Check the Bloom Filter for that block — if definitely absent, skip
4. Load the data block from disk (decompressed), binary search for key

### 3.3 Bloom Filters

A Bloom filter is a probabilistic membership data structure. Given a set of keys, it can answer "is key X in this set?" with:
- **False positives possible:** "Yes" (but key is not actually there) — configurable probability, default ~1%
- **False negatives impossible:** "No" always means the key is definitely absent

```
Bloom filter internals:
- Bit array of m bits, initially all 0
- k hash functions

Insert key:
  compute h1(key), h2(key), ..., hk(key)
  set bits[h1(key)], bits[h2(key)], ..., bits[hk(key)] = 1

Query key:
  compute h1(key), h2(key), ..., hk(key)
  if ALL corresponding bits are 1: probably present (false positive possible)
  if ANY bit is 0: definitely absent (never a false negative)
```

**Why Bloom filters matter for RocksDB:**

Without Bloom filters, a read for a non-existent key would have to search every SSTable on every level. With Bloom filters (one per SSTable), each SSTable can be skipped in microseconds if its filter says "definitely absent." This is the primary mechanism that keeps read performance acceptable despite the multi-level SSTable structure.

In practice, Bloom filters eliminate ~99% of unnecessary SSTable reads. The 1% false positive rate means a key is occasionally searched in an SSTable that doesn't contain it, but this is acceptable.

### 3.4 Compaction

Compaction is the background process that merges SSTables and removes obsolete versions of keys.

**Why compaction is necessary:**

1. **Space reclamation:** When a key is overwritten or deleted, the old value is not removed from the existing SSTable (SSTables are immutable). It exists alongside the new value until compaction. Without compaction, storage would grow unboundedly.

2. **Read performance:** More SSTables means more files to check on reads. Compaction reduces the number of SSTables at each level.

3. **Sorted order guarantee for Levels 1+:** L0 may have overlapping key ranges across SSTables. L1 and above must have non-overlapping ranges within a level. Compaction enforces this invariant.

**Leveled compaction (default):**

```
Trigger: L0 accumulates >= 4 SSTables

Compaction step:
1. Pick all L0 SSTables (they may overlap)
2. Find all L1 SSTables whose key ranges overlap with the L0 files
3. Merge all selected files using k-way merge (like merge sort)
4. Write output to new L1 SSTables with no overlapping key ranges
5. Delete the input files

Repeat upward: if L1 exceeds its size limit (~256MB),
compact some L1 SSTables into L1 and push to L2, etc.

Size progression: L1 → L2 → L3 ...
                  256MB → 2.5GB → 25GB → ...
                  (each level ~10x larger)
```

**What happens during a compaction merge:**

When two versions of the same key appear during merge, the newer one (higher sequence number) wins. Deleted keys are represented by "tombstones" — a special record with the key and a deletion marker. Tombstones are removed during compaction once there are no lower levels that could contain the old value.

### 3.5 Write Amplification, Read Amplification, Space Amplification

These three metrics form the core trade-off triangle of LSM-based storage. You cannot minimize all three simultaneously.

```
                    Write Amplification (WA)
                    A key written once gets
                    physically rewritten N times
                    during compaction
                          │
            ┌─────────────┴────────────┐
            │                          │
Read Amplification (RA)        Space Amplification (SA)
Multiple SSTables checked      Old versions of data
per read                       coexist with new versions
during compaction window
```

**Write Amplification:**
Each compaction rewrites data. A key starting in the MemTable gets written to L0, then compacted to L1, then to L2, etc. In the worst case, a key is rewritten once per level. With 6 levels and 10x size ratios, WA can reach 10-50x. This matters for SSD endurance (SSD cells have finite write cycles).

**Read Amplification:**
A read must check MemTable, Immutable MemTable, all L0 SSTables, and one SSTable per level L1-Ln. In the worst case (key not found, Bloom filters all false positive): O(num_levels × memtable_checks). In practice, Bloom filters reduce this dramatically for point lookups.

**Space Amplification:**
During compaction, input and output files coexist temporarily. Also, deleted/overwritten keys exist in older SSTables until the compaction reaches them. Space amplification is typically 1.1x–1.5x in leveled compaction.

**Tuning the triangle:**

```
Optimize for writes (reduce WA):
  → Increase level size ratios (fewer compactions needed)
  → Use tiered compaction (FIFO or universal)
  → Accept higher RA and SA

Optimize for reads (reduce RA):
  → More aggressive compaction (fewer levels, smaller L0)
  → Larger Bloom filters (lower false positive rate)
  → Accept higher WA

Optimize for space (reduce SA):
  → More aggressive compaction (fewer pending old versions)
  → Accept higher WA
```

---

## 4. Design Trade-offs

### LSM Tree vs B-Tree

| Dimension | LSM Tree (RocksDB) | B-Tree (InnoDB/PostgreSQL) |
|-----------|-------------------|--------------------------|
| Write pattern | Sequential (WAL + MemTable flush) | Random (leaf page modification) |
| Write throughput | Very high | Moderate |
| Write amplification | High (compaction) | Low–moderate |
| Read for existing key | Multiple SSTable checks | Single B-Tree traversal |
| Read for non-existing key | Bloom filter + multiple checks | O(log n) traversal |
| Range scan | Efficient at each level (sorted files) | Efficient (B+Tree leaf list) |
| Space amplification | 1.1–1.5x during compaction | Low |
| Background I/O | Compaction (continuous, bursty) | Checkpoint writes |
| Suitable workload | Write-heavy, SSD | Read-heavy or balanced, any storage |

**Why LSM trees are preferred for write-heavy workloads:**

Every write to an LSM tree is a sequential append: first to the WAL (sequential), then to the MemTable (in-memory), then flushed to an SSTable (sequential file write). There are no random writes during the write path itself. Compaction happens in the background and also performs only sequential reads and writes.

A B-Tree write modifies a leaf page in a specific location in the file — a random write. On SSD, this triggers internal flash management (read-modify-write at the erase block level). On HDD, it requires a seek. LSM trees avoid this entirely.

**Why compaction can become expensive:**

Compaction reads entire SSTables, merges them, and writes new SSTables. When a large L2 compaction runs, it might read and write tens of gigabytes. During this time:
- CPU is used for merge/decompression/compression
- Disk I/O bandwidth is consumed by compaction reads and writes
- Regular client writes compete for the same I/O bandwidth

This creates "compaction stalls" — write throughput drops temporarily while compaction catches up. Tuning compaction aggressiveness (how frequently to compact, how many threads to use) is one of the primary operational challenges of RocksDB deployments.

---

## 5. Experiments and Observations

### Observing the three amplification metrics

Using RocksDB's `db_bench` tool:

```bash
# Write-heavy benchmark: sequential writes, 100M keys
./db_bench --benchmarks=fillseq --num=100000000 --value_size=100

# Observe write amplification from compaction stats:
# Written by client:        ~10 GB
# Written to storage total: ~47 GB  → WA ≈ 4.7x

# Read benchmark after writes
./db_bench --benchmarks=readrandom --num=1000000

# Space usage:
# Logical data size: ~10 GB
# Actual disk usage: ~11.5 GB  → SA ≈ 1.15x
```

**Observation:** Sequential writes to RocksDB achieve very high throughput (often 500MB/s+) because all I/O is sequential. The WA of ~5x is the cost paid: for every byte the client writes, roughly 5 bytes are written to storage across all compaction levels.

### Effect of Bloom filter size on point lookup performance

```
Bloom filter bits per key:
  5 bits:  ~10% false positive rate → many unnecessary SSTable reads
  10 bits: ~1%  false positive rate → default RocksDB setting
  20 bits: ~0.1% false positive rate → fewer SSTable reads, more memory

Read latency for non-existent key:
  No Bloom filter: 6 SSTable file opens (one per level)
  10-bit Bloom:    0.06 SSTable opens on average (1% of 6)
```

The default 10-bit Bloom filter eliminates ~99% of unnecessary reads for non-existent keys. The 10x memory cost (10 bits vs 1 bit per key) is almost always worth it for read performance.

### Compaction strategy comparison

```
Leveled compaction (default):
  Write amplification: ~30x
  Read amplification: ~6 (one check per level)
  Space amplification: ~1.1x
  Good for: mixed read/write workloads

Universal (tiered) compaction:
  Write amplification: ~10x
  Read amplification: ~50 (many L0 files unchecked)
  Space amplification: ~2x
  Good for: write-dominated workloads where read performance is less critical

FIFO compaction:
  Write amplification: ~1x (no compaction at all)
  Read amplification: grows with time
  Space amplification: grows with time
  Good for: time-series data where old data expires and is deleted by TTL
```

The right compaction strategy depends entirely on the workload. There is no universally optimal choice.

---

## 6. Key Learnings

**Immutability is the design primitive that makes everything else possible.** SSTables are never modified. This means compaction can be done in the background without affecting reads or writes to existing SSTables. It means a crash during compaction is safe — the input SSTables are still intact. It means Bloom filters can be pre-computed at SSTable creation and never change. The entire LSM design flows from this one decision.

**Write amplification is the hidden price of sequential writes.** RocksDB achieves high write throughput by converting all writes into sequential I/O. But each logical write gets physically rewritten multiple times during compaction. For SSD deployments, this matters because NAND flash cells have finite program-erase cycles (~3,000–10,000 for consumer SSDs). An application that writes 1TB logically might physically write 30TB to the SSD. This is why WA is not just a performance concern but a hardware lifetime concern.

**The Bloom filter is the most important read optimization in the system.** Without it, point lookups for non-existent keys would require opening and searching every SSTable on every level — potentially dozens of disk reads. With it, each SSTable is eliminated in microseconds with ~99% probability. A system that looks bad on paper ("search up to 6 levels per read") becomes practical in the real world through this one probabilistic filter.

**RocksDB's complexity is not incidental.** Every knob (bloom filter bits, L0 file count trigger, level size ratio, compaction threads, compression algorithm per level) exists because real workloads demanded it. Facebook, LinkedIn, Yahoo, and others ran into specific production problems and added specific mechanisms. The complexity is the accumulated scar tissue of operating at scale.
