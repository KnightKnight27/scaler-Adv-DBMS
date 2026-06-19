# RocksDB Architecture

## 1. Problem Background

### Why RocksDB Was Created

RocksDB was created at Facebook in 2012 as a fork of Google's LevelDB. The immediate engineering problem was straightforward but hard: Facebook's serving infrastructure was generating write workloads that traditional B-tree storage engines — specifically InnoDB — could not sustain efficiently. These were embedded storage workloads for individual server processes (not shared database servers), handling hundreds of thousands of writes per second per node, with SSD as the target medium.

LevelDB was the closest existing solution: an LSM-tree engine designed at Google in 2011 to address the same write-throughput problem. But LevelDB was single-threaded and had limited configurability. Facebook took the core LSM-tree idea and rebuilt the operational surface: multi-threaded compaction, tunable compaction styles, column families, rate limiting, statistics, and extensive support for SSD I/O characteristics. The result was RocksDB — a high-performance, embeddable, write-optimized key-value storage engine used in production at Facebook, LinkedIn, Yahoo, and as the storage backend for databases like CockroachDB, TiKV, and MyRocks (MySQL + RocksDB).

### Why LSM-Tree Databases Exist

The fundamental problem is the mismatch between how writes arrive and how storage media works. Writes in real systems arrive at unpredictable keys. A B-tree must insert each key at its sorted position in the tree, which means random writes to disk pages. On HDDs, a random write is ~100–200x slower than a sequential write due to seek time. Even on SSDs, random writes cause write amplification through the SSD's own flash translation layer, reducing device lifetime and sustained throughput.

The Log-Structured Merge tree (LSM tree), introduced by O'Neil et al. in 1996, converts random writes into sequential writes by buffering all incoming writes in memory and periodically flushing sorted, immutable files to disk. All disk writes are sequential. The cost — reorganizing data over time through a process called compaction — is paid in the background, amortized across many writes, and controlled to avoid interfering with the foreground read/write path.

### Problems with Traditional B-Tree Databases

| Problem | B-Tree Consequence | LSM Solution |
|---|---|---|
| Random write I/O | Each key insert may touch a different page; random I/O on spinning disks, SSD wear on flash | All disk writes are sequential (flush and compaction) |
| Write amplification at high fanout | A page split cascades up the tree; multiple pages written per logical write | Writes go to the sequential WAL + MemTable; page-level reorganization deferred |
| Update-in-place on SSD | SSD cannot overwrite in place; FTL performs read-modify-write internally | LSM never overwrites; new versions are written as new SSTable entries |
| Read-optimized at the cost of writes | B-tree is tuned for point reads; random write throughput suffers | LSM trades read performance to maximize write throughput |

B-trees are a fundamentally read-optimized structure. The reason most OLTP databases default to B-trees is that OLTP workloads are traditionally read-heavy: many reads per write. For workloads where writes dominate — time-series ingestion, event logging, change data capture, feature stores, message queue backends — the B-tree's random-write cost becomes the dominant bottleneck.

---

## 2. Architecture Overview

### High-Level Architecture

```
  Client Writes                      Client Reads
       │                                  │
       ▼                                  ▼
+----------------------------------------------+
|               RocksDB Instance               |
|                                              |
|  ┌──────────┐   ┌──────────────────────────┐ |
|  │   WAL    │   │       MemTable           │ |
|  │(append-  │◄──│  (SkipList, in-memory,   │ |
|  │ only log)│   │   mutable, sorted)       │ |
|  └──────────┘   └──────────────────────────┘ |
|                          │ (flush when full)  |
|               ┌──────────▼──────────────────┐ |
|               │   Immutable MemTable(s)     │ |
|               │   (read-only, awaiting      │ |
|               │    SSTable flush)           │ |
|               └──────────┬──────────────────┘ |
|                          │ (background flush) |
|  ┌───────────────────────▼────────────────┐  |
|  │              SSTable Files             │  |
|  │                                        │  |
|  │  L0: [sst_00001] [sst_00002] ...       │  |
|  │       (may overlap in key space)       │  |
|  │                                        │  |
|  │  L1: [   key range A   ] [key range B] │  |
|  │       (non-overlapping, sorted)        │  |
|  │                                        │  |
|  │  L2: [  ....larger, non-overlapping .. │  |
|  │                                        │  |
|  │  Ln: [  ....largest level  ........... │  |
|  └────────────────────────────────────────┘  |
+----------------------------------------------+
```

