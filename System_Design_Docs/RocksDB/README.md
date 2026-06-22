# RocksDB: Architecture Analysis & System Design Deep Dive

**Course:** Advanced Database Management Systems
**Author:** Talin Daga
**Topic:** Storage Engine Internals — Log-Structured Merge Trees

---

## 1. Problem Background

### 1.1 Why Facebook Built RocksDB

By 2012–2013, Facebook's infrastructure faced a storage inflection point. Services like the social graph, feed ranking, and Messenger were generating billions of key-value write operations per day across tens of thousands of servers. The dominant storage paradigm at the time — relational databases backed by B-Tree storage engines (MySQL/InnoDB) — was struggling to keep pace, not because of a flaw in relational theory, but because of a fundamental architectural mismatch with the hardware environment and the access pattern of these workloads.

Facebook's engineers observed two critical shifts:

- **The rise of flash SSDs as primary server storage.** SSDs offer dramatically lower random-read latency than spinning disks (~100µs vs. ~10ms), but they have an important asymmetry: random writes are far more expensive than sequential writes due to erase-before-write mechanics and wear leveling. An engine that minimizes random writes and favors sequential I/O is ideally suited for SSDs, even compared to one that minimizes the total number of I/O operations.
- **Write-heavy, server-side workloads.** Social network use cases generate disproportionately more writes than reads. Every user action (like, post, message read receipt) produces multiple writes. The read-to-write ratio in these backends is often inverted relative to traditional OLTP systems.

RocksDB was forked from Google's LevelDB in 2012 to address these needs. The core thesis was: **design a storage engine that maximizes write throughput on flash storage, accepts tunable read-path trade-offs, and is deeply embeddable within application processes** — rather than running as a standalone database server.

### 1.2 Limitations of B-Tree Engines Under Write-Heavy Workloads

