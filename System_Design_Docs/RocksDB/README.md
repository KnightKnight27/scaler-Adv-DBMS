# RocksDB — LSM-Tree Based Storage Architecture

> **Author:** Prabhav Semwal | **Roll:** 24bcs10358  
> **Environment:** RocksDB `db_bench` tool in Podman (`ubuntu:22.04`, `rocksdb-tools` package)  
> **Benchmarks:** 500k key–value pairs, 100–200 byte values, leveled vs. universal compaction

---

## Table of Contents
1. [Problem Background](#1-problem-background)
2. [Architecture Overview](#2-architecture-overview)
3. [Internal Design](#3-internal-design)
4. [Design Trade-Offs](#4-design-trade-offs)
5. [Experiments / Observations](#5-experiments--observations)
6. [Key Learnings](#6-key-learnings)
7. [References](#7-references)

---

## 1. Problem Background

RocksDB was created at Facebook in 2012 as a fork of Google's LevelDB. The motivation was a specific production problem: Facebook's storage systems were dominated by SSDs, and the existing storage engines (B-tree based) were leaving performance on the table. SSDs are fast for random reads but have asymmetric write characteristics — sequential writes are dramatically faster than random writes, and SSDs degrade when written to in small random chunks because of the need to erase large blocks before rewriting.

The **Log-Structured Merge-tree (LSM-tree)** architecture that RocksDB implements was originally described by O'Neil et al. (1996) and addresses this by **converting all writes into sequential I/O operations**, regardless of the key distribution. The cost paid is that reads can be more expensive (must check multiple levels) and background compaction is required to maintain read performance.

RocksDB is embedded in many production systems: Facebook's MyRocks (MySQL with RocksDB), Apache Cassandra's storage tier options, TiKV (TiDB's storage layer), CockroachDB, and LinkedIn's Voldemort.

---

## 2. Architecture Overview

```mermaid
flowchart TB
    subgraph Write["Write Path (sequential)"]
        direction TB
        W[Write Request] --> WAL[WAL\nAppend to log file]
        WAL --> MT[Active MemTable\nSkipList in RAM]
        MT -->|"full (write_buffer_size)"| IMT[Immutable MemTable]
        IMT -->|flush thread| L0["L0 SSTables\n(may overlap key ranges)"]
    end

    subgraph Levels["Disk Levels (compaction)"]
        direction TB
        L0 -->|compaction| L1["L1 SSTables\n(disjoint key ranges, size-bounded)"]
        L1 -->|compaction| L2["L2 SSTables"]
        L2 -->|compaction| Ln["... Ln\n(each level 10× bigger)"]
    end

    subgraph Read["Read Path"]
        direction TB
        R[Read Request] --> RMT[Check MemTable]
        RMT --> RIMT[Check Immutable MemTables]
        RIMT --> BF[Bloom Filter per SSTable\n(skip unlikely files)]
        BF --> RL0[Search L0 SSTs\n(all of them, keys overlap)]
        RL0 --> RL1["Search L1+ SSTs\n(binary search, disjoint)"]
    end
```

### Key components

| Component | Role |
|---|---|
| **WAL** | Append-only write log on disk; ensures durability before MemTable flush |
| **MemTable** | In-memory sorted structure (SkipList by default); all writes land here |
| **Immutable MemTable** | Frozen MemTable awaiting flush; reads still served from it |
| **SSTable (Sorted String Table)** | Immutable sorted file on disk; written once during flush/compaction |
| **Bloom Filter** | Per-SSTable probabilistic structure; eliminates unnecessary SSTable I/O on reads |
| **Block Cache** | In-memory LRU cache of decompressed SSTable blocks |
| **Compaction** | Background merge of SSTables: reclaims deleted/overwritten keys, maintains level bounds |

---

## 3. Internal Design

### 3.1 Write Path — Sequential by Design

Every write in RocksDB follows exactly this sequence:

```
1. Append key-value to WAL (sequential disk write)
2. Insert into active MemTable (SkipList, in RAM)
3. Return "success" to the caller
```

Neither step 1 nor step 2 is a random write. The WAL is append-only (sequential). The SkipList insert is O(log n) in memory with no disk I/O. The actual SSTable files are written in large sequential chunks during flush and compaction. This is the core insight of LSM trees: **all disk I/O is sequential**, regardless of key access pattern.

**MemTable as a SkipList:**
```
Level 3:  ───── key_0042 ─────────────────────────────── key_9871
Level 2:  ───── key_0042 ─── key_2311 ─── key_5500 ───── key_9871
Level 1:  ───── key_0042 ─── key_2311 ─── key_4421 ─── key_5500 ─── key_9871
Level 0:  key_0001 ─ key_0042 ─ key_1003 ─ key_2311 ─ key_4421 ─ key_5500 ─ key_9871
```
Probabilistic skip pointers provide O(log n) insert and search. When the MemTable reaches `write_buffer_size` (default 64 MB), it becomes immutable and a new one opens.

### 3.2 SSTable (Sorted String Table) Format

When an immutable MemTable is flushed (or compaction produces output), the result is an **SSTable** — an immutable, sorted, self-contained file:

```
SSTable layout:
┌─────────────────────────────────────────┐
│  Data Blocks (sorted key-value pairs)   │
│  (compressed, default block size 4KB)   │
├─────────────────────────────────────────┤
│  Index Block (key → data block offset)  │
├─────────────────────────────────────────┤
│  Filter Block (Bloom filter bits)       │
├─────────────────────────────────────────┤
│  Metaindex Block                        │
├─────────────────────────────────────────┤
│  Footer (magic, version, pointers)      │
└─────────────────────────────────────────┘
```

SSTables are **never modified** after creation. An overwritten key produces a *new* SSTable entry; the old entry is garbage-collected by compaction.

### 3.3 Read Path — Multi-level Search

To read a key, RocksDB searches from the freshest data down:

```
1. Active MemTable        (latest writes, O(log n) SkipList search)
2. Immutable MemTable(s)  (not yet flushed)
3. L0 SSTables            (ALL must be checked — keys can overlap)
4. L1+ SSTables           (binary search on level metadata → one SSTable)
```

**Why L0 is special:** When MemTables flush to L0, key ranges can overlap between L0 files (two flushes can both contain key_5000). Every L0 file must be checked. L0 file count directly impacts read latency — this is why compaction is triggered eagerly at L0.

**Bloom Filters:** Before reading any SSTable, RocksDB checks the **Bloom filter** — a compact probabilistic data structure that answers "definitely does NOT contain this key" with zero false negatives (but has a small false-positive rate). A bloom filter check costs a few memory lookups; reading a 25 MB SSTable costs many disk I/Os. Bloom filters eliminate the vast majority of unnecessary SSTable reads for point lookups.

```
Space/accuracy trade-off:
  10 bits/key → ~1% false positive rate
   7 bits/key → ~5% false positive rate
```

### 3.4 Compaction — The Maintenance Engine

Compaction is the background process that merges SSTables from one level into the next:

**Leveled Compaction (default):**
- L0 → L1: triggered when L0 has ≥ `level0_file_num_compaction_trigger` (default 4) files
- Each level Ln has a target size (L1=256MB, L2=2.56GB, L3=25.6GB, ... ×10 each level)
- When Ln exceeds its target, one SSTable is selected and merged with all overlapping SSTables in Ln+1
- **Result:** L1+ always have disjoint key ranges — one SSTable per key at each level
- **Trade-off:** High write amplification (a key written once may be rewritten O(levels) times during compaction), but low read amplification (only O(1) SSTable per level for a point lookup)

**Universal Compaction:**
- All SSTables are sorted by creation time
- Merge is triggered when size ratios between adjacent "sorted runs" exceed a threshold
- **Result:** Fewer rewrites — better write amplification
- **Trade-off:** More files per read (the "sorted runs" are not level-bounded); higher space amplification during compaction (need room for input + output simultaneously)

**Amplification factors defined:**
- **Write amplification:** bytes written to disk / bytes written by application. LSM leveled: 10–30×. B-tree: ~2-3×.
- **Read amplification:** disk reads per logical read. LSM: 1 (bloom hit) to O(levels). B-tree: O(log n) always.
- **Space amplification:** disk space / live data size. LSM: ~1.1× (leveled) to 2× (universal during compaction). B-tree: 1.3–1.5×.

### 3.5 WAL and Durability

The WAL (write-ahead log) is the durability guarantee for data in MemTables:

```
Write → WAL → MemTable → return to caller

Crash happens:
  - If MemTable was flushed to SSTable → WAL entry can be discarded
  - If MemTable was not flushed → replay WAL records on restart
```

Once an immutable MemTable is successfully flushed to an L0 SSTable, the corresponding WAL segment is archived and eventually deleted. The WAL is never compacted — it is purely an append-only safety net.

---

## 4. Design Trade-Offs

### LSM vs. B-tree: the fundamental choice

| Property | LSM Tree (RocksDB) | B-tree (PostgreSQL, InnoDB) |
|---|---|---|
| **Write path** | Sequential (WAL + MemTable) | Random in-place (find the right page, modify it) |
| **Write amplification** | High (10–30× with leveled) | Low (2–3×) |
| **Read amplification** | Variable (bloom filter, then O(levels)) | Consistent O(log n) |
| **Space amplification** | Low–moderate (leveled ~1.1×) | Low (1.3–1.5×) but grows with fragmentation |
| **Write-heavy workload** | ✅ Excellent (all writes sequential) | ⚠️ Good (in-place, but random I/O at scale) |
| **Point lookup (warm cache)** | ✅ Bloom filter → O(1) often | ✅ B-tree O(log n) always |
| **Range scan** | ⚠️ Must merge across levels | ✅ Sequential heap/clustered read |
| **Compaction cost** | High CPU + I/O at unpredictable times | No equivalent (some in-place fragmentation) |
| **SSD friendliness** | ✅ Sequential writes = fewer erase cycles | ⚠️ Random writes = higher write amplification on SSD |

### Why LSM trees are preferred for write-heavy workloads

An SSD write cycle: the SSD must erase a block (~256 KB or larger) before writing to it, even for a 4 KB page update. If your workload writes 4 KB randomly across a 10 GB dataset, each write effectively erases and rewrites a large block — massive write amplification at the hardware level.

LSM trees batch writes into large sequential chunks (MemTable flushes, SSTable compaction outputs). Even if you write 1 million random keys, RocksDB produces a handful of large sequential SSTable files — the SSD sees sequential writes and is happy. This reduces SSD wear and maximizes write throughput.

### Why compaction becomes expensive

Compaction reads all input SSTables, merges them (re-sorting and de-duplicating), and writes the output. For a leveled strategy with levels growing 10× each:

- L1 → L2 compaction may read and rewrite up to 10× more data than was originally written to L1
- At peak write rates, compaction may not keep up → L0 file count grows → read latency degrades → RocksDB **stalls writes** until compaction catches up

This "compaction debt" is a critical operational concern for RocksDB-backed systems. Configuration knobs: `max_bytes_for_level_base`, `target_file_size_base`, `max_background_compactions`.

### Bloom Filter accuracy vs. memory

A Bloom filter with `bits_per_key=10` uses 10 bits per key in the database. For 500k keys, that's 500k × 10 / 8 = 625 KB per SSTable. The benefit: for a point lookup that misses, the bloom filter avoids reading any data blocks from the SSTable entirely. The cost: memory usage scales with the total number of keys across all SSTs.

---

## 5. Experiments / Observations

> Benchmarks run with `db_bench` (RocksDB tools, Ubuntu 22.04, Podman container). Workloads: 500,000 operations, 100–200 byte values. All timings on container-hosted filesystem.

### Experiment 1 — Sequential writes (`fillseq`)

```
fillseq : 2.130 micros/op | 469,550 ops/sec | 51.9 MB/s
```
Writing 500k key-value pairs in sequential key order achieves **469k ops/sec**. Sequential keys mean: each MemTable is sorted by creation time ≈ key order, flush produces a single large SSTable covering a contiguous key range, and compaction rarely needs to merge many files. This is the "best case" for LSM trees.

### Experiment 2 — Random writes (`fillrandom`)

```
fillrandom : 3.312 micros/op | 301,888 ops/sec | 33.4 MB/s
```
Random key writes achieve **302k ops/sec** — 36% lower than sequential. Random keys scatter across MemTable entries uniformly, but crucially the writes themselves are still sequential to disk (WAL + SSTable flush). The overhead vs. `fillseq` is the SkipList maintenance cost and more complex compaction (more files need merging because key ranges overlap more). Compare this to B-tree random writes on spinning disk, which can be 10–100× slower due to random seek I/O.

### Experiment 3 — Random reads after `fillrandom`

```
readrandom : 3.732 micros/op | 267,925 ops/sec | 20.9 MB/s
             140,952 of 200,000 found (29% miss rate — expected for random keys not all present)
```
Random point lookups achieve **268k ops/sec**. The miss rate (29%) shows the bloom filter is working — misses require checking all levels but the filter rejects most SSTable reads before any disk I/O. The 3.7 µs/op latency includes bloom filter checks plus actual data block reads for hits.

### Experiment 4 — Concurrent read + write (`readwhilewriting`)

```
readwhilewriting : 0.827 micros/op | 1,209,082 ops/sec | 12.3 MB/s
                   27,581 of 300,000 found (90.8% miss rate — reader is finding very few keys)
```
The combined read+write benchmark shows **1.2M ops/sec** total. The high miss rate (90.8%) indicates the reads are primarily hitting the Bloom filter and returning fast (the read key space is sparse relative to what was written). This demonstrates the **lock-free read path** — readers never block writers in RocksDB; concurrent readers and writers each operate on different memory regions.

### Experiment 5 — Leveled vs. Universal compaction

| | Leveled | Universal |
|---|---|---|
| fillrandom ops/sec | **309,719** | 318,299 |
| fillrandom MB/s | 63.8 | **65.6** |
| Compaction write (flush) | 0.07 GB @ 46.3 MB/s | 0.07 GB @ 47.6 MB/s |
| Stalls | 0 | 0 |

At this dataset size (500k × 200B = ~95 MB), both strategies produce similar throughput because the data fits in one or two SSTable files and major cross-level compaction hasn't been triggered yet. **The real difference emerges at multi-GB datasets:**

- **Leveled** maintains tight read amplification (1 SSTable per key per non-L0 level) at the cost of high write amplification as keys are rewritten through successive levels.
- **Universal** reduces write amplification (sorted runs are merged less eagerly) but can have more SSTable files to check per read and requires 2× space during major compaction.

**Compaction stats output (leveled):**
```
Cumulative writes: 500K writes, 500K keys, 500K commit groups
WAL:    written 0.11 GB @ 68 MB/s
Flush:  written 0.07 GB @ 46 MB/s
Stalls: 0 (all levels, no backpressure triggered)
```

The WAL wrote 110 MB (the raw input data) and the flush produced 70 MB (compression ratio applies even with `compression_type=none` due to block padding). No write stalls — data was small enough to flush before L0 filled.

### Experiment 6 — SSTable file layout on disk

After writing 500k keys (leveled compaction):
```
000007.sst  25M
000010.sst  25M
000012.sst  25M
CURRENT     16B    ← points to current MANIFEST
MANIFEST-000008    ← maps SST files to levels
LOG         32K    ← human-readable operational log
OPTIONS-000005     ← configuration snapshot
```

Three SSTable files, each 25 MB — the 75 MB total is the on-disk representation of ~95 MB logical data (some compaction savings even without compression). The `MANIFEST` file is the authoritative record of which SST belongs to which level.

### Experiment 7 — Overwrites

```
overwrite : 3.506 micros/op | 285,196 ops/sec | 31.6 MB/s
```
Overwriting the same key space as `fillrandom` achieves **285k ops/sec** — slightly lower than the original `fillrandom` because overwritten keys create multiple versions per key that compaction must merge and deduplicate. The key insight: **overwrites in RocksDB do NOT update in place** — they append a new entry with the same key. The old value becomes "tombstoned" and is only physically removed during compaction. This is what makes overwrites as fast as inserts, unlike B-tree systems where an overwrite must locate the existing page and modify it.

---

## 6. Key Learnings

1. **LSM trees convert all writes into sequential I/O.** Whether your keys are random or sequential, all disk writes are append-only (WAL) or large sequential flushes (SSTable). This is the fundamental reason LSM trees outperform B-trees on write-heavy SSD workloads at scale.

2. **Compaction is the price of write-optimized storage.** Writes are cheap because they defer work to background compaction. But compaction reads and rewrites data multiple times — write amplification of 10–30× is normal. Under sustained write load, compaction can fall behind and cause write stalls. Tuning compaction throughput is essential for production deployments.

3. **Bloom filters make point reads practical.** Without bloom filters, every point-lookup miss would require reading O(levels) SSTable files. With bloom filters, misses are resolved with a few memory operations. The trade-off is ~1% false-positive rate (occasional unnecessary SSTable read) vs. the memory cost of the filter.

4. **Reads are not free in LSM trees.** Sequential writes come at the cost of variable read complexity. A warm-cache hit is O(1) (bloom filter pass + block cache hit). A cold miss is O(L0_files + levels) — potentially reading blocks from many SSTables. B-trees always read O(log n) pages, making their read latency more predictable.

5. **Universal compaction shifts the knob between write amp and read amp.** Leveled compaction is "read-optimized LSM" (low read amp, high write amp). Universal compaction is "write-optimized LSM" (lower write amp, higher read amp). Choosing between them requires knowing your workload's read/write ratio.

6. **SSTables are immutable — that's the key abstraction.** Because SSTables are never modified after creation, multiple readers can access them concurrently without locking, snapshots are trivial (just record the set of SSTs visible at snapshot time), and crash recovery is simple (a partially written SSTable is just not added to the MANIFEST). Immutability buys lock-freedom and correctness almost for free.

---

## 7. References

- RocksDB wiki: https://github.com/facebook/rocksdb/wiki
- O'Neil, P. et al., *The Log-Structured Merge-Tree (LSM-Tree)* (1996)
- Facebook Engineering Blog — *RocksDB: Evolution of Development Priorities in a KV-Store Serving Large-Scale Applications* (2021)
- *RocksDB: A Persistent Key-Value Store for Flash Storage* (Dong et al., FAST 2021)
- LevelDB design notes: https://github.com/google/leveldb/blob/main/doc/impl.md
- Benchmark tool: `db_bench` — https://github.com/facebook/rocksdb/wiki/Benchmarking-tools