### Write Path

```
Client: Put(key, value)
    │
    ├─ 1. Append to WAL (sequential write, fsync for durability)
    │
    ├─ 2. Insert into MemTable (in-memory SkipList, O(log n))
    │
    └─ Return success to client
    
    [MemTable reaches size threshold (default 64MB)]
    │
    ├─ 3. MemTable becomes Immutable MemTable (read-only)
    │      A new empty MemTable is created for incoming writes
    │
    └─ 4. Background flush thread writes Immutable MemTable
           to a new SSTable file in L0 (sequential disk write)
           WAL entries covered by this flush can be discarded

    [L0 SSTable count reaches threshold]
    │
    └─ 5. Compaction: L0 SSTables merged + sorted into L1
           (and L1 into L2, etc., as levels fill up)
```

### Read Path

```
Client: Get(key)
    │
    ├─ 1. Check active MemTable    (exact key lookup, O(log n) SkipList)
    │         → if found and not deleted: return value
    │
    ├─ 2. Check Immutable MemTable(s)  (newest first)
    │         → if found: return value
    │
    ├─ 3. Check L0 SSTables  (newest file first, files may overlap)
    │      For each L0 file:
    │         a. Check Bloom filter  → if definitely absent, skip file
    │         b. Check index block   → find approximate offset in data block
    │         c. Read data block     → confirm key or miss
    │
    ├─ 4. Check L1 SSTables  (binary search over file key ranges,
    │      only ONE file can contain the key — ranges non-overlapping)
    │         a. Bloom filter → index block → data block
    │
    └─ 5. Repeat for L2, L3, ... Ln until key found or all levels exhausted
              → not found: return key-not-exists
```

The worst-case read touches the MemTable, all immutable MemTables, all L0 files, and one file per level from L1 through Ln. Bloom filters short-circuit most of these checks, making the average case far better than worst case.

---

## 3. Internal Design

### MemTable

The MemTable is an in-memory, mutable, sorted data structure that absorbs all incoming writes before anything touches disk. Every `Put(key, value)` and `Delete(key)` goes here first.

RocksDB's default MemTable implementation is a **SkipList**: a probabilistic data structure that maintains keys in sorted order with O(log n) insert, delete, and lookup. The sorted order is what makes the eventual flush to a sorted SSTable efficient — the MemTable can be scanned in key order without a sort step.

RocksDB also supports alternative MemTable implementations: a hash-based structure for point-lookup-heavy workloads, and a vector structure for bulk-load scenarios where in-order inserts make the sorted property unnecessary to maintain incrementally. The pluggability reflects a general RocksDB design principle: the default is tuned for general workloads, but workload-specific overrides exist for most components.

**Writes never wait for disk I/O** (assuming WAL is the durability mechanism, not synchronous MemTable flush). A write is acknowledged as soon as the WAL is flushed and the MemTable is updated. Disk bandwidth is consumed entirely by background flush and compaction threads, not by the foreground write path.

### Immutable MemTable

When the active MemTable reaches its size threshold (`write_buffer_size`, default 64MB), it is atomically converted to an **Immutable MemTable** — a read-only snapshot of the sorted write buffer. A new empty MemTable becomes active immediately. Writes continue without interruption.

