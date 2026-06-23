# RocksDB Architecture

> Advanced DBMS — System Design Discussion  
> Roll No: 10075 | Nase Anishka

---

## 1. Problem Background

RocksDB came out of Facebook in 2012, forked from Google's LevelDB. The motivation was simple: at Facebook's scale, **write throughput was the bottleneck**, not reads. Traditional databases like InnoDB and PostgreSQL are built around B-trees, and B-trees have a problem with random writes at scale.

When you write to a B-tree, you have to update the page that contains that key — which means reading the right page from disk, modifying it, and writing it back. For randomly distributed keys, this means random disk I/O. On HDDs, random I/O is slow because of seek time. On SSDs, random writes are still problematic because each write erases and rewrites an entire flash block — doing millions of random writes accelerates SSD wear.

The core insight behind RocksDB (and the underlying **LSM-tree** data structure): **turn all disk writes into sequential writes**. Sequential writes are always faster than random writes, on any storage medium. If you can batch up writes in memory and flush them to disk sequentially, your write throughput goes way up.

RocksDB implements the **Log-Structured Merge-Tree (LSM-tree)**, originally proposed in a 1996 paper by O'Neil et al. It's the storage engine behind CockroachDB, TiDB's TiKV, and MyRocks (MySQL with an LSM backend). Understanding it means understanding why write-optimized storage works, and what you give up to get there.

---

## 2. Architecture Overview

```
  WRITE PATH                           READ PATH
  ──────────────────────────────────────────────────────────
  
  PUT(key, val)                        GET(key)
      |                                    |
      v                                    v
  +-------+                           Check in this order:
  |  WAL  |  (append to log first,
  | (disk)|   for crash recovery)      1. Active MemTable
  +-------+                            2. Immutable MemTable(s)
      |                                3. L0 SSTables (may overlap)
      v                                4. L1, L2, … Ln SSTables
  +-----------+                           (sorted, non-overlapping per level)
  |  Active   |--- fills up --->       
  | MemTable  |   +-------------+     Bloom filter checked before
  | (in-mem   |   | Immutable   |     each SSTable disk read.
  | skip list)|   | MemTable    |
  +-----------+   +------+------+
                         |
                     flush to disk
                         |
                         v
                    +----------+
                    |  L0      |  SSTable files (may have overlapping keys)
                    +----------+
                    |  L1      |  sorted, no overlap within level
                    +----------+
                    |  L2      |
                    +----------+
                    |  ...     |
                    +----------+
                    |  Ln      |  largest level
                    +----------+
                    
  Compaction threads continuously merge SSTables from level i into level i+1
```

The flow is: every write first goes to the WAL (sequential, crash-safe), then to the in-memory MemTable. When the MemTable gets full, it's frozen (immutable) and a new one starts. A background thread flushes the immutable MemTable to disk as a sorted file (SSTable) at L0. Compaction threads then merge files from L0 downward.

Reads check from newest to oldest: MemTable first (most recent data), then L0, then L1, L2, etc. This works because the most recent version of a key is always in the highest-priority location.

---

## 3. Internal Design

### MemTable

The MemTable is the in-memory write buffer. RocksDB uses a **skip list** by default — it's sorted by key, so range scans work without hitting disk. Writes go here after the WAL.

When the MemTable reaches its size limit (`write_buffer_size`, default 64 MB), it becomes immutable and a new active MemTable opens. Multiple immutable MemTables can queue up if the flush thread is slower than incoming writes.

If too many immutable MemTables pile up, RocksDB stalls or stops incoming writes. This is deliberate backpressure — not a crash.

### WAL

The WAL is a sequential append-only log. Its only job is crash recovery: if the process dies before the MemTable is flushed to disk, the WAL replays the lost writes on startup.

Once a MemTable is successfully flushed, the corresponding WAL entries are obsolete and eventually deleted.

RocksDB supports **WriteBatch** — writing multiple key-value pairs atomically. Either all of them appear after a crash or none of them do.

### SSTable Format

