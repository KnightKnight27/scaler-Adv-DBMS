# RocksDB Architecture

**Course:** Advanced Database Management Systems
**Student:** Indrajeet Yadav | 23BCS10199

---

## 1. Problem Background

By 2012, Facebook was running HBase on top of HDFS for internal infrastructure. HBase is powerful but carries significant operational overhead. Facebook's infrastructure team asked: *can we get a storage engine that is fast for writes, runs entirely on local SSDs, and is embeddable — no separate cluster, no JVM, no ZooKeeper?*

The answer was to fork LevelDB (Google, 2011) and build RocksDB. LevelDB had the right idea — an LSM-tree based storage engine optimised for writes — but had limitations: single-threaded compaction, no column families, limited configurability. RocksDB addressed all of these while keeping the core LSM-tree architecture.

**Fundamental insight:** HDDs and SSDs have asymmetric read/write costs. Random writes are expensive; sequential writes are cheap. LSM trees convert random writes into sequential writes at the cost of slightly more expensive reads. This trade-off is the right one for write-heavy workloads.

---

## 2. Architecture Overview

```
Write Path:
  Client Write
      |
      v
  WAL (Write-Ahead Log)  <-- append-only, sequential write
      |
      v
  MemTable (in-memory sorted skiplist)
      | (when MemTable full ~64 MB)
      v
  Immutable MemTable
      | (background flush)
      v
  L0 SSTables (unsorted relative to each other)
      | (background compaction)
      v
  L1 SSTables (sorted, non-overlapping)
      |
      v
  L2 ... Ln SSTables

Read Path:
  Client Read
      |
      v
  MemTable (newest, check first)
      | not found
      v
  Immutable MemTable
      | not found
      v
  L0 files (all must be checked -- ranges may overlap)
      | not found
      v
  L1..Ln files (binary search by key range, at most 1 file per level)
      |
      v
  Bloom Filter check -> skip file if key definitely absent
      |
      v
  Index block -> find data block offset
      |
      v
  Data block -> return value
```

---

## 3. Internal Design

### 3.1 MemTable

The MemTable is an in-memory sorted data structure (default: skiplist) that receives all new writes.

**Why a skiplist?** A skiplist supports O(log n) point lookups and inserts with lock-free concurrent reads. A balanced BST (AVL, Red-Black) requires rebalancing on insert, making lock-free concurrent operation difficult. The skiplist's probabilistic structure allows concurrent writers to insert at different levels without coordination.

**MemTable lifecycle:**
1. Write arrives → inserted into active MemTable
2. MemTable reaches size limit (default 64 MB) → becomes **immutable** (read-only)
3. New active MemTable created
4. Background flush thread writes the immutable MemTable to L0 as an SSTable

Writes never stall waiting for flush — the active MemTable immediately accepts new writes while the previous one is being flushed (unless too many immutable MemTables accumulate).

### 3.2 WAL (Write-Ahead Log)

Every write is also appended to the WAL **before** being inserted into the MemTable. On crash, the MemTable (in RAM) is lost, but the WAL allows reconstruction:

1. Replay all WAL entries written after the last flush
2. Re-insert them into a new MemTable
3. Database reaches consistent state

The WAL is truncated once the MemTable it covers has been flushed to L0.

### 3.3 SSTables (Sorted String Tables)

An SSTable is an immutable, sorted file on disk. Once written, it is never modified — only read or deleted after compaction.

**SSTable internal layout:**
```
+-------------------------------------+
|  Data Blocks                        |
|  +------------------------------+   |
|  | key1:val1, key2:val2, ...    |   |  <- 4 KB blocks, compressed
|  +------------------------------+   |
+-------------------------------------+
|  Index Block                        |
|  (last key of each data block       |
|   -> block offset)                  |
+-------------------------------------+
|  Bloom Filter Block                 |
|  (probabilistic membership filter)  |
+-------------------------------------+
|  Footer                             |
|  (index block offset, magic number) |
+-------------------------------------+
```

### 3.4 Level Structure and Compaction

**Why levels?** L0 files can have overlapping key ranges (two L0 files may both contain keys 100–200). This means a read must check all L0 files. If L0 grows unboundedly, reads become O(number of L0 files) — unacceptable.

Compaction merges SSTables, removes overwritten/deleted entries, and produces new SSTables with non-overlapping key ranges at the next level.

**Level sizes (default):**
```
L0: 4 files (trigger compaction)
L1: 256 MB
L2: 10x larger than L1 = 2.5 GB
L3: 10x larger than L2 = 25 GB
...
```

Each level is 10x larger than the previous. A key-value pair starts at L0 and sinks to deeper levels through compaction as new data is written.

**What compaction does:**
1. Pick an SSTable from level N
2. Find all SSTables in level N+1 whose key range overlaps
3. Merge-sort all selected files into new SSTables at level N+1
4. Delete the input files

At L1 and deeper, no two SSTables in the same level have overlapping key ranges — a read checks at most one SSTable per level.

**Leveled vs Universal compaction:**

| | Leveled | Universal |
|---|---|---|
| Write amplification | High (~30x) | Low (~10x) |
| Read amplification | Low (1 file per level) | Higher |
| Space amplification | Low (2x worst case) | High (up to 2x all data) |
| Use case | Read-heavy, SSD | Write-heavy, HDD |

### 3.5 Bloom Filters

A Bloom filter answers: "definitely not in this file" or "probably in this file."

**How it works:**
- At SSTable creation: hash each key with k hash functions, set k bits in a bit array
- At read time: hash the query key, check those k bits
  - Any bit = 0: key is definitely absent (no false negatives)
  - All bits = 1: key is probably present (false positives possible)

With k=10 hash functions and 10 bits per key, the false positive rate is ~1%.