A background flush thread picks up Immutable MemTables and writes each one to a new SSTable file in L0. The Immutable MemTable is discarded once its SSTable is written and its WAL entries are no longer needed for recovery. Multiple Immutable MemTables can accumulate if flush cannot keep up with the write rate — RocksDB will stall or stop writes if too many accumulate (`max_write_buffer_number` limit), providing natural backpressure.

### SSTables

An SSTable (Sorted String Table) is an immutable, sorted file on disk. Once written, it is never modified — only replaced by compaction output. This immutability is central to the LSM design: there is no random write to an existing file, eliminating random I/O from the write path entirely.

#### SSTable File Format

```
┌─────────────────────────────────────────┐
│              Data Blocks                │
│  [key1|val1][key2|val2]...[keyN|valN]   │  ← sorted key-value pairs
│  [key1|val1]...                         │  ← next block
│  ...                                    │
├─────────────────────────────────────────┤
│              Meta Blocks                │
│  - Filter block   (Bloom filter data)   │
│  - Stats block    (key count, etc.)     │
├─────────────────────────────────────────┤
│              Index Block                │
│  One entry per data block:              │
│  [last_key_in_block → block_offset]     │  ← allows binary search to data block
├─────────────────────────────────────────┤
│              Footer                     │
│  Offset of index block                  │
│  Offset of meta block                   │
│  Magic number                           │
└─────────────────────────────────────────┘
```

Data blocks (default 4KB) hold key-value pairs in sorted key order. The index block is a sparse index over data blocks — to find a key, binary-search the index block to find which data block might contain it, then read only that data block. This two-level structure means reads touch at most two on-disk blocks per SSTable (index + data), keeping I/O bounded even for large files.

Keys within data blocks are prefix-compressed: if consecutive keys share a long prefix (common in namespace-prefixed keys), only the differing suffix is stored after the first full key. This reduces SSTable size significantly for structured key namespaces.

### WAL

The Write-Ahead Log is an append-only file that records every write operation before it reaches the MemTable. On crash, the MemTable state (which was in memory) is lost. WAL replay reconstructs the MemTable to the state it was in before the crash.

Each write appends a record to the WAL: `[sequence_number | type (Put/Delete) | key | value]`. The sequence number is RocksDB's global logical clock — it orders all writes across the database and is used in MVCC-style snapshot reads.

WAL records are grouped into 32KB blocks for alignment. The WAL for a given MemTable is retained until the MemTable has been flushed to an SSTable — at that point, the WAL segment is no longer needed for recovery because the data is durably in the SSTable.

`sync_options` controls WAL durability. `sync=true` calls `fsync` after every write — full durability, higher latency. `sync=false` relies on OS-level buffering — higher throughput, risk of losing the last ~few writes on OS crash. A middle ground (`wal_bytes_per_sync`) syncs periodically rather than per-write.

### LSM Tree Levels

#### L0 — The Landing Zone

L0 receives SSTables directly from MemTable flushes. L0 is fundamentally different from all other levels: **SSTable key ranges in L0 can overlap**. Two L0 files may both contain entries for the same key (different versions). This is unavoidable because each flush produces a sorted file of the MemTable's current key set, and consecutive MemTables are not globally sorted relative to each other.

The consequence: a read that reaches L0 must check every L0 file (in recency order) until the key is found or all files are exhausted. Each check involves a Bloom filter, then possibly a data block read. L0 is the most expensive level to read when many files have accumulated.

L0 file count is kept small via compaction. When L0 reaches a configurable threshold (default 4 files), compaction merges all L0 files with overlapping L1 files, producing new non-overlapping L1 files.

#### L1 and Higher — Sorted, Non-Overlapping Levels

From L1 onward, files within a level have **non-overlapping key ranges**. A key can exist in at most one file per level (ignoring the case of multiple versions across levels). This means a read at L1 or deeper requires checking exactly one file per level — a binary search over file key ranges identifies the single candidate file, and then a Bloom filter + index + data block lookup confirms presence or absence.

