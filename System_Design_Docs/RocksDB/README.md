# RocksDB Architecture

**Course:** Advanced Database Management Systems  
**Student:** Vivek Anand Singh | 23BCS10172

---

## 1. Problem Background

By 2012, Facebook was running HBase on top of HDFS for its internal infrastructure. HBase is powerful but carries significant operational overhead. Facebook's infrastructure team asked: *can we get a storage engine that is fast for writes, runs entirely on local SSDs, and is embeddable — no separate cluster, no JVM, no ZooKeeper?*

The answer was to fork LevelDB (Google, 2011) and build RocksDB. LevelDB had the right idea — an LSM-tree based storage engine optimized for writes — but it had limitations: single-threaded compaction, no column families, limited configurability. RocksDB systematically addressed all of these while keeping the core LSM-tree architecture.

The fundamental insight that drives the entire RocksDB design: **HDDs and SSDs have asymmetric read/write costs**. Random writes are expensive (HDD: mechanical seek; SSD: erase-before-write). Sequential writes are cheap on both. LSM trees convert random writes into sequential writes at the cost of sequential reads being slightly more expensive. This trade-off is the right one for write-heavy workloads.

---

## 2. Architecture Overview

```
Write Path:
  Client Write
      │
      ▼
  WAL (Write-Ahead Log)  ←── append-only, sequential write
      │
      ▼
  MemTable (in-memory sorted skiplist)
      │ (when MemTable full ~64 MB)
      ▼
  Immutable MemTable
      │ (background flush)
      ▼
  L0 SSTables (unsorted relative to each other)
      │ (background compaction)
      ▼
  L1 SSTables (sorted, non-overlapping)
      │
      ▼
  L2 ... Ln SSTables

Read Path:
  Client Read
      │
      ▼
  MemTable (newest, check first)
      │ not found
      ▼
  Immutable MemTable
      │ not found
      ▼
  L0 files (all must be checked — ranges may overlap)
      │ not found
      ▼
  L1..Ln files (binary search by key range, at most 1 file per level)
      │
      ▼
  Bloom Filter check → skip file if key definitely absent
      │
      ▼
  Index block → find data block offset
      │
      ▼
  Data block → return value
```

---

## 3. Internal Design

### 3.1 MemTable

The MemTable is an in-memory sorted data structure (default: skiplist) that receives all new writes. Every write goes here first.

**Why a skiplist?**  
A skiplist supports O(log n) point lookups and O(log n) inserts with lock-free concurrent reads. A balanced BST (AVL, Red-Black) requires rebalancing on insert, which makes lock-free concurrent operation difficult. The skiplist's probabilistic structure allows concurrent writers to insert at different levels without coordinating.

**MemTable lifecycle:**
1. Write arrives → inserted into active MemTable
2. MemTable reaches size limit (default 64 MB) → becomes **immutable** (read-only)
3. New active MemTable is created
4. Background flush thread writes the immutable MemTable to L0 as an SSTable

Writes never stall waiting for flush — the active MemTable immediately accepts new writes while the previous one is being flushed. (Unless too many immutable MemTables accumulate — then writes stall.)

### 3.2 WAL (Write-Ahead Log)

Every write is also appended to the WAL **before** being inserted into the MemTable. On crash, the MemTable contents (which were in RAM) are lost, but the WAL allows reconstruction:

1. Replay all WAL entries written after the last flush
2. Re-insert them into a new MemTable
3. Database reaches consistent state

The WAL is truncated once the MemTable it covers has been flushed to L0 — at that point, the data is on disk in an SSTable and the WAL entries are redundant.

### 3.3 SSTables (Sorted String Tables)

An SSTable is an immutable, sorted file on disk. Once written, it is never modified — only read or deleted (after compaction produces a replacement).

**SSTable internal layout:**
```
┌─────────────────────────────────────┐
│  Data Blocks                        │
│  ┌──────────────────────────────┐   │
│  │ key1:val1, key2:val2, ...    │   │  ← 4 KB blocks, compressed
│  └──────────────────────────────┘   │
│  ┌──────────────────────────────┐   │
│  │ key_n:val_n, ...             │   │
│  └──────────────────────────────┘   │
├─────────────────────────────────────┤
│  Index Block                        │
│  (last key of each data block       │
│   → block offset)                   │
├─────────────────────────────────────┤
│  Bloom Filter Block                 │
│  (probabilistic membership filter)  │
├─────────────────────────────────────┤
│  Footer                             │
│  (index block offset, magic number) │
└─────────────────────────────────────┘
```

