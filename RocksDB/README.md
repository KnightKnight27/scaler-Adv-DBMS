# Topic 4: RocksDB Architecture

> **Course:** Advanced DBMS | **Name:** Penta Guna Sai Kumar | **Roll Number:** 24BCS10070

---

## Table of Contents
1. [Problem Background](#1-problem-background)
2. [Architecture Overview](#2-architecture-overview)
3. [Internal Design](#3-internal-design)
4. [Design Trade-Offs](#4-design-trade-offs)
5. [Experiments / Observations](#5-experiments--observations)
6. [Directly Answering the Core Study Questions](#6-directly-answering-the-core-study-questions)
7. [Key Learnings](#7-key-learnings)

> All architecture diagrams in this document are original ASCII diagrams authored by me; external material is credited in the References footer.

---

## 1. Problem Background

In 2012, Facebook was operating at a scale where traditional B+tree-based storage systems could no longer keep up. Their use case: serving hundreds of millions of users' social graph data from SSD-backed servers, with write-heavy workloads — likes, posts, friendships, notifications — at millions of writes per second.

The problem with B+trees at this scale:
- **Write amplification**: updating a B+tree page requires reading the full page (4-8KB), modifying a few bytes, and writing the full page back. For SSDs, random writes are significantly slower than sequential writes.
- **Fragmentation**: as B+trees grow, pages become partially filled (after splits and deletions), wasting space.
- **SSD wear**: SSDs have limited write endurance (P/E cycles). Excessive random writes accelerate SSD aging.

Facebook forked **LevelDB** (a key-value store from Google, itself based on the Log-Structured Merge tree paper by O'Neil et al., 1996) and built **RocksDB** — a storage engine designed specifically for high-throughput write workloads on fast storage (SSDs, NVMe). 

The core insight behind RocksDB's design: **convert random writes into sequential writes**. The LSM (Log-Structured Merge) tree achieves exactly this — at the cost of more complex reads and periodic background compaction.

---

## 2. Architecture Overview

```
Write Path:
                    Write Request
                         │
                    ┌────▼────┐
                    │  WAL    │  ← Sequential append (crash safety)
                    │(logfile)│
                    └────┬────┘
                         │
                    ┌────▼──────────────────────────┐
                    │         MemTable              │
                    │  (In-memory Skip List,        │
                    │   sorted by key)              │
                    │  Default: 64MB                │
                    └────┬──────────────────────────┘
                         │ (when full)
                    ┌────▼──────────────────────────┐
                    │    Immutable MemTable         │  ← Read-only, awaiting flush
                    └────┬──────────────────────────┘
                         │ (background flush thread)
                         ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                         LSM Tree Levels (Disk)                              │
│                                                                             │
│  L0 (Level 0): SST files flushed directly from MemTable                     │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐  ← may have overlapping key ranges     │
│  │SST-1 │ │SST-2 │ │SST-3 │ │SST-4 │    (up to L0_file_num_compaction_trigger)│
│  └──────┘ └──────┘ └──────┘ └──────┘                                        │
│                                                                             │
│  L1 (Level 1): ~256MB, sorted, no overlapping key ranges                    │
│  ┌──────────────────────────────────────────────────────┐                   │
│  │  [a-d] [e-h] [i-l] [m-p] [q-t] [u-z]               │                     │
│  └──────────────────────────────────────────────────────┘                   │
│                                                                             │
│  L2 (Level 2): ~2.5GB, sorted, non-overlapping                              │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                                                             │
│  ...                                                                        │
│                                                                             │
│  Ln (max level): 90% of all data lives here                                 │
└─────────────────────────────────────────────────────────────────────────────┘

Read Path:
  MemTable → Immutable MemTable → L0 (newest to oldest) → L1 → L2 → ... → Ln
  Bloom Filters on each SST skip irrelevant files
```

---

## 3. Internal Design

### 3.1 MemTable

The MemTable is RocksDB's in-memory write buffer. Every write (Put, Delete, Merge) is first inserted into the MemTable.

#### Default Implementation: Skip List

```
Skip List (sorted by key):
Level 3:  head ─────────────────────────────── "lion" ───────────────── tail
Level 2:  head ─────────── "cat" ─────────── "lion" ──── "tiger" ───── tail  
Level 1:  head ─── "ant" ── "cat" ── "dog" ── "lion" ── "owl" ── "tiger" ── tail
Level 0:  head ─ "ant" ─ "bat" ─ "cat" ─ "dog" ─ "fox" ─ "lion" ─ "owl" ─ "tiger" ─ tail
```

Skip lists provide O(log n) insert and search — equivalent to balanced BSTs but with simpler implementation for concurrent access. RocksDB's skip list uses CAS (compare-and-swap) for lock-free concurrent inserts.

**Why skip list over hash table?** Keys must be sorted for SST file generation — a sorted flush is required. A hash table can't produce sorted output efficiently.

**Alternative MemTable implementations** (pluggable):
- **Vector MemTable**: simple array, sorted at flush time. Best for bulk loads.
- **Hash Linked List**: fast for point lookups on prefix keys.
- **Hash Skip List**: hash map over prefix + skip list per prefix.

#### MemTable Lifecycle

```
State 1: Active MemTable — accepts new writes
         └── When full (write_buffer_size, default 64MB):

State 2: Immutable MemTable — read-only, waiting for background flush
         └── Background flush thread writes it as L0 SST file
         └── WAL segment for this MemTable can now be deleted
         └── A new Active MemTable is created

State 3: Flushed — gone from memory, data now in L0 SST file
```

RocksDB can pipeline multiple immutable MemTables (`max_write_buffer_number`), allowing new writes to continue while flush is in progress — eliminating write stalls during flush.

#### Garbage Collection During Flush

When flushing to SST, RocksDB performs **inline compaction**:
- Removes duplicate keys (keeps only the latest version)
- Removes deleted keys (`DELETE` records) if no older snapshot needs them
- This reduces SST file size before it even hits L0

---

### 3.2 SST Files (Sorted String Tables)

SST files are immutable sorted files on disk. Once written, they are never modified — only compacted into new files and then deleted.

#### Block-Based SST Format (default)

```
SST File Layout:
┌──────────────────────────────────┐
│  Data Blocks                     │  ← key-value pairs, sorted by key
│  [Block 0: keys 1-N]             │  Each block typically 4KB (default)
│  [Block 1: keys N+1-M]           │  Optionally compressed (Snappy/ZSTD)
│  ...                             │
├──────────────────────────────────┤
│  Index Block                     │  ← "last key of each data block → block offset"
│  (allows binary search for key)  │  Loaded into block cache
├──────────────────────────────────┤
│  Filter Block                    │  ← Bloom Filter over all keys in file
│  (probabilistic membership test) │  "Is key X possibly in this file?"
├──────────────────────────────────┤
│  Compression Dictionary          │  (if dictionary compression enabled)
├──────────────────────────────────┤
│  Meta Index Block                │  ← offsets of filter + compression blocks
├──────────────────────────────────┤
│  Footer (48 bytes)               │  ← magic number, format version, metaindex offset
└──────────────────────────────────┘
```

---

### 3.3 Bloom Filters

Bloom filters are probabilistic data structures that answer: **"Is key X definitely NOT in this SST file?"**

```
Bloom Filter (simplified, 8-bit array):
Initial state: [0,0,0,0,0,0,0,0]

Add key "alice":  hash1("alice")=2, hash2("alice")=5 → [0,0,1,0,0,1,0,0]
Add key "bob":    hash1("bob")=1,   hash2("bob")=4  → [0,1,1,0,1,1,0,0]

Query "carol":    hash1("carol")=3, hash2("carol")=7 → bits 3,7 = 0,0 → DEFINITELY NOT IN FILE
Query "alice":    hash1("alice")=2, hash2("alice")=5 → bits 2,5 = 1,1 → POSSIBLY IN FILE (need to check)
```

For a read request:
1. Check Bloom filter for target SST — O(1)
2. If filter says NO → skip SST entirely (no disk I/O)
3. If filter says YES → read SST blocks to find the key (disk I/O)

False positive rate is configurable (bits_per_key, default 10 bits → ~1% false positive rate). With Bloom filters, a point lookup typically reads only 1-2 SST files even across many levels, reducing read amplification dramatically.

**Bloom filters + L0**: L0 SSTs may have overlapping key ranges, so a read might need to check all L0 files. Bloom filters make this O(number_of_L0_files) bit-checks rather than O(L0_files × disk_seeks).

---

### 3.4 Write Path

```
1. Client: Put("user:1001", "{name: Alice}")

2. WAL Write (if WAL enabled):
   → Serialize record to WAL buffer
   → WAL buffer flushed to logfile (sequential append)

3. MemTable Insert:
   → Skip list insert of (key="user:1001", value="{name: Alice}", seq_num=42)
   → seq_num is a monotonically increasing version number per write

4. Return success to client
   (at this point: durable if WAL was flushed, otherwise durable after next WAL flush)

5. Background: When MemTable is full:
   → Mark MemTable immutable
   → Create new Active MemTable
   → Flush thread sorts + writes immutable MemTable to L0 SST file
   → WAL segment for this MemTable deleted
```

Total work on critical path: 1 sequential WAL append + 1 skip list insert. This is why RocksDB achieves millions of writes/second.

---

### 3.5 Read Path

```
1. Client: Get("user:1001")

2. Check Active MemTable (skip list lookup)
   → Found? Return value immediately.

3. Check Immutable MemTables (newest to oldest)
   → Found? Return value.

4. For each level L0, L1, ..., Ln:
   a. Identify candidate SST files (overlapping key range for this key)
   b. For each candidate SST: check Bloom filter
      → Bloom says NO: skip file (no I/O)
      → Bloom says MAYBE: read Index Block → binary search → read Data Block
   c. Found key? Return value.

5. Key not found in any level → return "not found"
```

**Read amplification** = number of disk I/Os per read. In the worst case (key not in any filter, at deepest level): O(levels). In practice, Bloom filters + block cache reduce this to 1-2 disk reads for most queries.

---

### 3.6 Compaction

Compaction is RocksDB's background process that merges SST files across levels, removing duplicates and deleted keys. It is the price paid for the write-speed benefits of the LSM design.

#### Level-Style Compaction (default)

```
Trigger: L0 has too many files (L0_file_num_compaction_trigger, default 4)

Action: Pick 1 file from L0 → find all overlapping files in L1 → merge-sort all → write new L1 files

         L0: [a-z] 
         L1: [a-d][e-h][i-l][m-p][q-t][u-z]
              ↓ compaction picks [a-d] from L1 (overlaps with L0[a-z])
         Reads: L0 file + [a-d] from L1
         Writes: new [a-d] file in L1 (with duplicates removed, deletes applied)

Level sizes: L1=256MB, L2=2.56GB, L3=25.6GB (multiplier: max_bytes_for_level_multiplier=10)
Write amplification per level: ~10x
Total write amplification: ~30x for a 5-level setup (worst case)
```

#### Universal-Style Compaction

Instead of level-by-level, merges all files at once when total size ratio is exceeded. Results in:
- **Lower write amplification** (~10x vs ~30x)
- **Higher space amplification** (temporarily holds 2x data during compaction)
- **Higher read amplification** (more files to check)

Good for write-heavy workloads where write amplification is the bottleneck.

#### FIFO Compaction

No merging at all — just deletes the oldest file when total size exceeds limit. Effectively a cache with TTL semantics. Not suitable for general-purpose storage.

#### Compaction Filter

Applications can plug in a **compaction filter** — a callback invoked on each key during compaction. Use cases:
- TTL expiry: delete keys whose timestamp is older than N days
- Data transformation: modify values during compaction
- Access control: strip sensitive fields during background cleanup

This is used heavily in Facebook's Cassandra-like use cases where data has inherent expiration.

---

### 3.7 Column Families

RocksDB supports multiple **column families** within a single database — logical namespaces with separate MemTables, SST files, and compaction configurations, but sharing a single WAL.

```
DB
├── Column Family "default"
│   ├── MemTable
│   └── L0...Ln SST files
├── Column Family "metadata"
│   ├── MemTable (can have different write_buffer_size)
│   └── L0...Ln SST files
└── Column Family "sessions"
    ├── MemTable
    └── L0...Ln SST files (can use different compaction style)
```

Cross-column-family writes are atomic via `WriteBatch`. This enables different data types to coexist with tailored performance characteristics — e.g., hot write-heavy data in one CF with smaller MemTables and more aggressive compaction, cold archival data in another CF with FIFO compaction.

---

### 3.8 WAL

RocksDB's WAL is simpler than PostgreSQL's. It is a plain sequential log file. Every `Put`/`Delete`/`Merge` writes a record to the WAL before modifying the MemTable. On crash recovery, RocksDB replays the WAL to reconstruct any MemTable contents not yet flushed to SST files.

Unlike PostgreSQL's WAL (which is also used for replication), RocksDB's WAL is purely for crash recovery. Replication in RocksDB-backed systems (like MyRocks or TiKV) is handled at a higher layer.

---

### 3.9 Deletes and Tombstones

Because SST files are **immutable**, a `Delete` cannot remove data in place. Instead RocksDB writes a **tombstone** — a delete marker with its own sequence number — into the MemTable, exactly like a normal write.

```
Put("k", "v1")  seq=10  ─┐
Delete("k")     seq=25   ├─▶ during read, highest seq wins → "k" reported as not-found
Put("k", "v2")  seq=40  ─┘   (seq=40 > 25, so "k" = "v2")
```

On reads, the newest sequence number for a key wins, so a tombstone shadows all older values. The tombstone — and the data it shadows — is only **physically reclaimed during compaction**, and only once it has reached a level where no older version exists and no live snapshot still needs it. Two consequences:

- **Delete-heavy workloads temporarily grow space and read amplification**: deleted keys keep occupying SST space until compaction reaches them, and scans must read past accumulated tombstones.
- **Range deletes** use a special `DeleteRange` tombstone so deleting a million contiguous keys is one record, not a million — but the shadowed data still lingers until compacted.

This is the LSM mirror image of PostgreSQL's dead tuples: deletion is cheap and sequential at write time, with the reclamation cost deferred to background compaction.

---

## 4. Design Trade-Offs

### The Fundamental LSM Trade-off: Write Amplification vs. Read Amplification

```
                 B+Tree                    LSM Tree
               (PostgreSQL/MySQL)          (RocksDB)
Write Amp:      Low (1-2x)                High (10-30x)
Read Amp:       Low (1-3 I/Os)            Medium (1-7 I/Os with Bloom)
Space Amp:      Low (~1x)                 Medium (1.1-3x depending on compaction)
Write Speed:    Random I/O bound          Sequential I/O bound (10-100x faster on SSD)
```

RocksDB is the right choice when writes >> reads and data is accessed primarily by key (not complex range joins). It is the wrong choice for OLAP workloads with large range scans and complex joins.

### Specific Trade-offs

| Design Decision | Benefit | Cost |
| Immutable SST files | Simple concurrent reads, no locking | Compaction required to reclaim space and merge versions |
| MemTable (skip list) | Lock-free concurrent inserts | Data in MemTable lost if WAL disabled + crash |
| Bloom filters | Nearly eliminates I/O for missing keys | ~10 bits/key memory overhead; false positives still require disk read |
| Leveled compaction | Low space amplification, good read perf | High write amplification (~10x per level) |
| Universal compaction | Low write amplification | High space + read amplification |
| Column families | Per-CF tuning | More complex management |
| Compaction filters | In-line TTL/transformation | Compaction latency increases |

### Why LSM is preferred in write-heavy workloads

A SSD can do sequential writes at ~3 GB/s but random writes at only ~200 MB/s. B+tree updates require random writes (modify arbitrary pages). LSM writes are **always sequential** — WAL append + MemTable flush produces one big sequential write per batch of operations. On NVMe SSDs, this difference is 10-50x in throughput.

### Why compaction can become expensive

Compaction reads SST files, merges them, and writes new files. For L1→L2 compaction, a 256MB L1 compacting with overlapping 256MB in L2 reads and writes 512MB. Each byte written to L1 may be written again at L1→L2, L2→L3, etc. In the worst case, a single write at L0 is rewritten ~6 times by the time it reaches Lmax — hence write amplification of 10-30x.

Large compaction jobs also consume disk bandwidth, causing **write stalls** — RocksDB throttles writes when L0 file count or pending compaction bytes exceed thresholds. This is the main operational pain point of RocksDB at scale.

---

## 5. Experiments / Observations

> **Environment:** RocksDB 9.10.0 (librocksdb-dev) | C++ benchmark compiled with g++ -O2 | 50,000 keys, value size ~100 bytes

### Experiment 1: Write Performance — Sequential Writes

**Benchmark code (C++, RocksDB API):**
```cpp
// 50,000 sequential key-value pairs, leveled compaction + bloom filter
for (int i = 0; i < 50000; i++) {
    snprintf(key, sizeof(key), "key%08d", i);
    snprintf(val, sizeof(val), "value_%08d_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", i);
    db->Put(write_options, key, val);
}
db->Flush(FlushOptions());
db->CompactRange(CompactRangeOptions(), nullptr, nullptr);
```

**Actual output:**
```
=== Leveled Compaction + Bloom Filter ===
  Write 50000 keys (seq):  164 ms  (304,579 ops/s)
  SST on disk:             615,981 bytes  (601 KB — 1 file at L6)

=== Leveled Compaction (no bloom) ===
  Write 50000 keys (seq):  160 ms  (310,641 ops/s)
  SST on disk:             553,369 bytes  (540 KB — 1 file at L6)

=== Universal Compaction + Bloom Filter ===
  Write 50000 keys (seq):  160 ms  (310,721 ops/s)
  SST on disk:             549,036 bytes  (536 KB — 1 file at L6)
```

**Observation:** All three configurations write at ~300K ops/s — sequential writes go directly to MemTable (memory) then flush to SST in one shot, so compaction style doesn't affect write throughput at this scale. At larger datasets with multiple SST levels, leveled compaction generates more background I/O than universal.

---

### Experiment 2: Write Amplification by Compaction Style

**Compaction stats from `rocksdb.stats` after flush + compact:**

**Leveled compaction (default):**
```
** Compaction Stats [default] **
Level    Files   Size     Score W-Amp  Wr(MB/s) Comp(sec) KeyIn  KeyDrop
  L0      0/0    0.00 KB   0.0   1.0    33.8      0.02       0      0
  L6      1/0  601.54 KB   0.0   0.0     0.0      0.00       0      0
 Sum      1/0  601.54 KB   0.0   1.0    33.8      0.02       0      0
```
- WAF (Write Amplification Factor) = **1.0** (sequential write, single SST, no re-compaction needed)

**Universal compaction:**
```
** Compaction Stats [default] **
Level    Files   Size     Score W-Amp  Wr(MB/s) Comp(sec) KeyIn  KeyDrop
  L0      0/0    0.00 KB   0.2   1.0    35.6      0.02       0      0
  L6      1/0  536.17 KB   0.0   0.9    16.1      0.03     50K      0
 Sum      1/0  536.17 KB   0.0   1.9    22.7      0.05     50K      0
```
- WAF = **1.9** — universal compaction performed 2 compaction passes (L0→L6 flush + L6 re-merge), writing 50K keys twice
- At large scale (millions of keys, many levels), leveled WAF can reach 10-30x vs universal's 5-15x

**Key insight:** Leveled compaction bounds space amplification (only ~1.1x at steady state) at the cost of higher WAF. Universal compaction reduces WAF but temporarily holds 2x data during compaction, increasing space amplification.

---

### Experiment 3: Bloom Filter Impact on Read Performance

**Point lookup benchmark: 1,000 random reads**

```
=== Leveled + Bloom Filter (10 bits/key) ===
  Random read 1000: 6 ms  (158,780 ops/s), hits=1000/1000

=== Leveled, no Bloom Filter ===
  Random read 1000: 5 ms  (179,211 ops/s), hits=1000/1000
```

**Note:** Both show similar performance here because the dataset fits in the block cache (warm cache scenario). The bloom filter's advantage is most visible for **non-existent key lookups** and **cold reads** where SST files must be checked on disk.

**Theoretical model for Bloom filter benefit:**
```
Without Bloom: Every point read must check ALL SST files at every level
  → L0: up to 4 file reads (L0 files overlap), L1-L6: 1 read each
  → Worst case: 10 disk reads per point lookup

With Bloom (10 bits/key, ~1% false positive rate):
  → Per file: 1 cheap memory check (Bloom) → skip file if negative
  → Only ~1% of checks result in unnecessary disk reads
  → Practical average: 1.01–1.5 disk reads per point lookup
  
Speedup on cold cache (as measured in literature): 5–8x for missing keys
```

---

### Experiment 4: SST Level Structure — Leveled vs Universal

**After inserting 50K keys and compacting:**

**Leveled compaction — manifest dump:**
```
--- level 0 --- version# 1 ---  (empty — all data compacted down)
--- level 1 --- version# 1 ---  (empty)
--- level 2 --- version# 1 ---  (empty)
--- level 3 --- version# 1 ---  (empty)
--- level 4 --- version# 1 ---  (empty)
--- level 5 --- version# 1 ---  (empty)
--- level 6 --- version# 1 ---  
  1 SST file: 601.54 KB  (all 50,000 keys in sorted order at deepest level)
```

**Universal compaction — manifest dump:**
```
--- level 0 --- (empty after compaction)
--- level 6 --- 
  1 SST file: 536.17 KB  (same data, slightly smaller — no bloom filter overhead)
```

**Read path with data at L6:**
```
Point lookup for "key00025000":
1. Check MemTable → miss (empty after flush)
2. Check L0 → empty
3. Check L1 → empty
4. ...
5. Check L6 → Bloom filter says MAYBE → read SST file → found in 1 disk I/O

Total I/Os: 1  (ideal case for a fully compacted single-file DB)
```

---

### Experiment 5: Compaction Write Stall (Observed Behavior)

**db_bench batchput performance before compact vs after:**

```
=== Writes before compact (data in WAL + MemTable) ===
  batchput 500 keys:   24.1 ms  (20,724 ops/s)

=== After manual compact ===
  scan 500 keys:        8.8 ms  (57,073 keys/s)  ← SST sequential read
```

**Write stall thresholds (RocksDB defaults, observed in production):**
```
level0_slowdown_writes_trigger = 20 L0 files  → writes throttled by 50%
level0_stop_writes_trigger     = 36 L0 files  → writes completely blocked

memtable_memory_budget = 512MB (default)
When MemTable fills faster than flush thread can write:
  → Write stall begins
  → All Put() calls block until background flush completes
```

**OPTIONS-000007 (actual RocksDB options file generated by our benchmark):**
```
[CFOptions "default"]
  compaction_style=kCompactionStyleLevel
  write_buffer_size=67108864         (64MB MemTable)
  max_write_buffer_number=2          (at most 2 MemTables before stall)
  level0_file_num_compaction_trigger=4
  level0_slowdown_writes_trigger=20
  level0_stop_writes_trigger=36
  target_file_size_base=67108864     (64MB target SST size)
  max_bytes_for_level_base=268435456 (256MB L1 budget)
```

This shows the actual configuration RocksDB chose for our workload. Tuning these parameters is the primary operational challenge in production RocksDB deployments.

---

## 6. Directly Answering the Core Study Questions

**Q: Why are LSM trees preferred in write-heavy workloads?**
Because they convert *random* writes into *sequential* writes. A write only appends to the WAL and inserts into an in-memory skip list; flushes and compactions emit large sequential SST writes. On SSD/NVMe, sequential throughput is 10–50× random throughput (~3 GB/s vs ~200 MB/s), so the critical path — one sequential append + one memory insert — sustains millions of writes/sec. B+trees instead modify arbitrary pages in place, incurring random I/O and read-modify-write amplification per update.

**Q: Why can compaction become expensive?**
Compaction re-reads and re-writes data that is already on disk to merge files, drop duplicates, and apply tombstones. In leveled compaction a byte may be rewritten once per level on its way to Lmax, giving **write amplification of ~10–30×**. These background reads/writes compete with foreground traffic for disk bandwidth; when L0 file count or pending-compaction bytes cross thresholds, RocksDB **throttles or stalls writes** (Experiment 5). So the very mechanism that keeps reads fast is also the main source of write amplification, SSD wear, and tail-latency spikes.

**Q: How do Bloom filters improve read performance?**
A Bloom filter per SST answers "is key X *definitely not* here?" with an O(1) in-memory bit check and no false negatives (only ~1% false positives at 10 bits/key). On a point lookup, any SST whose filter says "no" is skipped with zero disk I/O. Without filters, a read may have to probe every SST at every level (O(files) disk seeks); with them, a typical lookup touches only 1–2 SSTs — turning read amplification from O(levels × files) into roughly O(1) for the common case, and giving the biggest gains on lookups for non-existent keys and cold cache.

---

## 7. Key Learnings

1. **LSM trees invert the write/read trade-off**: B+trees optimize for reads at the cost of random write I/O. LSM trees convert all writes into sequential I/O (10-100x faster on SSDs) at the cost of needing compaction to maintain read performance. This is the right trade-off when you have more writes than reads — social graphs, time-series, event logs, message queues.

2. **Compaction is not optional — it's load-bearing**: Without compaction, L0 fills with overlapping files, read amplification grows unboundedly, and space amplification explodes. Compaction is what makes the LSM tree practical, not just a B+tree with extra steps. The engineering challenge is ensuring compaction keeps pace with write throughput.

3. **Bloom filters are the key to making reads fast**: Without them, every point lookup must check every SST file at every level — O(files × disk_seeks). With Bloom filters, this becomes O(levels × 1 bit_check + 1 disk_read). The 10 bits/key memory cost is one of the highest-ROI tuning decisions in RocksDB.

4. **Write amplification is the hidden SSD wear problem**: Writing 30x more bytes than the logical data means an SSD with 100 TB total write endurance exhausts itself after 3.3 TB of user writes (in the worst case). Production RocksDB deployments carefully monitor WAF (write amplification factor) and tune compaction strategies.

5. **RocksDB's pluggability is a competitive advantage**: Pluggable MemTables, pluggable compaction algorithms, compaction filters, column families — this makes RocksDB adaptable to radically different workloads (TiKV for distributed KV, MyRocks for MySQL, Cassandra's SSTables, CockroachDB's Pebble fork). The same LSM skeleton powers a wide range of systems.

6. **The WAL in RocksDB is simpler than PostgreSQL's**: PostgreSQL's WAL is the single source of truth for both recovery and replication. RocksDB's WAL is purely for MemTable reconstruction on crash — once the MemTable is flushed to an immutable SST, the WAL segment is expendable. Replication is a higher-level concern.

---

*References: RocksDB Wiki (github.com/facebook/rocksdb/wiki); "Optimizing Space Amplification in RocksDB" (CIDR 2017, Dong et al.); "Benchmarking, Analyzing, and Optimizing Write Amplification" (EDBT 2025); "Constructing and Analyzing the LSM Compaction Design Space" (VLDB 2021); RocksDB Tuning Guide; db_bench documentation; "Characterizing, Modeling, and Benchmarking RocksDB" (FAST 2020)*