Each level is a fixed size multiple larger than the previous (typically 10x by default): L1=256MB, L2=2.56GB, L3=25.6GB, etc. Data flows downward: L0→L1→L2→...→Ln as higher levels fill up.

```
Level capacity (default multiplier = 10):
  L0:  ~4 files (file count trigger, not size)
  L1:  256 MB
  L2:  2.56 GB
  L3:  25.6 GB
  L4:  256 GB
  ...
```

The last level (Ln) holds the vast majority of data. A typical production RocksDB instance might have 95%+ of its data in the bottom two levels.

### Compaction

Compaction is the process of merging SSTables from one level into the next, eliminating redundant versions of keys and reclaiming space from deleted entries. Without compaction, reads would have to search an ever-growing collection of L0 files, and deleted or overwritten keys would occupy space forever.

#### Why Compaction Exists

Each time a key is updated, a new entry is appended to the write path. The old value is not removed — it is simply superseded by the newer entry. Similarly, a `Delete(key)` writes a **tombstone** record (a marker indicating the key is deleted), not a removal of the old value. Compaction is the only mechanism that physically removes superseded versions and tombstones.

#### Minor Compaction (L0 → L1)

When L0 reaches the file count threshold, the compaction process:
1. Takes all L0 SSTables.
2. Finds all L1 SSTables whose key ranges overlap with the L0 files.
3. Merges all of them in a k-way merge (all inputs are individually sorted), producing new non-overlapping L1 SSTables.
4. Deletes the input L0 and L1 files; atomically makes the new L1 files visible.

The merge is a standard sorted merge — each input file is iterated in key order, and the outputs are written sequentially. During the merge, if the same key appears in multiple input files, the version with the highest sequence number is kept (the most recent write); older versions are dropped.

#### Major Compaction (Ln → Ln+1)

When a level exceeds its size target, compaction picks one SSTable from that level (the one most likely to have accumulated stale versions, by heuristic) and merges it with all overlapping SSTables in the next level.

```
L1 compaction example:

  L1: [A–F] [G–M] [N–Z]           ← [G–M] selected for compaction
                                       (overlaps with L2 files below)
  L2: [A–C] [D–H] [I–N] [O–Z]

  Merge inputs: L1[G–M] + L2[D–H] + L2[I–N]

  Output (new L2 files, non-overlapping):
       [D–G] [H–J] [K–N]          ← replaces old L2[D–H] and L2[I–N]

  L1[G–M] is deleted.
```

This incremental, single-file compaction keeps compaction I/O bounded per compaction job, allowing background compaction to run concurrently with foreground reads and writes without long stalls.

#### Compaction Styles

RocksDB supports multiple compaction strategies:
- **Leveled compaction** (default): strict level size limits; minimizes space amplification; higher write amplification.
- **Universal compaction**: merges all SSTables sorted by recency; minimizes write amplification; higher space amplification (up to 2x during merge).
- **FIFO compaction**: for time-series data; deletes oldest files when total size exceeds a limit; minimal compaction overhead; no merging.

### Bloom Filters

A Bloom filter is a probabilistic data structure that answers membership queries: "is key K in this SSTable?" It can answer definitively "No" (key is definitely absent) or probabilistically "Maybe" (key might be present). It never produces false negatives — if the filter says No, the SSTable can be skipped entirely.

#### Structure

A Bloom filter is a bit array of size `m` and `k` hash functions. To add a key, compute `k` hash values and set those `k` bit positions to 1. To query a key, compute the same `k` hashes and check if all `k` bit positions are 1. If any bit is 0, the key is definitely absent. If all are 1, the key is probably present (with a false positive rate that depends on `m`, `k`, and the number of keys in the filter).

