# Topic 4: RocksDB Architecture

## Overview

RocksDB is a high-performance, embedded key-value store developed by Facebook (Meta), built as a fork of Google's LevelDB. Its foundational design choice is the **Log-Structured Merge Tree (LSM-Tree)** — a data structure engineered specifically for write-intensive workloads. Unlike traditional B-Tree engines that update data in place, RocksDB converts all writes into sequential disk operations, fundamentally trading some read complexity for extraordinary write throughput.

Understanding RocksDB means understanding *why* each component exists — not just what it does.

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        WRITE PATH                               │
│                                                                 │
│  Client Write                                                   │
│       │                                                         │
│       ▼                                                         │
│  ┌─────────┐    append    ┌──────────────────────────────────┐  │
│  │  WAL    │◄────────────│         Write Operation           │  │
│  │ (disk)  │             └──────────────────────────────────┘  │
│  └─────────┘                          │                         │
│                                       ▼                         │
│                           ┌──────────────────────┐             │
│                           │   Mutable MemTable   │  (64MB)     │
│                           │    (Skip List)        │             │
│                           └──────────┬───────────┘             │
│                              (full?) │                          │
│                                      ▼                          │
│                           ┌──────────────────────┐             │
│                           │ Immutable MemTable(s)│             │
│                           └──────────┬───────────┘             │
│                              flush   │                          │
└──────────────────────────────────────┼──────────────────────────┘
                                       │
┌──────────────────────────────────────▼──────────────────────────┐
│                        DISK (SSTable Levels)                    │
│                                                                 │
│  L0: [SST_a][SST_b][SST_c][SST_d]   ← overlapping key ranges  │
│            │                                                    │
│       compaction                                                │
│            ▼                                                    │
│  L1: [SST──────────────────────]    ← non-overlapping, sorted  │
│            │                                                    │
│       compaction                                                │
│            ▼                                                    │
│  L2: [SST──────────────────────────────────────────────────]   │
│            │                              (~10x larger per level)│
│           ...                                                   │
│  Ln: [SST──────────────────────────────────────── ... ──────]  │
│                                                                 │
│  Each SSTable contains: [Data Blocks | Index Block | Bloom Filter│
│                          | Footer]                              │
└─────────────────────────────────────────────────────────────────┘

                         READ PATH
                              │
              ┌───────────────▼────────────────┐
              │  1. Check Mutable MemTable      │
              │  2. Check Immutable MemTable(s) │
              │  3. Check L0 SSTables (ALL)     │  ← Bloom filter first
              │  4. Check L1 (at most 1 file)   │  ← Binary search on index
              │  5. Check L2 ... Ln             │
              └─────────────────────────────────┘
