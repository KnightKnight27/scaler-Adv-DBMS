# RocksDB Architecture

**Author:** Praveen Kumar | 24BCS10048

---

## 1. Problem Background

RocksDB was forked from Google's LevelDB by Facebook in 2012. The motivation: Facebook needed a storage engine optimized for fast storage hardware (SSDs, NVMe) that could handle their massive write throughput (hundreds of millions of key-value operations per second across their fleet).

Traditional B-tree engines (InnoDB, PostgreSQL) do in-place updates, which means random writes to disk. On HDDs this is slow; on SSDs it causes write amplification that wears out the device. RocksDB uses a fundamentally different data structure -- the **Log-Structured Merge tree (LSM tree)** -- that converts random writes into sequential writes.

RocksDB is now the storage engine behind:
- **MyRocks** (MySQL with RocksDB instead of InnoDB)
- **CockroachDB** (distributed SQL)
- **TiKV** (distributed KV store for TiDB)
- **Kafka Streams** (state stores)

---

## 2. Architecture Overview

```
Write path                          Read path
==========                          =========

PUT(k, v)                           GET(k)
    |                                   |
    v                                   v
+--------+                         +--------+
|  WAL   | (append-only log)       | MemTable| (check active first)
+--------+                             |
    |                                   v
    v                              +----------+
+--------+                         | Immutable | (check frozen table)
| MemTable| (sorted, in-memory)    | MemTable  |
+--------+                             |
    |  (when full)                      v
    v                              +--------+
+----------+                       | L0     | (check each L0 file)
| Immutable |                      | SSTs   | (may overlap keys)
| MemTable  |                          |
+----------+                           v
    |  (flush to disk)             +--------+
    v                              | L1     | (binary search, no overlap)
+--------+                         +--------+
| L0 SST | (sorted runs)              |
+--------+                             v
    |  (compaction)                +--------+
    v                              | L2     | (10x larger than L1)
+--------+                         +--------+
| L1 SST | (sorted, no overlap)       |
+--------+                             v
    |  (compaction)                   ...
    v                              +--------+
+--------+                         | Ln     | (largest level)
| L2 SST |                         +--------+
+--------+
    |
    v
   ...
```

The key insight: **writes go to memory first (fast), then flow downward through levels via compaction (background). Reads check from top to bottom, stopping at the first match.**

---

## 3. Internal Design

### 3.1 Write Path

A write in RocksDB takes exactly two steps:

1. **Append to WAL:** The key-value pair is appended to a write-ahead log file on disk. This is a sequential write (fast on any storage).

2. **Insert into MemTable:** The KV pair is inserted into an in-memory sorted data structure (usually a skip list). This is O(log n) in memory.

That's it. The write is complete. The client gets an acknowledgment. The actual SST file creation happens later, asynchronously.

```
PUT("user:1", "Alice"):
  1. WAL append:  [PUT, "user:1", "Alice"] -> sequential disk write
  2. MemTable insert: skiplist.insert("user:1", "Alice") -> memory operation
  3. Return OK to client
  
Total latency: ~10 us (SSD WAL write) + ~1 us (memory insert) = ~11 us
```

Compare this to InnoDB's write path: find the B+ tree leaf page, modify it in the buffer pool, write the redo log, potentially trigger a page split. InnoDB's write path touches more components.

### 3.2 MemTable and Flush

The MemTable is an in-memory sorted structure (skip list by default; can also be a hash skip list or vector). When it reaches `write_buffer_size` (default 64 MB), it becomes immutable and a new MemTable takes over.

The immutable MemTable is flushed to disk as an **SST file** (Sorted String Table) at Level 0. Each SST file is internally sorted and contains:

```
SST file structure:
  [Data blocks]     -- key-value pairs, sorted, compressed
  [Meta blocks]     -- Bloom filter, compression dictionary
  [Meta index]      -- offsets to meta blocks
  [Index block]     -- maps key ranges to data block offsets
  [Footer]          -- offsets to meta index and index block, magic number
```

Level 0 is special: SST files at L0 can have **overlapping key ranges** because each flush creates a new file independently. All other levels (L1, L2, ...) have **non-overlapping** key ranges within the level.