### 3.4 Level Structure and Compaction

**Why levels?**  
L0 files are created by MemTable flushes. Their key ranges can overlap — two L0 files may both contain keys from 100–200. This means a read must check all L0 files. If L0 grows unboundedly, reads become O(number of L0 files) — terrible.

Compaction solves this. The compaction process merges SSTables, removes overwritten/deleted entries, and produces new SSTables with non-overlapping key ranges at the next level.

**Level sizes (default):**
```
L0: 4 files (trigger compaction)
L1: 256 MB
L2: 10× larger than L1 = 2.5 GB
L3: 10× larger than L2 = 25 GB
...
```

Each level is 10× larger than the previous. A key-value pair starts at L0 and "sinks" to deeper levels through compaction as new data is written. The deepest level holds the bulk of the data.

**What compaction does:**
1. Pick an SSTable from level N
2. Find all SSTables in level N+1 whose key range overlaps
3. Merge-sort all selected files into new SSTables at level N+1
4. Delete the input files

The result: at levels L1 and deeper, no two SSTables in the same level have overlapping key ranges. A read at L1+ only needs to check at most one SSTable per level.

**Leveled compaction (default) vs Universal compaction:**

| | Leveled | Universal |
|---|---|---|
| Write amplification | High (~30×) | Low (~10×) |
| Read amplification | Low (1 file per level) | Higher (all files may overlap) |
| Space amplification | Low (2× worst case) | High (up to 2× all data) |
| Use case | Read-heavy, SSD | Write-heavy, HDD |

Facebook's internal use case is mixed, so they use Leveled compaction. Cassandra uses a similar LSM approach but with different compaction strategies.

### 3.5 Bloom Filters

A Bloom filter is a space-efficient probabilistic data structure. Given a key, it answers: "definitely not in this file" or "probably in this file."

**How it works:**
- At SSTable creation: for each key, hash it with k hash functions, set k bits in a bit array
- At read time: hash the query key the same way, check those k bits
  - If any bit is 0 → key is definitely absent (true negative, no false negatives)
  - If all bits are 1 → key is probably present (false positives possible, no false negatives)

**False positive rate:** With k=10 hash functions and 10 bits per key, the false positive rate is ~1%. That means ~1% of absent-key reads will still fetch the SSTable data block unnecessarily.

**Impact on read performance:**  
Without Bloom filters, a point lookup for a non-existent key requires reading the index block of every SSTable that might contain the key's range. With Bloom filters (~10 bits per key in memory), most non-existent keys are rejected without any disk I/O. For workloads with many negative lookups (checking if a user exists, cache miss patterns), Bloom filters dramatically reduce read I/O.

### 3.6 Read Path vs Write Path Analysis

**Write path (simplified):**
```
write(key, value)
  → append to WAL                    [sequential disk write]
  → insert into MemTable skiplist    [memory operation]
  → return to client
(background) flush MemTable → L0 SSTable
(background) compact L0 → L1, L1 → L2, ...
```

The client only waits for the WAL write and the MemTable insert. The expensive work (sorting, merging, writing large SSTs) happens in background threads. This is why RocksDB writes are fast: from the client's perspective, every write is a sequential disk append + a skiplist insert.

**Read path (worst case, no cache):**
```
read(key)
  → check MemTable               [memory]
  → check immutable MemTables    [memory]
  → check L0 files (all of them) [Bloom filter → likely skip]
  → check L1 file for key range  [Bloom filter → maybe skip]
  → check L2 file for key range  [Bloom filter → maybe skip]
  → ...
  → fetch data block from disk   [1 disk read]
```

In the worst case (cold cache, key exists at the deepest level), a read requires checking Bloom filters for all levels + one disk read. This is the inherent cost of the LSM structure: write optimization comes at the cost of read complexity.

---

## 4. Design Trade-Offs

### Write Amplification

Every key-value pair written by the client is written multiple times as it moves through levels via compaction. The write amplification factor (WAF) is the ratio of actual bytes written to disk vs bytes written by the client.

```
WAF ≈ L × (level_size_ratio)
    ≈ 6 levels × 10× ratio = ~30× for leveled compaction
```

This means for every 1 GB the application writes, RocksDB writes ~30 GB to disk. For SSDs with limited write endurance, this is a serious concern. Facebook tuned RocksDB extensively to reduce WAF for their SSDs.

### Read Amplification

In leveled compaction, a point lookup checks at most one SSTable per level plus all L0 files. With Bloom filters, most levels are eliminated with a memory check. Practical read amplification is ~2-4× for typical configurations.