```

---

## Core Components

### 1. Write-Ahead Log (WAL)

**What it is:** A sequential, append-only log file on disk that records every write operation before it is applied to the MemTable.

**Why it exists:** The MemTable lives entirely in memory. If the process crashes or the machine loses power, all in-memory data is lost. The WAL solves this — on restart, RocksDB replays the WAL to reconstruct the MemTable state exactly as it was before the crash. This is the same principle used in PostgreSQL's WAL and MySQL's redo log.

**Key design choice:** WAL writes are *sequential* appends, which are fast even on spinning disks. The trade-off is that every write hits both the WAL and the MemTable — but since WAL writes are sequential, this cost is minimal.

**Durability modes:**
- **Synchronous (default):** Each write is followed by an `fsync`, guaranteeing the data survives a crash. Safest but slower.
- **Asynchronous:** Writes are buffered and flushed in batches. Higher throughput but risks data loss on crash.
- **Disabled:** WAL is bypassed entirely for maximum speed when data loss is tolerable.

**Connection to observed behavior:** When you see RocksDB performing well under crash-recovery tests, it is the WAL replaying and restoring state. When you tune `sync_log=false` and see throughput jump, you are trading away that safety.

---

### 2. MemTable (Mutable)

**What it is:** An in-memory, sorted data structure that absorbs all incoming writes. RocksDB's default implementation is a **skip list** — a probabilistic data structure that provides O(log n) insertions and lookups while maintaining sorted order.

**Why a skip list?** A hash map would be faster for point writes but cannot maintain sorted order, which is required for efficient SSTable flushing and range queries. A balanced BST (like a red-black tree) would also work but is harder to make concurrent. The skip list allows multiple readers and a single writer to operate concurrently with minimal locking.

**Capacity:** By default, the MemTable grows to 64 MB (`write_buffer_size`). This size is a direct lever on write throughput — larger MemTables absorb more writes before flushing, but consume more memory.

**What happens when it fills:** The full MemTable is *promoted* to an Immutable MemTable, and a fresh, empty MemTable is immediately created to accept new writes. This promotion is instant — no copying occurs, the pointer is simply frozen.

---

### 3. Immutable MemTable

**What it is:** A MemTable that has been sealed — it accepts no new writes, only reads. A background flush thread drains it to disk as an SSTable.

**Why have a separate immutable stage?** Without it, writes would have to pause every time a MemTable was being flushed to disk. The immutable stage acts as a buffer: writes continue into the new mutable MemTable, while the background thread independently flushes the immutable one. This decouples write ingestion from disk I/O.

**Multiple immutable MemTables:** RocksDB can queue several immutable MemTables simultaneously (configurable via `max_write_buffer_number`). If disk I/O is slow and flushes can't keep up, multiple immutable MemTables can pile up — eventually stalling writes. This is a real production concern in write-burst scenarios.

---

### 4. SSTables (Sorted String Tables)

**What they are:** Immutable, sorted files on disk. Each SSTable is produced by a MemTable flush or a compaction merge. Once written, an SSTable is never modified — only read or eventually deleted after compaction supersedes it.

**Internal structure of an SSTable:**
```
┌─────────────────────────────────┐
│  Data Blocks                    │  ← key-value pairs, sorted
│  (compressed, configurable)     │
├─────────────────────────────────┤
│  Index Block                    │  ← maps key ranges to block offsets
├─────────────────────────────────┤
│  Bloom Filter Block             │  ← probabilistic membership test
├─────────────────────────────────┤
│  Footer / Metadata              │
└─────────────────────────────────┘
```

**Why immutable?** Immutability makes SSTables safe to read concurrently without locks, simplifies crash recovery (partial writes are detected and discarded), and enables efficient caching.

**Level 0 (L0) is special:** SSTables flushed directly from MemTables land at L0 and *may have overlapping key ranges* with each other. This is the only level where overlap is permitted. Reads at L0 must consult every file in the level, making L0 the most expensive level for reads. This is also why RocksDB compacts L0 aggressively.

**L1 through Ln:** These levels are organized so that SSTables within a level have *non-overlapping, sorted* key ranges. A read at any of these levels requires checking at most *one* SSTable. Each level is roughly 10× larger than the previous one (the "size ratio" or "fanout").

---

### 5. Bloom Filters

**What they are:** Space-efficient probabilistic data structures that answer the question: *"Is this key definitely NOT in this SSTable?"*

**How they work:** A Bloom filter for an SSTable hashes each key through multiple hash functions and sets bits in a bit array. To check if a key exists, the same hash functions are applied. If any corresponding bit is 0, the key is *definitely* absent. If all bits are 1, the key is *probably* present (false positives are possible, false negatives are not).

**Why they matter for reads:** Without Bloom filters, a point lookup for a non-existent key would have to open and scan every SSTable across every level. With Bloom filters, this becomes nearly free — the filter is loaded into memory and checked in microseconds before any disk I/O is attempted. This is the primary mechanism by which RocksDB achieves reasonable read performance despite its LSM structure.

**The false positive rate** is configurable (typically 1%). A lower false positive rate requires more bits per key but reduces unnecessary disk reads. RocksDB stores a Bloom filter per SSTable at every level except optionally L0.

**Bloom filters do NOT help range scans.** For a range query, you must read actual data blocks — the filter cannot say "all keys from A to B are absent." This is a fundamental limitation and explains why LSM systems are weaker for range-scan-heavy workloads compared to B-Trees.

---

### 6. Compaction

**What it is:** A background process that merges multiple SSTables, removes deleted or overwritten entries, and reorganizes data into deeper, larger levels.

**Why it is necessary:**
- Without compaction, SSTables accumulate indefinitely at L0.
- Reads would degrade over time (more files to check).
- Deleted keys (tombstones) and overwritten values would waste space permanently.
- Key ranges would become fragmented and unordered.

**What happens during compaction:** RocksDB selects one or more SSTables from level Ln-1, finds all overlapping SSTables at level Ln, performs a sorted merge (like merge-sort), discards stale versions of keys and tombstones, and writes new SSTables at level Ln. The input files are then deleted.

#### Compaction Strategies

**Leveled Compaction (default):**
- Each level (L1+) is a single sorted run partitioned across multiple files with non-overlapping key ranges.
- When a level exceeds its size target, one SSTable is selected and merged *only* with the overlapping SSTables in the next level.
- **Result:** Low read amplification (at most 1 file per level for a point lookup), moderate write amplification.
- **Write amplification example:** With 4 levels and fanout 10: `1 (flush) + 2 (L0→L1) + 10 (L1→L2) + 10 (L2→L3) + 10 (L3→L4) = ~33×`. Every byte written by the application is physically written to disk ~33 times in total across all levels.

**Universal (Tiered) Compaction:**
- Multiple sorted runs are allowed to accumulate within a level.
- Compaction merges entire sorted runs rather than selectively merging by key range.
- **Result:** Much lower write amplification, but higher read amplification (must check more files per read) and higher space amplification (duplicate data exists across runs).
- **When to use:** Write-dominated workloads where read latency is less critical, and when SSD lifespan (affected by write amplification) is a concern.

**FIFO Compaction:**
- Old SSTables are simply deleted when total size exceeds a threshold.
- No merging. Useful for time-series or hot-cold data where old data is simply expired.

---

## Write Path — Step by Step

```
Client calls Put(key, value)
         │
         ▼