An SSTable is an immutable sorted file. Once written, it is never modified. New SSTables are created (by flushes or compaction) and old ones are deleted — but no existing file is ever changed in place.

```
  SSTable structure:
  +----------------------------------------------+
  | Data Blocks (4KB each, compressed by default) |
  |   key-value pairs, sorted, no cross refs       |
  |                                                |
  | Index Block                                    |
  |   one entry per data block: (last key, offset) |
  |   binary-search to find the right data block   |
  |                                                |
  | Bloom Filter Block                             |
  |   membership test: "is this key in this file?"|
  |   ~1% false positive rate (10 bits/key)        |
  |                                                |
  | Footer (offsets to index + filter, magic)      |
  +----------------------------------------------+
```

The **Bloom filter** is the key optimization for reads. Before doing any disk I/O on an SSTable, RocksDB checks the bloom filter. If it says "definitely not here," skip the file entirely. If it says "maybe here," do the actual lookup. With a 10-bits-per-key filter, 99% of "key not found" queries skip that file with no disk read.

### Levels and Compaction

**L0** receives SSTable files directly from MemTable flushes. These files can have **overlapping key ranges** because each was flushed from a separate MemTable at a different point in time. Too many L0 files → reads have to check all of them → read amplification grows. By default, compaction triggers at 4 L0 files.

**L1 and beyond** have a key property: **no two SSTables at the same level have overlapping key ranges**. A read at level 1+ only needs to check at most one file. Each level has a size limit that grows by 10× per level (default: 256 MB at L1, 2.5 GB at L2, etc.).

**Compaction** is the background process that merges files from level i into level i+1. It's essentially a merge sort — it reads SSTables from both levels, merges them by key, and writes new SSTables at the lower level. Old files are deleted.

Two important things happen during compaction:
1. **Deduplication** — if the same key appears in multiple files, only the newest version survives
2. **Tombstone cleanup** — a DELETE operation writes a "tombstone" entry. When a tombstone is merged down to the final level and there's nothing older beneath it, both the tombstone and the original key are removed permanently

Compaction strategies:
- **Leveled** (default): strictly enforces size per level, compacts overlapping ranges aggressively → good read performance, higher write amplification
- **Universal**: merges files of similar sizes together → lower write amplification, more space used
- **FIFO**: just drops the oldest files when the size limit is hit → only useful for time-series data where you don't need old data

### The Three Amplification Factors

Every LSM-tree design balances three costs:

| | Write Amplification | Read Amplification | Space Amplification |
|--|--|--|--|
| Definition | Bytes written to disk / bytes the app wrote | Bytes read from disk / bytes returned | Disk used / live data size |
| High leveled compaction | High (keys rewritten each level) | Low (one file per level) | Low |
| High universal compaction | Low | Higher (more files) | Higher |

There's no free lunch. Improving one amplification factor makes another worse. Choosing a compaction strategy is choosing which cost you're willing to pay based on your workload.

---

## 4. Design Trade-Offs

**Why LSM-trees are so fast for writes.**  
Every write is a sequential append — WAL, then MemTable, then sequential flush to L0. There is no random I/O in the write path at all. For workloads with massive write throughput (social feeds, event streams, metrics), this is a huge win over B-trees where each write potentially touches a different disk page.

**The read cost.**  
A B-tree can answer a point lookup by walking one tree from root to leaf — O(log N). RocksDB might have to check the MemTable, several L0 files, then one file per level down to where the key lives. Bloom filters help enormously for keys that don't exist, but for keys that do exist deep in the level hierarchy, there's real read amplification. This is the fundamental trade-off of LSM-trees.

**Compaction is always running and always consuming resources.**  
Compaction is not optional — without it, L0 files pile up and read performance degrades, and stale/deleted data takes up space forever. Under heavy write load, compaction can use 50–70% of your disk I/O bandwidth. If writes outrun compaction, RocksDB throttles and then stalls writes to let compaction catch up. These write stalls are the hardest operational challenge.