RocksDB uses approximately 10 bits per key by default, giving a false positive rate of roughly 1%. That means 1 in 100 SSTable files that the Bloom filter says "maybe" will actually not contain the key, requiring a wasted data block read. Increasing bits-per-key reduces the false positive rate at the cost of larger filter blocks.

#### Impact on Read Performance

Without Bloom filters, a point read that misses the MemTable must perform an index + data block read on every SSTable at every level — potentially dozens of files. With Bloom filters, most of those files are eliminated with a single bit-array check (a memory operation, since filter blocks are cached). For a key that does not exist in the database, Bloom filters eliminate nearly all disk I/O — each level's Bloom check says "No" and the search terminates without reading a single data block.

For a key that does exist (and is in the bottom level, Ln), the read path is: Bloom miss at MemTable, Bloom miss at L0, Bloom miss at L1, ... Bloom hit (or false positive + miss) at Ln → data block read at Ln. The total disk reads are bounded by the number of levels, not the number of SSTables.

---

## 4. Design Trade-Offs

### The Three Amplification Factors

All LSM-tree design decisions reduce to managing three competing amplification factors:

| Factor | Definition | Who Pays | RocksDB Default |
|---|---|---|---|
| **Write Amplification (WA)** | Bytes written to disk / bytes written by application | Storage device lifetime, I/O bandwidth | ~10–30x with leveled compaction |
| **Read Amplification (RA)** | Disk reads per logical read | Read latency, I/O bandwidth | ~5–20x without Bloom filters; ~1–3x with |
| **Space Amplification (SA)** | Bytes on disk / bytes of live data | Storage cost | ~1.1–2x with leveled; up to 2x during universal compaction |

These three factors are in a three-way tension. Optimizing one almost always worsens one or both of the others.

### Write Amplification

Write amplification in an LSM tree is caused by compaction. Each key written once by the application may be rewritten multiple times as it moves from L0 → L1 → L2 → ... → Ln. In leveled compaction, a key written at L0 is rewritten during L0→L1 compaction, then again during L1→L2 compaction, and so on. The total WA is roughly proportional to the number of levels times the level size ratio.

For a 10x size ratio and 6 levels: theoretical WA ≈ 10 × 6 = 60. In practice it is lower because not all keys are compacted equally and the multiplier is applied per-level, not uniformly. Empirically, leveled compaction produces WA in the range of 10–30x for general workloads.

**Why it is accepted:** Even at 30x WA, sequential writes are still faster than random writes. On an NVMe SSD with 3GB/s sequential write throughput, 30x WA means the application can sustain 100MB/s of logical writes while consuming the device's full sequential bandwidth. A B-tree sustaining the same application write rate might produce fewer total bytes written but at random I/O patterns that consume I/O capacity far less efficiently.

### Read Amplification

A point read may have to consult the MemTable, immutable MemTables, all L0 SSTables, and one file per level from L1 to Ln. Without Bloom filters, each SSTable check requires at minimum one index block read and potentially one data block read. With Bloom filters, most checks are eliminated with an in-memory bit-array operation.

The residual read amplification after Bloom filtering is roughly one data block read at the level where the key is found, plus the Bloom filter checks at all shallower levels. For a key in Ln, the expected I/O is ~1 data block read (at Ln) plus negligible memory operations at all other levels.

**The worst case for reads** is range scans: Bloom filters do not help with range queries. A range scan must merge results from the MemTable, all immutable MemTables, all L0 SSTables, and the relevant key range across all levels. More levels = more merge inputs = higher range scan cost.

### Space Amplification

Leveled compaction keeps space amplification low (~10%) because at any given time, the only extra space consumed is by compaction input files that haven't been deleted yet and by multiple versions of recently-written keys. The bottom level holds most of the data; upper levels are small. 

Universal compaction can transiently use 2x space during a full merge, because all existing data plus the new compacted output coexist on disk until the merge completes. This makes leveled compaction the right choice when storage is constrained and universal compaction better for workloads where write throughput is the overriding concern and storage is available.