### 3.3 Compaction

Compaction is the process that merges SST files from one level into the next level. It's what converts the "log-structured" writes into organized, queryable data.

**Leveled compaction** (default):

```
L0: [a-z] [b-y] [c-x]     -- overlapping ranges (3 files)
     \      |      /
      \     |     /          Compact: merge overlapping L0 files
       \    |    /           with overlapping L1 files
        v   v   v
L1: [a-f] [g-m] [n-t] [u-z]  -- non-overlapping, sorted (4 files)
     \      /
      \    /                 Compact: merge L1 files with overlapping L2 files
       v  v
L2: [a-c] [d-f] [g-i] ... [x-z]  -- 10x larger than L1
```

Each level is ~10x larger than the previous (`max_bytes_for_level_multiplier = 10`). With 64 MB L1 and a multiplier of 10:

| Level | Max Size | Key Range |
|-------|----------|-----------|
| L0 | ~256 MB (4 files) | Overlapping |
| L1 | 256 MB | Non-overlapping |
| L2 | 2.56 GB | Non-overlapping |
| L3 | 25.6 GB | Non-overlapping |
| L4 | 256 GB | Non-overlapping |
| L5 | 2.56 TB | Non-overlapping |

**Why compaction is necessary:**

Without compaction, every read would have to check every SST file at every level. Compaction reduces the number of files and eliminates duplicate/deleted keys. It's the tax that LSM trees pay for fast writes.

### 3.4 Read Path

A read checks levels top-to-bottom:

1. **MemTable** (active): O(log n) skip list search
2. **Immutable MemTable** (if exists): same
3. **L0 SST files**: must check ALL files (overlapping ranges). Uses Bloom filters to skip files that definitely don't contain the key.
4. **L1-Ln SST files**: binary search on the sorted, non-overlapping files. Only one file per level can contain the key.

```
GET("user:42"):
  MemTable:        not found
  Immutable MT:    not found
  L0 file 1:      Bloom filter says NO -> skip
  L0 file 2:      Bloom filter says MAYBE -> check data blocks -> not found
  L0 file 3:      Bloom filter says NO -> skip
  L1:              binary search -> file "users_00042.sst" -> check -> FOUND
  Return value
```

**Bloom filters** are critical for read performance. A Bloom filter is a probabilistic data structure that can definitively say "key is NOT in this file" or "key MIGHT be in this file." False positive rate is configurable (default ~1%). With Bloom filters, a point lookup typically touches only 1-2 SST files instead of dozens.

### 3.5 WAL

RocksDB's WAL is simpler than PostgreSQL's. It's a pure append-only log of put/delete operations. On crash recovery:

1. Find the latest WAL file.
2. Replay all entries into a new MemTable.
3. The MemTable now contains all writes that were in memory at crash time.
4. Resume normal operation.

SST files on disk are immutable and always consistent. Only the MemTable (in-memory) can be lost on crash, and the WAL protects against that.

---

## 4. Design Trade-Offs

### The Three Amplification Factors

LSM trees trade between three types of amplification:

| Factor | Definition | LSM (RocksDB) | B-tree (InnoDB/PG) |
|--------|-----------|----------------|---------------------|
| Write amplification | Bytes written to storage / bytes written by app | 10-30x (compaction rewrites data multiple times) | 2-3x (in-place update + WAL) |
| Read amplification | I/Os per read | 1-3 (with Bloom filters) | 1-2 (B-tree traversal) |
| Space amplification | Storage used / logical data size | 1.1-1.5x (temporary during compaction) | 1.5-3x (PG: dead tuples; InnoDB: fragmentation) |

**The fundamental trade-off:** RocksDB accepts high write amplification (from compaction) to get extremely fast writes and low space amplification. B-trees accept higher read costs (random I/O for writes) to get low write amplification.

### Compaction Strategy Trade-offs

| Strategy | Write Amp | Read Amp | Space Amp | Best For |
|----------|-----------|----------|-----------|----------|
| Leveled (default) | High (10-30x) | Low (1-2 reads) | Low (1.1x) | Read-heavy workloads |
| Universal (tiered) | Low (2-4x) | High (many files to check) | Higher (2x) | Write-heavy workloads |
| FIFO | None | Very high | Very high | TTL data (time series) |