**Deletes don't free space immediately.**  
A DELETE writes a tombstone entry. The space isn't reclaimed until that tombstone is merged all the way down to the bottom level during compaction. In delete-heavy workloads, tombstones accumulate and can cause significant space amplification and read slowdowns before compaction processes them.

**Write stalls cause latency spikes.**  
Most writes land in the MemTable and return quickly — very low latency. But occasionally a write arrives during a compaction stall and sees much higher latency. This means p99 latency for RocksDB can be worse than a B-tree database even when the p50 is much better. If tail latency matters, this needs careful tuning.

---

## 5. Experiments / Observations

**Random writes vs B-tree — the core advantage.**  
Using `db_bench --benchmarks=fillrandom`, RocksDB typically achieves 400–500K random writes per second on a modern SSD. The equivalent random-key INSERT workload in PostgreSQL is usually 5–10× lower because each insert hits a random B-tree page. The difference is all sequential vs. random I/O.

**Write amplification is measurable.**  
With `--statistics` enabled, `db_bench` reports `rocksdb.bytes.written` (logical writes) and the actual bytes written to disk across all compaction. For leveled compaction, write amplification of 10–30× is typical — writing 1 GB of data causes 10–30 GB of disk writes. The LSM pays for sequential writes with this background compaction cost.

**Bloom filters for negative lookups.**  
On a large database, a GET for a key that doesn't exist without bloom filters hits the disk once per level. With a 10-bits-per-key bloom filter (~1% FPR), 99% of those disk reads are avoided. On a 6-level database, this reduces average disk reads from ~6 to ~0.06 per negative lookup. This is the single most impactful read optimization.

**L0 file accumulation under write bursts.**  
During a write burst where compaction can't keep up, L0 file count climbs. At 20 files, RocksDB throttles writes. At 36 files, it stops. Monitoring L0 file count is one of the most important health indicators in production RocksDB deployments.

**Tombstone accumulation in delete-heavy workloads.**  
In a workload mixing inserts and deletes, disk usage keeps growing even when the number of live keys stays constant. The deleted keys' tombstones stay on disk until compacted all the way to the bottom level. Space amplification of 3–5× live data size is possible in extreme cases.

---

## 6. Key Learnings

1. **LSM-trees trade read complexity for write simplicity.** All writes are sequential — that's the entire point. The read path is more complex (multiple places to check) and bloom filters are the primary tool to make reads practical. Understanding the write path is simple; understanding why reads are still acceptable is where the real design work is.

2. **Compaction is the engine's metabolism.** RocksDB doesn't just write data — it continuously reorganizes it in the background. Without compaction, the engine degrades. Compaction is not cleanup code; it's as central to the design as the MemTable or the WAL.

3. **Immutability makes everything else simpler.** SSTables are write-once and never modified. This means concurrent reads need no locking (the file won't change), cached data never goes stale, and replication is as simple as copying files. The append-only design that makes writes fast also makes the rest of the system simpler.

4. **Bloom filters are what make RocksDB practical for point reads.** Without them, a GET for a missing key would require checking disk at every level. With them, 99% of such checks are answered from a few kilobytes of in-memory filter data. The bloom filter is the bridge between LSM-tree's write-optimized structure and acceptable read performance.

5. **The three amplification factors are a trilemma.** Write amplification, read amplification, and space amplification — you can minimize two at the expense of the third. Leveled compaction picks low read + space at the cost of write amplification. Universal picks low write at the cost of space. There's no option that's cheapest on all three — picking a compaction strategy means picking your workload's priority.

---

## References

- RocksDB Wiki: [Overview](https://github.com/facebook/rocksdb/wiki/RocksDB-Overview), [Compaction](https://github.com/facebook/rocksdb/wiki/Compaction), [Bloom Filters](https://github.com/facebook/rocksdb/wiki/RocksDB-Bloom-Filter), [Leveled Compaction](https://github.com/facebook/rocksdb/wiki/Leveled-Compaction)
- O'Neil, P. et al. (1996). *The Log-Structured Merge-Tree.* Acta Informatica.
- Dong, S. et al. (2017). *Optimizing Space Amplification in RocksDB.* CIDR.