Traditional B-Tree based engines (InnoDB, PostgreSQL's heap storage) are architected around **in-place updates**. To understand why this becomes a bottleneck, consider the write path of a single row update in InnoDB:

1. The dirty page is located in the buffer pool (or fetched from disk if not cached).
2. The undo log is written to preserve the old value for MVCC.
3. The redo log (WAL) is written for crash recovery.
4. The actual page in the B-Tree is modified in memory.
5. Eventually, the dirty page is flushed back to its original on-disk location.

This model has several structural inefficiencies at high write throughput:

- **Random write amplification.** Each update touches a specific page at a fixed location on disk. Under sustained write pressure, the B-Tree's page cache fills with dirty pages, and the background flushing process generates a storm of random I/Os — the worst possible access pattern for both SSDs and spinning disks.
- **Write-lock contention on B-Tree nodes.** Insertions and deletions require structural modifications (splits, merges, rotations) that require locking tree nodes, serializing concurrent writers on hot key ranges.
- **The double-write buffer problem.** InnoDB writes pages twice (once to the double-write buffer, once to the actual tablespace) to prevent partial-page corruption on crash. This halves effective write throughput.
- **Page fragmentation under random insertion patterns.** B-Tree pages become under-utilized as data is inserted in non-sequential key order, reducing space efficiency and increasing I/O for equivalent data volumes.

The root cause is that **B-Trees are optimized for read performance and in-place mutability**, which requires that data always exist at a predictable, indexed location on disk. Under write-heavy workloads, the overhead of maintaining this guarantee — at the cost of random I/O — becomes the primary bottleneck.

An **LSM-Tree (Log-Structured Merge Tree)**, first formalized by O'Neil et al. in 1996, addresses this by inverting the trade-off: it converts all writes into sequential I/O at the cost of deferring the work of organizing data into a background compaction process. RocksDB is a highly engineered implementation of this idea, tuned for modern multi-core hardware and flash storage.

---

## 2. Architecture Overview

### 2.1 High-Level Data Flow

RocksDB's architecture follows a two-phase strategy: **absorb writes quickly in memory, then drain them to disk sequentially**.

```
Write (key, value)
        |
        v
  [Write Ahead Log (WAL)]  <-- sequential append to disk (crash durability)
        |
        v
   [MemTable]              <-- in-memory sorted structure (fast random writes)
        |
  (when full, ~64MB)
        |
        v
 [Immutable MemTable]      <-- frozen, pending flush
        |
        v
   [SSTable L0]            <-- flushed to disk as a sorted file
        |
  (background compaction)
        |
        v
   [SSTable L1 → Ln]       <-- progressively merged, sorted, larger levels
```

Reads traverse this structure in reverse: MemTable first (most recent data), then L0 SSTables, then L1 through Ln. The first matching key wins, implementing a "latest-write-wins" semantic without requiring a full scan.

### 2.2 Core Components

#### MemTable

The MemTable is an **in-memory, mutable, sorted data structure** that absorbs all incoming writes. It serves as the write buffer for the engine. All writes — inserts, updates, and deletes — are appended to the MemTable without touching the on-disk structure. Deletes are represented as **tombstone records** (a key with a deletion marker), not as an actual removal, since the old value may still exist in lower SSTable levels.

Once the MemTable reaches a configurable size threshold (default 64 MB), it is promoted to an **Immutable MemTable** and a new MemTable is initialized to absorb subsequent writes. The immutable MemTable is then asynchronously flushed to disk as an L0 SSTable. Multiple immutable MemTables may exist simultaneously if flushes lag behind the write rate.

The MemTable data structure is **pluggable** (discussed in detail in Section 3). The default implementation is a **SkipList**, which provides O(log n) inserts and O(log n) point lookups, and supports efficient range scans — a critical requirement for iterators.

#### Write Ahead Log (WAL)

Before any write is applied to the MemTable, it is **first appended sequentially to the Write Ahead Log on disk**. The WAL is a flat, append-only file. Because it is purely sequential, WAL writes are extremely fast even on spinning disks, and near-zero-cost on SSDs.

The WAL's sole purpose is **crash recovery**. If the process crashes before the MemTable is flushed to an SSTable, the in-memory data would be lost. On restart, RocksDB replays the WAL to reconstruct the MemTable and recover to a consistent state. Once a MemTable is successfully flushed to an L0 SSTable (a durable file), the corresponding WAL segment is no longer needed and is deleted.

This design separates **durability** (WAL) from **read performance** (SSTables), allowing each to be optimized independently.

#### SSTables (Sorted String Tables)

An SSTable is an **immutable, sorted, on-disk file** produced by flushing a MemTable. "Immutable" is the critical property: once written, an SSTable is never modified in-place. This eliminates the random-write problem entirely. Mutations are instead handled by writing a new, more recent entry — the merge process (compaction) reconciles versions later.

Internally, each SSTable contains:
- **Data blocks:** Fixed-size blocks (default 4KB) containing sorted key-value pairs.
- **Index block:** A sparse index over the data blocks, enabling binary search to locate the correct data block for a given key.
- **Filter block:** A Bloom filter (or other probabilistic filter) that allows the read path to skip the SSTable entirely if the key definitely doesn't exist within it.
- **Metadata blocks:** Compression type, checksums, table properties.

SSTables are organized into **levels** (L0, L1, ..., Lmax), where each level has a progressively larger total capacity, and levels L1 and above enforce a **non-overlapping key range invariant** — at most one SSTable at any given level contains a specific key.

---

## 3. Internal Design

### 3.1 LSM-Tree Storage Levels: L0 to Ln

The multi-level structure of RocksDB's LSM-tree is the architectural mechanism that converts random writes into sequential I/O and manages the long-term organization of data.

**Level 0 (L0):**
- L0 is populated directly by flushing MemTables to disk.
- **Critically, L0 SSTables may have overlapping key ranges.** If three successive MemTables each contained key "user:42", all three L0 SSTables will contain that key. This means reads at L0 must consult *all* L0 files (up to `level0_file_num_compaction_trigger`, default 4).
- L0 is a transient buffer between in-memory and the organized lower levels. RocksDB triggers compaction when the number of L0 files exceeds the configured threshold.

**Level 1 (L1):**
- Total data size in L1 is bounded (default ~256 MB).
- **Key ranges across all L1 SSTables are non-overlapping and contiguous.** A given key can exist in at most one L1 SSTable.
- This invariant, maintained by compaction, means a point lookup at L1 requires at most one SSTable read (plus a Bloom filter check). This is the primary read optimization for deeper levels.

**Levels L2 to Lmax:**
- Each level is typically **10x larger** than the level above it (the `max_bytes_for_level_multiplier`).
- The non-overlapping invariant is maintained throughout.
- Most data resides in the deepest level, making the total storage amplification factor approximately `1 / (1 - 1/multiplier)` ≈ 1.11 for a multiplier of 10 — theoretically close to 1x, in practice slightly higher due to level overlap during compaction.

The consequence of this design: **writes are fast because they always go to a sequential destination (WAL, then MemTable, then L0 flush). The cost of organizing data is deferred and amortized across background compaction.**

### 3.2 Bloom Filters and Read Path Optimization

Without Bloom filters, a point lookup for a key that doesn't exist in the database would require scanning every SSTable at every level — a catastrophic I/O cost. Bloom filters solve this with a probabilistic shortcut.

A **Bloom filter** is a compact, in-memory bitset associated with each SSTable. When the SSTable is written, every key in the file is hashed through `k` independent hash functions, each setting a bit in the filter. On a lookup:

1. Hash the query key through the same `k` functions.
2. Check if all `k` bits are set.
3. **If any bit is unset: the key definitely does NOT exist in this SSTable.** The SSTable is skipped.
4. **If all bits are set: the key probably exists** (false positive possible). Read the data block to confirm.

The false positive rate is controlled by `bits_per_key` (default 10 bits per key yields ~1% false positive rate). The critical insight is that **the cost of a false negative (a key incorrectly reported as missing) is zero** in RocksDB's model, because data at higher levels is definitively more recent. A false positive only costs one unnecessary SSTable I/O — still far better than consulting every file.

In practice, with 10 bits/key Bloom filters, the vast majority of non-existent key lookups require zero disk I/O beyond the in-memory filter check, making negative lookups essentially free. This is especially important for write-heavy workloads where point reads for non-existent keys are common (e.g., checking if a cache entry exists before inserting).

### 3.3 Compaction: The Background Workhorse

Compaction is the process by which RocksDB reorganizes data from shallower levels into deeper, more organized levels. It is fundamental to correctness (garbage-collecting obsolete versions and tombstones) and performance (maintaining the read-path invariants that make lookups efficient).

**Why compaction is necessary:**

- **Space reclamation:** Every update writes a new version of the key. The old version occupies space in a lower SSTable. Without compaction, storage would grow unboundedly for any updated key.
- **Tombstone application:** A delete writes a tombstone. The tombstone must be merged down to the level where the original key resides and both must be discarded. Without this, deleted keys continue to consume space forever, and reads must carry tombstones across all levels to prove a key is deleted.
- **Read performance restoration:** As L0 accumulates files with overlapping key ranges, read latency degrades (more files to check per lookup). Compaction merges these into L1 with non-overlapping ranges, restoring O(1) SSTable reads per level.

**How Level Style Compaction works:**

1. A compaction job picks one SSTable from level N (typically the largest or oldest file) and identifies all SSTables at level N+1 whose key ranges overlap with it.
2. It reads all selected files, performs a **k-way merge sort** on their key-value pairs, resolves version conflicts (newer key wins, tombstones discard old versions), and writes the merged result as one or more new SSTables at level N+1.
3. The input SSTables (from both levels N and N+1) are deleted atomically after the new N+1 files are durable.
4. This process propagates down the levels as needed, eventually settling in the largest level.

**Pluggable MemTable Implementations:**

The MemTable abstraction in RocksDB is not tied to a single data structure. The default and most common implementation is the **SkipList**, which provides probabilistic O(log n) inserts, lookups, and iteration with good cache efficiency under concurrent access. However, RocksDB supports alternative implementations:

- **Vector MemTable:** Stores key-value pairs in an unsorted vector. Insertions are O(1) amortized, but iteration requires a full sort on flush. Useful for bulk-loading scenarios where write order is already sorted by the application.
- **HashSkipList / HashLinkedList (Prefix Hash):** Partitions keys by prefix into a hash table, with each bucket containing a SkipList or linked list. This dramatically accelerates prefix lookups (O(1) bucket lookup + O(log n) within bucket) at the cost of making full-range scans less efficient. Useful for workloads with heavy prefix-range queries (e.g., all keys prefixed with `user:<id>:`).

The pluggability reflects RocksDB's design philosophy: **no single data structure is universally optimal; the MemTable should be chosen based on the workload's access pattern.**

---

## 4. Design Trade-Offs

### 4.1 The Amplification Triangle

Every storage engine design must navigate a fundamental three-way tension between three amplification factors. These cannot all be minimized simultaneously — reducing one necessarily increases one or both of the others.

**Write Amplification (WA):** The ratio of bytes physically written to disk divided by the logical bytes written by the application. In RocksDB with Level Style Compaction, a single logical byte may be written 10–30 times by the time it is compacted from L0 down to the final level (L1→L2→...→Lmax). High WA increases SSD wear and consumes I/O bandwidth, potentially limiting throughput.

**Read Amplification (RA):** The number of disk reads required to satisfy a single logical read. In the worst case (key not in MemTable, not in cache, not caught by any Bloom filter), RocksDB must check L0 (all files, since ranges overlap) plus one SSTable per lower level. With default settings, this can be 5–10 SSTable reads. Bloom filters reduce this dramatically in practice, but RA is inherently higher in LSM-trees than in B-Trees.

**Space Amplification (SA):** The ratio of disk space consumed to the actual logical data size. RocksDB's SA is driven by the coexistence of multiple versions of the same key across levels, and the temporary space required during compaction (old files are not deleted until new files are safely written). In practice, Level Style Compaction achieves SA of roughly 1.1–1.5x. Universal Style Compaction can reach 2x during compaction cycles.

```
┌─────────────────────────────────────────────────────┐
│              The Amplification Triangle              │
│                                                      │
│          Write Amplification (WA)                    │
│                   /\                                 │
│                  /  \                                │
│                 /    \                               │
│                /      \                              │
│  Read Amp.    /________\   Space Amp.                │
│   (RA)                        (SA)                  │
│                                                      │
│  Level Style:   High WA,  Low SA,  Moderate RA       │
│  Universal:     Low WA,   High SA, Moderate RA       │
│  FIFO:          Lowest WA, Highest SA, Highest RA    │
│  B-Tree:        Moderate WA, Lowest RA, Lowest SA    │
└─────────────────────────────────────────────────────┘
```

**No free lunch exists.** Tuning RocksDB is fundamentally the act of deciding which amplification dimension to sacrifice, given the workload's bottleneck: I/O bandwidth (WA matters most), latency-sensitive reads (RA matters most), or storage cost (SA matters most).

### 4.2 Compaction Styles: Level vs. Universal vs. FIFO

#### Level Style Compaction (default)

In Level Style Compaction (also known as "Leveled"), each level has a bounded total size, and files within each level cover non-overlapping key ranges. Compaction is triggered when a level exceeds its size limit and merges files from that level into the next.

- **Write Amplification:** High. A key written to L0 may be rewritten ~10 times as it migrates from L0→L1→L2→...→Lmax. Total WA ≈ `O(num_levels * level_multiplier)`. For a 6-level DB with 10x multiplier, WA can reach 30–50x in the worst case.
- **Read Amplification:** Low. Because key ranges are non-overlapping at L1 and below, a point lookup hits at most one SSTable per level. With Bloom filters, most lookups are answered in memory or with 1–2 disk reads.
- **Space Amplification:** Low (~1.1x for data at rest). Obsolete versions are garbage-collected aggressively during compaction.
- **Best for:** Read-heavy or mixed workloads where read latency and storage efficiency are prioritized. This is the recommended style for production systems with predictable read SLAs.

#### Universal Style Compaction

Universal Compaction (also known as "Tiered" in the broader literature) does not enforce per-level size limits. Instead, it sorts all SSTable files by time and merges groups of "similarly-sized" files together. Files are only merged when the total size ratio between the smallest files and all other files exceeds a threshold.

- **Write Amplification:** Low. Files are merged only when the size ratio justifies it, meaning each byte is rewritten far fewer times — approximately `O(num_sorted_runs)` ≈ 5–10x compared to Level Style's 30–50x.
- **Read Amplification:** Moderate. Because multiple sorted runs can exist simultaneously (unlike Level Style's strictly one file per key range per level), reads may need to consult more files. Bloom filters mitigate this but cannot eliminate it entirely.
- **Space Amplification:** High. During a full compaction cycle, the engine may temporarily hold both the input and output files, requiring up to 2x the final data size in disk space. This is a hard operational constraint that can cause compaction to stall if disk space is insufficient.
- **Best for:** Workloads that are extremely write-heavy and can tolerate periodic spikes in storage usage. Common in time-series or log ingestion pipelines where write throughput is the primary constraint.

#### FIFO (First-In, First-Out) Compaction

FIFO is the simplest strategy: no merging occurs. Files are simply evicted (deleted) from the oldest end when total storage exceeds a configured limit. No version reconciliation, no tombstone application.

- **Write Amplification:** Effectively 1x — data is written once and never rewritten by compaction.
- **Read Amplification:** Very high. With no compaction, the number of SSTables grows without bound until eviction, and reads must check all of them.
- **Space Amplification:** Controlled by the total size limit, but correctness is sacrificed: data is deleted based on age, not on whether it is still logically needed.
- **Best for:** Time-series data with a natural TTL, where only recent data is queried and old data is expected to expire. Not suitable for general-purpose databases.

### 4.3 LSM-Tree Write Optimization vs. B-Tree Read Optimization

The LSM vs. B-Tree dichotomy is not about which is "better" — it is about which access pattern is being optimized.

| Property | RocksDB (LSM-Tree) | InnoDB (B-Tree) |
|---|---|---|
| **Write Path** | Sequential: WAL append + MemTable insert. O(1) effective cost. | Random: locate page in buffer pool, modify in-place, write WAL. O(log n) + random I/O. |
| **Read Path (point)** | MemTable → L0 → L1...Ln. O(levels) with Bloom filters. | Binary search of B-Tree. O(log n), predictable, 1–3 page reads. |
| **Read Path (range)** | Merge iterator across MemTable + all SSTables. Excellent for sequential ranges. | B-Tree leaf traversal. Excellent for clustered ranges. |
| **Write Amplification** | High (Level Style: 10–50x) | Moderate (B-Tree splits + WAL: 5–10x) |
| **Read Amplification** | Moderate (1–10 SSTable reads, Bloom-filtered) | Low (1–3 page reads for cached B-Tree) |
| **Space Amplification** | Moderate (1.1–2x, version coexistence) | Low (in-place updates, minimal extra space) |
| **Concurrency** | Lock-free reads (immutable SSTables) + fine-grained MemTable locking | Page-level or row-level locking (complex MVCC) |
| **SSD Efficiency** | High (sequential writes dominant) | Moderate (random writes on dirty page flush) |
| **Predictable Latency** | Lower tail latency for writes; compaction can cause read spikes | More predictable read latency; write stalls under heavy load |

**The fundamental insight:** InnoDB stores data where it will ultimately live and pays the cost of random I/O to keep it organized at write time. RocksDB stores data where it is cheapest to write and pays the cost of background compaction to organize it later. For workloads where writes dominate and reads can tolerate slightly higher average latency (but still benefit from Bloom filter acceleration), RocksDB's trade-off is decisively superior.

---

## 5. Experiments / Observations

### 5.1 Theoretical Analysis Using `db_bench`

RocksDB ships with `db_bench`, a benchmarking tool that allows engineers to measure engine performance under controlled, repeatable conditions. The following analysis describes what one would observe when running `db_bench` to compare Level Style and Universal Style Compaction under a write-heavy workload.

**Experimental Setup (theoretical):**

```bash
# Level Style Compaction baseline
./db_bench \
  --benchmarks=fillrandom,stats \
  --num=50000000 \
  --value_size=1024 \
  --write_buffer_size=67108864 \
  --compaction_style=0 \               # 0 = Level Style
  --max_bytes_for_level_base=268435456 \
  --statistics

# Universal Style Compaction
./db_bench \
  --benchmarks=fillrandom,stats \
  --num=50000000 \
  --value_size=1024 \
  --write_buffer_size=67108864 \
  --compaction_style=1 \               # 1 = Universal Style
  --statistics
```

### 5.2 Predicted Observations: Write Amplification

When comparing both compaction styles under a random-write workload of 50 million key-value pairs (each 1KB value):

**Level Style Compaction — Expected Behavior:**

- **Write throughput:** Approximately 80,000–120,000 ops/sec during steady state on NVMe SSD.
- **Write stall events:** Visible in statistics output (`rocksdb.write.stall`) as L0 accumulates files faster than compaction can drain them. These stalls represent periods where the write rate is artificially throttled to prevent L0 from growing unboundedly.
- **Write amplification factor (observed via `rocksdb.compact.write.bytes` / logical bytes written):** Expected to be in the range of **20–40x** for a 6-level configuration with 10x level multiplier. Each byte written by the application is physically rewritten ~25 times by background compaction across all levels.
- **Compaction I/O:** Sustained background write bandwidth of 200–500 MB/s observed throughout the benchmark, even after the foreground write rate completes.

**Universal Style Compaction — Expected Behavior:**

- **Write throughput:** Approximately 150,000–250,000 ops/sec — a **1.5–2x improvement** in raw write throughput compared to Level Style under identical conditions.
- **Write stall events:** Significantly fewer stalls during the early phase of the benchmark. However, when a full-compaction cycle triggers (merging all sorted runs into one), a temporary stall of several seconds may occur as the engine waits for the large merge to complete.
- **Write amplification factor:** Expected to be in the range of **5–12x** — a **3–4x reduction** compared to Level Style Compaction.
- **Space usage:** During a full compaction sweep, temporary disk usage would spike to approximately **1.8–2x** the logical data size, as the engine holds both input and output files simultaneously before deleting inputs.

**Key Takeaway from the Comparison:**

The `db_bench` comparison would confirm the amplification triangle empirically. Universal Compaction does not eliminate I/O — it redistributes it. The total compaction I/O over the entire benchmark duration would be lower for Universal Style, but that I/O arrives in larger, less frequent bursts rather than as a steady, predictable stream. For a production system with latency SLAs, the burstiness of Universal Compaction's I/O profile can be more disruptive than the steady background pressure of Level Style, even if the aggregate amplification is lower.

This is why RocksDB exposes `max_compaction_bytes`, `compaction_readahead_size`, and dedicated `max_background_compactions` thread pool controls — to allow operators to shape this I/O behavior to match their infrastructure's I/O budget.

---

## 6. Key Learnings

### 6.1 LSM-Trees Reframe the Write Problem as a Scheduling Problem

The most important architectural insight from studying RocksDB is that **LSM-trees do not eliminate write I/O — they defer and batch it**. A B-Tree pays the cost of organization immediately, at write time, in the form of random I/O. An LSM-tree pays the same cost later, in background compaction, as sequential I/O. The total physical I/O may actually be higher in an LSM-tree (write amplification), but by converting random I/O to sequential I/O, the throughput achievable on flash storage is dramatically higher. This reframes the storage engine design problem from "minimize total I/O" to "optimize I/O access patterns for the hardware."

### 6.2 Background Compaction Thread Management Is a First-Order Operational Concern

A frequently underappreciated aspect of running RocksDB in production is that **compaction threads directly determine write latency stability**. If compaction falls behind the write rate, L0 file count grows until it hits `level0_slowdown_writes_trigger` (default 20), at which point RocksDB throttles foreground writes. If it continues to grow to `level0_stop_writes_trigger` (default 36), writes are halted entirely. This is a **write stall** — a hard availability event visible to the application.

The solution is not simply "add more compaction threads" — excessive compaction threads compete with foreground reads for I/O bandwidth. The correct approach is to **monitor compaction lag via RocksDB statistics** (specifically `rocksdb.estimate.pending.compaction.bytes` and `rocksdb.num.running.compactions`), set an appropriate number of background threads via `SetBackgroundThreads()`, and adjust write buffer sizes and L0 thresholds to give compaction enough headroom. This requires ongoing operational expertise, not just initial configuration.

### 6.3 Bloom Filters Are the Correctness Mechanism for Practical Read Performance

Without Bloom filters, LSM-trees would be impractical for point lookups. The theoretical O(num_levels) read complexity would manifest as 5–10 random disk reads per lookup — unacceptable for interactive workloads. Bloom filters reduce this to, in practice, 0–1 disk reads for the common case. The architectural takeaway is that **probabilistic data structures are not approximations in RocksDB — they are a load-bearing component of the read path**. Choosing the right `bits_per_key` value involves a genuine engineering trade-off between memory pressure (more bits = more RAM used) and read performance (fewer false positives = fewer spurious disk reads).

### 6.4 High Configurability Is Both RocksDB's Superpower and Its Operational Complexity

RocksDB exposes over 100 tunable parameters, from compaction style and thread counts to block cache sizes, compression codecs per level, and iterator prefetch distances. This degree of configurability is intentional: Facebook, Uber, LinkedIn, TiKV, CockroachDB, and MyRocks all use RocksDB as their storage foundation, but with radically different tuning profiles suited to their workloads. This is the **central design philosophy** — build a general-purpose, deeply tunable foundation, not an opinionated system.

The implication is that **a default RocksDB installation is rarely optimal for any specific workload**. Engineers must profile their access patterns (write-heavy vs. read-heavy, point lookups vs. range scans, key size distribution) and tune accordingly. The RocksDB team provides the `OptimizeForPointLookup()`, `OptimizeForSmallDb()`, and `OptimizeLevelStyleCompaction()` helper presets as starting points, but production deployments invariably require workload-specific tuning. This configurability is what makes RocksDB both the most powerful and most operationally demanding embedded storage engine available.

---

*Document prepared as part of the Advanced DBMS System Design analysis series.*