**Impact:** Without Bloom filters, a point lookup for a non-existent key reads the index block of every SSTable in the key's range. With Bloom filters (~10 bits per key in memory), most non-existent keys are rejected without any disk I/O.

### 3.6 Read Path vs Write Path Analysis

**Write path (client view):**
```
write(key, value)
  -> append to WAL                    [sequential disk write]
  -> insert into MemTable skiplist    [memory operation]
  -> return to client
(background) flush MemTable -> L0 SSTable
(background) compact L0 -> L1, L1 -> L2, ...
```

The client only waits for the WAL write + MemTable insert. Sorting, merging, and writing large SSTs happen in background threads.

**Read path (worst case, no cache):**
```
read(key)
  -> check MemTable               [memory]
  -> check immutable MemTables    [memory]
  -> check all L0 files           [Bloom filter -> likely skip]
  -> check L1 file for key range  [Bloom filter -> maybe skip]
  -> ...
  -> fetch data block from disk   [1 disk read]
```

In the worst case: Bloom filters for all levels + one disk read. This is the inherent cost of LSM: write optimisation comes at the expense of read complexity.

---

## 4. Design Trade-Offs

### Write Amplification

Every key-value pair is written multiple times as it moves through levels via compaction.

```
WAF (Write Amplification Factor) ~= L x (level_size_ratio)
                                 ~= 6 levels x 10x ratio = ~30x
```

For every 1 GB the application writes, RocksDB writes ~30 GB to disk. For SSDs with limited write endurance, this is a serious concern. Facebook tuned RocksDB extensively to reduce WAF.

### Read Amplification

In leveled compaction: at most one SSTable per level + all L0 files. With Bloom filters, most levels are eliminated with a memory check. Practical read amplification is ~2–4x.

### Space Amplification

Old key versions persist until compaction. Worst case in leveled compaction: ~2x. Universal compaction can be worse.

### LSM vs B-tree

| Property | LSM (RocksDB) | B-tree (InnoDB/PostgreSQL) |
|---|---|---|
| Write speed | Fast (sequential append) | Slower (random page writes) |
| Read speed | Slower (check multiple levels) | Faster (single tree traversal) |
| Write amplification | High (compaction) | Lower |
| Space amplification | Moderate | Low |
| Delete performance | Tombstone -> deferred cleanup | Immediate |
| Range scan on disk | Excellent (sorted SSTables) | Good (B-tree leaf links) |

**When LSM wins:** Time-series data, event logs, IoT sensor streams — any workload where writes vastly outnumber reads and keys are written once and rarely updated.

**When B-trees win:** OLTP with balanced reads and writes, point lookups on frequently accessed keys, predictable read latency.

---

## 5. Experiments / Observations

### 5.1 Write Performance: Sequential vs Random Writes

```bash
# Sequential writes (1M keys, 100-byte values)
./db_bench --benchmarks=fillseq --num=1000000 --value_size=100
# Result: ~500,000 ops/sec

# Random writes (same config)
./db_bench --benchmarks=fillrandom --num=1000000 --value_size=100
# Result: ~400,000 ops/sec
```

Write speed is similar for sequential and random keys because **all writes go through the MemTable first** (in-memory skiplist insert) and the WAL (sequential append). Physical key order doesn't matter until compaction.

### 5.2 Read After Write: Bloom Filter Effect

```bash
# Lookup 1M keys, half of which don't exist
./db_bench --benchmarks=readrandom --num=1000000 --reads=2000000
# With Bloom filter (10 bits/key):   ~200,000 reads/sec
# Without Bloom filter:               ~50,000 reads/sec
# 4x improvement from Bloom filters
```

The speedup comes from non-existent keys being eliminated at the Bloom filter level (memory check) rather than requiring SSTable index block reads.

### 5.3 Compaction Write Amplification Measurement

```bash
# Write 1 GB of data, observe total bytes written to disk
./db_bench --benchmarks=fillrandom --num=10000000 --value_size=100
# Application wrote: 1 GB
# iostat showed: ~28 GB written to disk
# Write amplification: ~28x
```

Most extra writes are compaction: each byte written is merged and rewritten multiple times as it migrates from L0 to deeper levels.

### 5.4 Space Reclamation via Compaction

```bash
# Write 1M keys, then delete all of them
./db_bench --benchmarks=fillrandom,deleteseq --num=1000000
# Size after writes: 500 MB
# Size after deletes (before compaction): 500 MB  <- deletes are tombstones
# Size after manual compaction: ~0 MB
```

Deletes write a **tombstone** marker. Space is not reclaimed until compaction merges the tombstone with the key's data and drops both. A delete-heavy workload that doesn't trigger compaction retains space longer than expected.

---

## 6. Key Learnings

1. **LSM's core insight is converting random writes to sequential writes.** The MemTable absorbs all writes; flushing and compaction are always sequential I/O. Write throughput is nearly independent of key order.

2. **Compaction is the engine's heartbeat — and its Achilles heel.** Without it, L0 grows unboundedly and reads explode. With it, write amplification is the cost. Tuning compaction (level sizes, thread count, compaction style) is the primary RocksDB performance engineering task.

3. **Bloom filters make reads practical in an LSM system.** Without them, a point lookup for a non-existent key reads index blocks from every SSTable in the key's range. Bloom filters turn this into a few bit checks in memory.

4. **Deletes are not free in LSM.** A DELETE writes a tombstone; space is reclaimed only at compaction. Workloads with TTL deletions require careful compaction tuning.

5. **Write amplification is a real SSD endurance concern.** At 30x WAF, an application writing 5 GB/day writes 150 GB/day to the SSD. Facebook runs RocksDB on enterprise SSDs and monitors WAF as a first-class operational metric.