---

## 5. Experiments / Observations

### Compaction Behavior Under Sustained Writes

Under a sustained write-only workload, observing RocksDB's behavior via its built-in statistics (`OPTIONS` file + `db_bench --stats_interval`) reveals a characteristic pattern:

- **Phase 1 (early)**: Writes are fast — MemTable absorbs all writes, only WAL goes to disk. Write latency is low and consistent.
- **Phase 2 (L0 accumulates)**: L0 SSTable count grows. Read latency begins increasing as more L0 files must be checked per read. Write latency remains low.
- **Phase 3 (L0 compaction triggered)**: Compaction starts. Write throughput may dip briefly as the system catches up. After compaction, L0 count drops, read latency recovers.
- **Phase 4 (steady state)**: Writes, flushes, and compaction run concurrently. Throughput stabilizes. `rocksdb.block.cache.hit` and `rocksdb.bloom.filter.useful` counters show the proportion of reads served from cache vs. disk, and the proportion of file checks eliminated by Bloom filters.

The key observation: **write latency spikes occur when the MemTable fills faster than the flush thread can drain it**, or when L0 compaction falls behind. Both are resource starvation scenarios (I/O or CPU for compaction), not algorithmic failures. Tuning `max_background_compactions` and `max_background_flushes` threads directly controls this.

### Bloom Filter Impact

Measuring `Get()` latency with and without Bloom filters enabled on a database where most reads miss (the key does not exist):

- **Without Bloom filters**: every Get touches index + data block in each SSTable at each level. Read IOPS scales with number of levels × number of L0 files.
- **With Bloom filters** (10 bits/key): the vast majority of Bloom checks return false, and the Get terminates without any data block reads. The Bloom filter blocks are cached in the block cache — the check is an in-memory operation.

The `rocksdb.bloom.filter.useful` statistic counts how many times a Bloom filter prevented a file read. On a negative-lookup-heavy workload (e.g., checking cache existence for uncached keys), this counter often represents 95%+ of all SSTable checks — meaning without Bloom filters, read I/O would be 20x higher.

### Compaction Write Amplification in Practice

Querying `rocksdb.compact.write.bytes` and `rocksdb.bytes.written` (application-level writes) at intervals gives the live write amplification ratio. For a mixed read-write workload with random keys across a large key space (uniform distribution, which maximizes overlap between compaction levels), write amplification is typically 15–25x with leveled compaction.

For a sequential key workload (monotonically increasing keys, as in time-series or auto-increment), write amplification is much lower (~3–5x) because each L0 flush covers a new key range that does not overlap with existing levels, reducing compaction merge work.

### WAL and Durability Trade-off

Toggling `sync=false` vs `sync=true` on the WAL write reveals the durability-throughput trade-off directly. With `sync=false`, the OS write buffer absorbs WAL writes and flushes them asynchronously. Throughput increases significantly (often 2–5x) because `fsync` latency (~1–10ms on SSD) is eliminated from the critical path. The exposure: if the OS crashes (not just the application), the last batch of writes since the last OS-initiated flush is lost. For applications where losing a few seconds of writes is acceptable (e.g., derived data that can be rebuilt), `sync=false` is a standard production setting.

---

## 6. Comparison with PostgreSQL and InnoDB

### B-Tree vs LSM Tree: Fundamental Difference

The core difference is **when data is organized**. B-trees organize data at write time — each insert finds the correct position in the sorted tree immediately, modifying pages in place. LSM trees defer organization — writes go to the MemTable in any order, and reorganization into sorted levels happens asynchronously via compaction.

B-tree: **pay for organization at write time** (random I/O per insert).  
LSM tree: **pay for organization at compaction time** (sequential I/O, amortized over many writes).

### Comparative Analysis