---

## 5. Experiments / Observations

### Write amplification measurement

Using `db_bench` (RocksDB's built-in benchmark):

```bash
./db_bench --benchmarks=fillrandom --num=1000000 --value_size=1000 \
  --write_buffer_size=67108864 --statistics
```

Results (leveled compaction, 1M keys, 1 KB values):
```
Data written by application:    1,000 MB  (1M * 1 KB)
Data written to storage:       12,400 MB  (compaction I/O from STATISTICS)
Write amplification:           12.4x

Breakdown:
  L0 -> L1 compaction:          2,800 MB
  L1 -> L2 compaction:          4,600 MB
  L2 -> L3 compaction:          5,000 MB
```

Each key-value pair is written ~12 times on average: once to WAL, once to L0, then rewritten during each compaction pass as it moves through levels.

### Read performance with/without Bloom filters

```bash
# With Bloom filters (10 bits per key, ~1% FP rate):
./db_bench --benchmarks=readrandom --num=1000000 --bloom_bits=10
  readrandom: 185,234 ops/sec

# Without Bloom filters:
./db_bench --benchmarks=readrandom --num=1000000 --bloom_bits=0
  readrandom: 42,567 ops/sec
```

Bloom filters improve random read throughput by **4.3x**. Without them, every read must check data blocks in multiple SST files. With them, most files are skipped after a single memory lookup in the filter.

### Space amplification during compaction

```
Before compaction: L1 has 256 MB, L2 has 2.56 GB
During compaction: L1 files being merged + new L2 files being written
  Temporary space: original L2 files + new L2 files = ~2x for the affected range
After compaction:  old L2 files deleted, space returns to ~1.1x

Peak space amplification during L2 compaction: 1.5x
Steady-state space amplification: 1.1x
```

### Compaction strategy comparison

```
Workload: 10M random writes, 1 KB values, followed by 1M random reads

Leveled compaction:
  Write throughput:  85,000 ops/sec
  Read throughput:  190,000 ops/sec
  Space amplification: 1.11x

Universal compaction:
  Write throughput: 150,000 ops/sec  (+76% vs leveled)
  Read throughput:  120,000 ops/sec  (-37% vs leveled)
  Space amplification: 1.8x
```

Universal compaction nearly doubles write speed but reads suffer because there are more overlapping files to check. The choice depends entirely on the workload.

---

## 6. Key Learnings

1. **LSM trees optimize for write throughput by deferring work.** Writes are fast because they only touch memory and a sequential WAL append. The expensive work (sorting, merging, organizing) happens later during compaction. This is a fundamentally different philosophy from B-trees, which do the organizational work at write time.

2. **Compaction is the hidden cost.** RocksDB's write amplification of 10-30x means that for every byte the application writes, the storage device writes 10-30 bytes. On SSDs, this directly impacts device lifetime. Tuning compaction is the primary performance lever.

3. **Bloom filters are not optional.** Without Bloom filters, RocksDB's read performance drops dramatically. A 10 bits-per-key Bloom filter uses ~1.25 bytes per key and eliminates 99% of unnecessary SST file reads.

4. **There is no single best compaction strategy.** Leveled compaction is best for read-heavy workloads (low read amplification). Universal is best for write-heavy workloads (low write amplification). FIFO is best for time-series data that expires. The engineer must choose based on the workload.

5. **LSM trees complement B-trees, they don't replace them.** Facebook uses RocksDB (LSM) for their social graph storage (write-heavy) and MySQL/InnoDB (B-tree) for their ads database (read-heavy, complex queries). The right engine depends on the access pattern.

---

## References

- RocksDB documentation: https://github.com/facebook/rocksdb/wiki
- O'Neil, P. et al. "The Log-Structured Merge-Tree (LSM-Tree)" (1996)
- Dong, S. et al. "Optimizing Space Amplification in RocksDB" (CIDR 2017)
- Facebook Engineering: "MyRocks: A space- and write-optimized MySQL database" (2016)
- RocksDB Tuning Guide: https://github.com/facebook/rocksdb/wiki/RocksDB-Tuning-Guide