1. Encode the operation as a WriteBatch
         │
         ▼
2. Append to WAL (fsync if sync mode)         ← Durability guaranteed here
         │
         ▼
3. Insert into Mutable MemTable (skip list)   ← Acknowledged to client
         │
         ▼
4. If MemTable is full:
     - Freeze current MemTable → Immutable MemTable
     - Allocate new Mutable MemTable
         │
         ▼
5. Background thread: Flush Immutable MemTable → L0 SSTable
         │
         ▼
6. Background thread: If L0 file count threshold exceeded,
   compact L0 → L1, and cascade if L1 exceeds size target
```

**Why LSM writes are fast:** Steps 2 and 3 are the only ones on the critical (foreground) path. Both are sequential memory/disk operations. All the heavy disk work (flush, compaction) happens asynchronously in the background.

---

## Read Path — Step by Step

```
Client calls Get(key)
         │
         ▼
1. Check Mutable MemTable                    ← Most recent writes here
         │ not found
         ▼
2. Check Immutable MemTable(s)               ← Recently sealed, not yet flushed
         │ not found
         ▼
3. For each SSTable in L0 (newest first):
     a. Check Bloom Filter → if ABSENT, skip file entirely
     b. If possibly present → Binary search on Index Block → Read Data Block
         │ not found in any L0 file
         ▼
4. For L1, L2, ... Ln:
     a. Binary search the level's key range to identify the ONE relevant SSTable
     b. Check Bloom Filter
     c. If possibly present → Read Data Block
         │
         ▼