| Dimension | PostgreSQL (B-Tree) | InnoDB (B-Tree) | RocksDB (LSM) |
|---|---|---|---|
| **Write throughput** | Moderate; random heap + index I/O | Moderate; clustered B+ tree random insert | High; sequential WAL + MemTable; compaction in background |
| **Write latency** | Low for single writes; degrades under heavy random inserts | Low for PK inserts; degrades with UUID/random PKs | Consistently low (MemTable absorbs writes); occasional stalls during compaction |
| **Point read** | Fast (B-tree + heap fetch or index-only) | Fast (clustered index, 1 traversal) | Good with Bloom filters; worse on cache-cold data |
| **Range scan** | Efficient (seq scan or index range) | Efficient (clustered index range, sequential leaf reads) | Slower; must merge from MemTable + all levels |
| **Storage overhead** | Heap bloat from dead tuples; requires VACUUM | Low bloat; undo logs separate from data | Space amplification from multiple key versions; compaction reclaims space |
| **Write amplification** | Low per write (one heap insert + index inserts) | Low per write; page splits add overhead | High (10–30x from compaction) |
| **Read amplification** | Low (B-tree height = 3–5 for large tables) | Low (same) | Higher; depends on level count + Bloom efficiency |
| **MVCC mechanism** | Dead tuples in heap (`xmin`/`xmax`) + VACUUM | Undo log chain (`DB_ROLL_PTR`) + purge thread | Sequence numbers on keys; older versions evicted by compaction |
| **Operational complexity** | VACUUM tuning, bloat monitoring | Buffer pool, redo log sizing, compaction not user-visible | Compaction tuning, write stall monitoring, level sizing |

### When Each Architecture Is Preferred

**PostgreSQL / InnoDB (B-Tree) is preferred when:**
- Workload is read-heavy or mixed read-write.
- Range queries and sorted access are common.
- Transactions with arbitrary read-write patterns are required.
- Operational simplicity and mature tooling are priorities.
- The working set fits comfortably in the buffer pool / page cache.

**RocksDB (LSM) is preferred when:**
- Writes vastly outnumber reads (event ingestion, logging, CDC, time-series).
- The working set is too large for the buffer pool (cold data dominates).
- SSD is the storage medium and minimizing write amplification at the device level matters for device lifetime.
- The database is embedded in an application process (not a shared server).
- Key-value access patterns dominate over complex relational queries.

**The inflection point** is roughly where writes become the bottleneck rather than reads. For a 1:10 write-to-read ratio, B-trees perform well. For a 10:1 write-to-read ratio, LSM trees are almost always the better choice. For equal reads and writes with a working set that fits in memory, the two architectures perform comparably.

---

## 7. Key Learnings

### Why LSM Trees Are Write-Optimized

The write-optimization in LSM trees is architectural, not incidental. Every design element serves the goal of making disk writes sequential:

1. All writes land in the MemTable — no disk I/O at all.
2. MemTable flush produces a single sequential write to L0.
3. Compaction merges sorted files sequentially and writes sorted output sequentially.
4. No file is ever modified in place.

The result is that from the storage device's perspective, RocksDB generates only sequential writes. A B-tree under the same workload generates random writes, one per key inserted into the appropriate position in the tree. On any storage medium where sequential I/O outperforms random I/O, LSM wins on write throughput.

### Why Compaction Is Necessary

Compaction is not an optional optimization — it is structurally required for three reasons:

1. **Correctness of reads**: Without compaction, multiple versions of the same key accumulate across L0 files and levels. Reads would have to search everywhere and return the version with the highest sequence number — with no way to prune the search space.
2. **Space reclamation**: Deleted keys leave tombstones. Updated keys leave old versions. Only compaction physically removes these, freeing space. Without compaction, storage usage grows monotonically regardless of logical data size.
3. **Read performance**: Without compaction, the number of SSTables grows without bound, increasing read amplification without limit. Compaction keeps the number of levels bounded, keeping read cost bounded.