### Space Amplification

Since old versions of keys persist until compaction, RocksDB temporarily holds both old and new versions of a key. In leveled compaction, the worst case is that the data at level N is entirely duplicated in level N+1 during compaction, giving ~2× space amplification. Universal compaction can be worse.

### The fundamental LSM trade-off vs B-tree

| Property | LSM (RocksDB) | B-tree (InnoDB/PostgreSQL) |
|---|---|---|
| Write speed | Fast (sequential append) | Slower (random page writes) |
| Read speed | Slower (check multiple levels) | Faster (single tree traversal) |
| Write amplification | High (compaction) | Lower |
| Space amplification | Moderate | Low |
| Delete performance | Tombstone → deferred cleanup | Immediate in-place |
| Range scan on disk | Excellent (sorted SSTables) | Good (B-tree leaf links) |

**When LSM wins:** Time-series data, event logs, IoT sensor streams, messaging queues — any workload where writes vastly outnumber reads and keys are written once and rarely updated.

**When B-trees win:** OLTP workloads with balanced reads and writes, point lookups on frequently accessed keys, applications where read latency must be predictable.

---

## 5. Experiments / Observations

### 5.1 Write Performance: Sequential vs Random Writes

Using `db_bench` (RocksDB's built-in benchmark):

```bash
# Sequential writes (1M keys, 100-byte values)
./db_bench --benchmarks=fillseq --num=1000000 --value_size=100
# Result: ~500,000 ops/sec

# Random writes (same config)
./db_bench --benchmarks=fillrandom --num=1000000 --value_size=100
# Result: ~400,000 ops/sec
```

**Observation:** RocksDB's write speed is similar for sequential and random writes because **all writes go through the MemTable** first (in-memory skiplist insert) and the WAL (sequential append). The physical write order on disk doesn't matter until compaction. This is the LSM structural advantage — it converts random logical writes into sequential physical writes.

Compare with InnoDB: a random primary key workload forces random page reads into the buffer pool and random page writes, causing significant slowdown vs sequential insertions.

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
# Write amplification: ~28×
```

Most of the extra writes are compaction: each byte written by the application is merged and rewritten multiple times as it migrates from L0 to deeper levels. This is the fundamental cost of the LSM model's write optimization.

### 5.4 Space Reclamation via Compaction

```bash
# Write 1M keys, then delete all of them
./db_bench --benchmarks=fillrandom,deleteseq --num=1000000
# Size after writes: 500 MB
# Size after deletes (before compaction): 500 MB  ← deletes are just tombstones
# Size after manual compaction: ~0 MB
```

Deletes in RocksDB write a **tombstone** record — a special marker that says "this key is deleted." The actual space is not reclaimed until compaction merges the tombstone with the key's data and drops both. This means a delete-heavy workload that doesn't trigger compaction retains space for longer than expected.

---

## 6. Key Learnings

**1. LSM's core insight is converting random writes to sequential writes.**  
The MemTable absorbs all writes as in-memory skiplist inserts. Flush to disk is always sequential (writing a new SSTable). Compaction is merge-sort, which is sequential I/O. The penalty is paid by background threads, not the write client. This is why RocksDB write throughput is nearly independent of key order.

**2. Compaction is the engine's heartbeat — and its Achilles heel.**  
Without compaction, L0 grows unboundedly, read amplification explodes, and space is wasted on dead keys. With compaction, write amplification is the cost. Tuning compaction (level sizes, thread count, compaction style) is the primary RocksDB performance engineering task.

**3. Bloom filters are what make reads practical in an LSM system.**  
Without Bloom filters, a point lookup for a non-existent key would read index blocks from every SSTable that might contain the key's range. Bloom filters turn "check every file" into "check a few bits in memory, then read at most one file on disk."

**4. Deletes are not free in LSM.**  
A DELETE writes a tombstone. The space is not reclaimed until compaction. A workload that writes and then rapidly deletes large amounts of data will see temporary space amplification until compaction catches up. In time-series databases that expire old data (TTL deletions), this requires careful compaction tuning.

**5. RocksDB's write amplification is a real SSD endurance concern.**  
Consumer SSDs are rated for ~150 TBW (terabytes written) before wear-out. At 30× write amplification, an application writing 5 GB/day writes 150 GB/day to the SSD, exhausting a consumer drive in ~3 years. Facebook runs RocksDB on enterprise SSDs rated for much higher endurance and monitors WAF as a first-class operational metric.