5. Return the most recent version found (or "not found")
```

**Read performance characteristics:**
- **Best case (key in MemTable):** Microseconds, pure memory lookup.
- **Typical case (key in L1-Ln):** One Bloom filter check + one block read per level (usually 1–2 disk reads total).
- **Worst case (non-existent key, no Bloom filter):** Must probe every level. Bloom filters reduce this to near-zero disk I/O.
- **Range scans:** Must read and merge data from MemTable + all relevant SSTables across levels. Bloom filters do not help here.

---

## The Three Amplification Factors

These are the core trade-off axes in LSM design. Every tuning decision is a movement along these axes.

| Factor | Definition | LSM Impact | B-Tree Impact |
|---|---|---|---|
| **Write Amplification (WA)** | Bytes written to disk / bytes written by client | High (~10–30×) due to compaction rewriting data repeatedly | Low (~1–2×), in-place updates |
| **Read Amplification (RA)** | Disk reads per logical read | Moderate; Bloom filters reduce it greatly | Low; single tree traversal |
| **Space Amplification (SA)** | Disk space used / actual live data size | Moderate (~1.1× with leveled) to High (~2× with tiered during compaction) | Low; no duplicate data |

**The fundamental tension:** You cannot minimize all three simultaneously. Every compaction strategy is a different point in this trade-off space:
- **Leveled compaction:** Low RA + Low SA, High WA
- **Tiered/Universal compaction:** Low WA, High RA + High SA
- **FIFO:** Zero WA/SA (just delete), but worst RA and data loss by design

---

## Why LSM Trees Are Optimized for Writes

Traditional B-Trees update data **in place**. Writing a single key-value pair may require:
1. Reading the relevant page from disk into memory.
2. Modifying the page.
3. Writing the modified page back to disk (random write).
4. Possibly updating parent pages and rebalancing — more random writes.

Random writes are expensive because disks (even SSDs) handle sequential I/O far better than random I/O. B-Trees generate a random write for every update.

LSM Trees convert **all** writes into sequential operations:
- The WAL is sequential append.
- The MemTable is in-memory (no disk I/O for the write itself).
- SSTable flushes are large sequential writes of a complete, sorted file.
- Compaction is sequential read + sequential write.

This design means RocksDB can sustain write throughputs of hundreds of MB/s even on commodity SSDs, while a B-Tree engine would saturate at much lower rates due to random I/O overhead.

**The cost of this design:** Reads are more complex (must check multiple files, multiple levels), and compaction is an ongoing background cost that consumes CPU and disk I/O. RocksDB accepts this cost in exchange for write performance.

---

## Why Compaction Is Required

Without compaction:
1. **L0 files pile up:** Each flush creates a new L0 SSTable. Since L0 files may have overlapping key ranges, reads must scan all of them. Reads slow down proportionally to the number of L0 files.
2. **Tombstones (deletes) linger:** A delete in an LSM system does not remove data — it writes a *tombstone* marker. Without compaction, tombstones and the original entries they delete coexist forever, wasting space and polluting reads.
3. **Stale versions accumulate:** A key updated 100 times exists in 100 SSTable entries across levels. Only the newest version is valid; the rest are dead weight. Space usage grows without bound.
4. **Read performance degrades:** The more SSTables that exist, the more files a read must potentially examine. Bloom filters help, but they cannot eliminate the problem entirely when hundreds of files accumulate.

Compaction is essentially **garbage collection** for the LSM structure — mandatory housekeeping that makes long-term operation viable.

---

## Read Performance Trade-offs

| Scenario | Performance | Reason |
|---|---|---|
| Point lookup, key in MemTable | Excellent | Pure memory, O(log n) skip list |
| Point lookup, key exists on disk | Good | Bloom filter + 1 block read per level |
| Point lookup, key does NOT exist | Good (with Bloom filters) | Bloom filter rejects most files; only rare false positives cause disk I/O |
| Short range scan | Moderate | Must read and merge across MemTable + multiple SSTables; no Bloom filter help |
| Long range scan | Poor vs B-Tree | Must merge many SSTables across levels; B-Tree is superior for this |
| Read during heavy compaction | Variable | Compaction consumes I/O bandwidth, causing latency spikes |

---

## Storage Efficiency Trade-offs

| Configuration | Space Amplification | Write Amplification | Best Use Case |
|---|---|---|---|
| Leveled compaction | ~1.1× (close to actual data size) | High (~30×) | Read-heavy or mixed workloads |
| Universal (tiered) compaction | ~2× during compaction | Low (~10×) | Write-heavy workloads, SSD life-span sensitive |
| Aggressive compression (Zstd) at lower levels | Lower total | CPU overhead ↑ | Cost-sensitive storage, cold data |
| FIFO compaction | Minimal (TTL-based) | Near zero | Time-series, ephemeral data |

---

## Connecting Architecture to Observed Behavior

| Observation | Architectural Explanation |
|---|---|
| RocksDB handles write bursts far better than MySQL InnoDB | Writes go to MemTable (memory); InnoDB must random-write B-Tree pages to disk |
| Latency spikes during heavy write periods | Compaction consuming disk I/O bandwidth; background and foreground I/O compete |
| Read latency is non-deterministic | Depends on which level the key resides in, current L0 file count, and compaction state |
| Disk usage is higher than actual data size | Space amplification from duplicate data during compaction; tombstones not yet purged |
| Recovery after crash is fast | WAL replay reconstructs MemTable state; SSTables are immutable and need no repair |
| `db_bench` write throughput far exceeds read throughput | By design — LSM is write-optimized; sequential writes vs. multi-level reads |

---

## Analysis: Trade-offs RocksDB's Engineers Accepted

1. **Write amplification is the price of organized reads.** Without compaction (and its write amplification), reads would degrade indefinitely. Engineers at Facebook accepted ~30× write amplification to keep read latency bounded. For an SSD writing 500 MB/s of application data, this means ~15 GB/s of actual disk writes — a significant cost.

2. **Bloom filters are a space vs. read-speed trade-off.** A Bloom filter consumes ~10 bits per key at 1% false-positive rate. For a database with 10 billion keys, that's ~12 GB just for Bloom filters. Engineers chose to pay this memory cost because the alternative (disk I/O for every read miss) is far more expensive.

3. **Sequential I/O over random I/O, always.** Every design choice — WAL, MemTable flushes, compaction — is oriented around sequential disk access. This was the correct bet: both HDDs (where random I/O is penalized by seek time) and SSDs (where random I/O causes write amplification at the device firmware level) benefit from sequential access patterns.

4. **Compaction complexity is acceptable for production-grade write throughput.** Managing background compaction, rate limiting, priority queues, and compaction filters adds significant engineering complexity. Facebook accepted this complexity because the alternative — a B-Tree engine — could not sustain the write rates required for their workloads.

---

## Suggested Questions — Answered

**Why are LSM trees preferred in write-heavy workloads?**

LSM trees convert all writes to sequential operations (WAL append + MemTable insert). The actual disk persistence happens asynchronously via background flushes, which are large sequential writes. B-Trees, by contrast, must perform random writes to update in-place, which are limited by IOPS. For write-heavy workloads like logging, time-series ingestion, or event streaming, this difference is orders of magnitude.

**Why can compaction become expensive?**

Compaction reads all input SSTables, merges them in sorted order, and writes new SSTables — all on disk. At deep levels where SSTables are large (e.g., L3 at 50 GB+), compaction can consume hundreds of MB/s of disk bandwidth for extended periods. This competes directly with foreground reads and writes, causing latency spikes. Additionally, with a fanout of 10, compacting L3 → L4 requires reading and rewriting ~10× as much data as the input, so a single compaction event at a deep level can write tens of gigabytes. Engineers mitigate this with rate limiting and I/O priority scheduling, but cannot eliminate it.

**How do Bloom Filters improve read performance?**

Without Bloom filters, a point lookup for a non-existent key requires opening every SSTable file at every level, loading index blocks, and performing binary searches — potentially hundreds of disk reads. A Bloom filter allows RocksDB to answer "this key is definitely not here" with a single memory operation (microseconds, no disk I/O). Since production databases frequently query for keys that don't exist (e.g., cache-miss scenarios, existence checks), Bloom filters convert what would be worst-case read amplification into near-constant-time operations. The only cost is the occasional false positive (~1%), which causes one unnecessary disk read.

---

## References

- [RocksDB Official Wiki — Compaction](https://github.com/facebook/rocksdb/wiki/Compaction)
- [RocksDB Tuning Guide](https://github.com/facebook/rocksdb/wiki/RocksDB-Tuning-Guide)
- [Optimizing Space Amplification in RocksDB — CIDR 2017 (Facebook)](https://www.cidrdb.org/cidr2017/papers/p82-dong-cidr17.pdf)
- [Constructing and Analyzing the LSM Compaction Design Space — VLDB](https://vldb.org/pvldb/vol14/p2216-sarkar.pdf)
- [Deep Dive into RocksDB's LSM-Tree Architecture — MinervaDB](https://minervadb.xyz/deep-dive-into-rocksdbs-lsm-tree-architecture/)
- [The Fundamentals of RocksDB — GetStream](https://getstream.io/blog/rocksdb-fundamentals/)