Compaction's cost — write amplification — is the price paid for maintaining a structure that is both writable without random I/O and readable without exhaustive search.

### Why Bloom Filters Help

The read path's worst case (key not present in database) would require reading index and data blocks in every SSTable at every level — potentially 50+ file reads. Bloom filters reduce this to 50+ in-memory bit-array checks, eliminating disk I/O entirely for absent keys. The false positive rate (~1% at 10 bits/key) means 1 in 100 "maybe" answers requires a data block read that then confirms absence — a minor overhead compared to no filter.

Bloom filters are most valuable when:
- The working set is larger than the block cache (cache-cold reads dominate).
- Negative lookups are frequent (e.g., checking existence before insert, cache miss patterns).
- The database has many levels (more SSTables to filter).

They provide diminishing returns when the block cache is large enough to hold all index and filter blocks, because the checks become in-cache reads anyway.

### Engineering Lessons

- **There is no free lunch between reads and writes.** Every database makes a fundamental choice about which operation to optimize. B-trees optimize reads by organizing data at write time. LSM trees optimize writes by deferring organization to compaction. Understanding this trade-off is the starting point for any storage engine evaluation.

- **Sequential I/O is architecturally preferable to random I/O on any hardware.** The LSM design would have no advantage if sequential and random I/O were equally fast. They are not — not on HDDs (by 100x), not on SSDs (by 3–10x), and not on NVMe (by 2–5x for sustained throughput vs IOPS-limited random writes). Hardware characteristics shape the correct software architecture.

- **Amortization is a design tool.** Compaction takes expensive work (sorting and merging) and distributes it in the background across time, preventing it from affecting the foreground write latency. VACUUM in PostgreSQL does the same for dead-tuple cleanup. Deferred, amortized background work is a recurring pattern in systems that need to maintain structural invariants without blocking the hot path.

- **Probabilistic data structures are worth their false positive rate.** Bloom filters accept a ~1% false positive rate in exchange for eliminating ~99% of unnecessary disk reads. In high-throughput systems, that trade-off is almost always correct. The cost of a filter miss (one wasted disk read) is far smaller than the cost of checking every SSTable (many disk reads).

- **The right choice depends on the access pattern, not on which system is "better."** RocksDB is strictly worse than PostgreSQL for read-heavy, relational, complex-query workloads. PostgreSQL is strictly worse than RocksDB for write-heavy, key-value, embedded workloads. Knowing why each system was built is the prerequisite for knowing when to use it.

---

## References

1. O'Neil, P., Cheng, E., Gawlick, D., & O'Neil, E. (1996). *The log-structured merge-tree (LSM-tree)*. Acta Informatica, 33(4), 351–385.
2. Facebook Engineering. *RocksDB: A Persistent Key-Value Store for Flash and RAM Storage*. https://rocksdb.org/
3. RocksDB Wiki. *Leveled Compaction*. https://github.com/facebook/rocksdb/wiki/Leveled-Compaction
4. RocksDB Wiki. *RocksDB Tuning Guide*. https://github.com/facebook/rocksdb/wiki/RocksDB-Tuning-Guide
5. RocksDB Wiki. *Bloom Filter*. https://github.com/facebook/rocksdb/wiki/RocksDB-Bloom-Filter
6. Cao, Z. et al. (2020). *Evolution of Development Priorities in Key-value Stores Serving Large-scale Applications: The RocksDB Experience*. FAST '20.
7. Dayan, N., Athanassoulis, M., & Idreos, S. (2017). *Monkey: Optimal Navigable Key-Value Store*. SIGMOD '17. (Optimal Bloom filter allocation across LSM levels.)
8. Luo, C., & Carey, M. J. (2020). *LSM-based Storage Techniques: A Survey*. The VLDB Journal.
9. RocksDB source: `db/memtable.cc`, `db/compaction/`, `table/block_based/` — MemTable, compaction, and SSTable implementation.
