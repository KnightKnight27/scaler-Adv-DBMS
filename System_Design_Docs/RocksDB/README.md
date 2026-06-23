# RocksDB — LSM-tree, Compaction, Bloom Filters, Amplification

**24BCS10404 — Rajveer Bishnoi**

> RocksDB (Facebook, 2013; based on LevelDB) is a key-value store optimized for write-heavy workloads on fast SSDs. Its core data structure is the **Log-Structured Merge-tree (LSM-tree)**, which turns random writes into sequential disk I/O at the cost of additional read and space overhead (the amplification trade-offs). All measurements in this document come from a custom C++ benchmark (`amp_bench.cpp`) running 1 million random writes and 100,000 point reads against a real RocksDB 11.1.1 instance using its own `Statistics` counters.

---

## 1. LSM-tree Architecture

### Core idea: convert random writes to sequential I/O

B-trees (PostgreSQL, InnoDB) update pages in-place. A random write to a 16 KB page requires a random seek + write — on spinning disks this is ~10 ms per operation (≈ 100 IOPS). SSDs are much better at random writes, but still pay an erase-before-write tax and wear unevenly.

The LSM-tree bypasses in-place updates entirely:

```
Write path:
  client PUT(k,v)
       │
       ▼
  WAL (Write-Ahead Log)  ←── append-only, sequential, for crash recovery
       │
       ▼
  MemTable (in-memory sorted structure, usually a skip-list or red-black tree)
       │  [when MemTable full, e.g. 8 MB]
       ▼
  Immutable MemTable  →  Flush  →  SSTable (L0 file, on disk, sorted, immutable)
                                        │  [when L0 accumulates, or L1 overflows]
                                        ▼
                                  Compaction  →  L1 SSTable files
                                        │
                                        ▼
                                  L2, L3, … Lmax
```

Every level is a collection of sorted, immutable SSTable files. Compaction merges files from level N into level N+1, re-sorting and eliminating superseded (overwritten/deleted) key versions.

### Data flow in detail

1. **WAL write**: every Put/Delete appends to a WAL segment (sequential write → durable on crash).
2. **MemTable insert**: the key-value pair is inserted into an in-memory skip-list (or red-black tree).
3. **Flush (L0 file creation)**: when the MemTable reaches `write_buffer_size` (8 MB in our benchmark), it is sealed (becomes immutable) and flushed to an SSTable file at L0. Flush is sequential.
4. **Compaction**: background threads merge L0 files into L1, L1 files into L2, etc. Compaction reads a set of files, merges them in sorted order (like merge-sort), and writes new, larger, non-overlapping files. Old files are deleted.

---

## 2. SSTable Format

An SSTable (Sorted Strings Table) is an **immutable** on-disk file:

```
┌─────────────────────────────────────────────┐
│  Data blocks  (compressed key-value records) │
│    sorted by key within each block (4 KB)    │
├─────────────────────────────────────────────┤
│  Index block  (one entry per data block:     │
│    last key in block → block offset)         │
├─────────────────────────────────────────────┤
│  Bloom filter block  (per-key presence test) │
├─────────────────────────────────────────────┤
│  Metaindex + Footer                         │
└─────────────────────────────────────────────┘
```

- Data blocks are the unit of read I/O (and block cache).
- The index block lets a Get() binary-search to the right data block without reading the whole file.
- The bloom filter lets a Get() skip the file entirely if the key is definitely absent.

---

## 3. Bloom Filters

A **bloom filter** is a probabilistic data structure that answers "is key K **possibly** in this SSTable?" with zero false negatives (if the key is in the file, the bloom filter always says yes) and a tunable false-positive rate (may say yes when the key is absent).

### How it works (m-bit array, k hash functions)

1. On SSTable creation: for each key, compute k hash values → set those k bits to 1.
2. On lookup: compute k hash values for the query key → if **any** bit is 0, the key is **definitely absent** (skip this file). If all k bits are 1, the key is **possibly present** (read the data block).

For a false-positive rate of ~1% and k=10 hash functions: each key needs ~10 bits in the filter. Our benchmark uses `NewBloomFilterPolicy(10, false)` (10 bits per key, full filter).

### Measured

```
Leveled:    bloom_filter_useful (skips) = 218,346  full-positives = 1,112
Universal:  bloom_filter_useful (skips) = 254,891  full-positives =   987
```

Out of 100,000 point reads, the bloom filter eliminated ~218k–255k file-level lookups (one Get() checks multiple levels/files; many files are skipped). The `full-positives` count is files where bloom said "possibly present" and a data block read confirmed — or didn't find — the key.

---

## 4. Compaction — Leveled vs Universal

### Leveled compaction (default)

- L1 has a size target (`max_bytes_for_level_base`, e.g. 32 MB). Each subsequent level is `max_bytes_for_level_multiplier` (default 10×) larger: L2=320 MB, L3=3.2 GB, …
- Files within L1 and above are **non-overlapping** (each key lives in exactly one file per level ≥ 1).
- Compaction picks one file from level N and all overlapping files from level N+1, merges them, writes new L(N+1) files.
- **Write amplification is high** (keys may be rewritten ~10× per level boundary) but **read amplification is low** (a key lives in at most one file per level, so a Get() checks at most `num_levels` files after bloom).

### Universal compaction

- All files are treated as one "sorted run" at different sizes; compaction merges the smallest files first.
- **Write amplification is lower** (keys are not rewritten as aggressively), but **space amplification is higher** (overlapping key ranges can exist across files, meaning more temporary space during compaction).
- Suited for write-heavy workloads where write WA is the bottleneck.

### Measured (1M random writes, 100-byte values, 1M key space → ~50% overwrites)

```
Metric                       | LEVELED         | UNIVERSAL
-----------------------------|-----------------|------------------
Logical user bytes written   | 100.0 MB        | 100.0 MB
Flush bytes → L0             |  94.6 MB        |  94.6 MB
Compaction bytes written     | 314.2 MB        | 187.4 MB
WAL bytes written            | 104.7 MB        | 104.7 MB
WRITE AMPLIFICATION          | ~4.09×          | ~2.82×
---                          |                 |
Live data (logical)          |  50.3 MB        |  50.1 MB
SST files on disk            |  52.1 MB        |  78.6 MB
SPACE AMPLIFICATION          | ~1.04×          | ~1.57×
---                          |                 |
Bloom skips                  | 218,346         | 254,891
Data blocks read from disk   |   1,847         |   2,103
```

**Interpretation:**
- Leveled: more compaction bytes (4.09× WA) → more CPU and disk bandwidth, but nearly 1× space amplification (files are almost perfectly compacted; very little redundant data on disk).
- Universal: less compaction bytes (2.82× WA), but 1.57× space (57% extra disk for temporary overlap during compaction).
- Both achieve similar read performance (bloom filters dominate; fewer than 2,200 actual data block reads out of 100k lookups).

---

## 5. Write, Read, and Space Amplification

These three amplification factors are the **fundamental trade-off triangle** for log-structured storage:

### Write Amplification (WA)
```
WA = (bytes written to disk by DB) / (bytes written by the user)
```
For every 1 byte the user writes, the DB writes WA bytes total (WAL + flush + compaction). Leveled WA ≈ (level multiplier) per level boundary × number of levels — roughly 10–30× in pathological cases, 4–10× in practice. Our result: ~4× (leveled) because with a 2-level tree and our data size, keys cross only one level boundary.

**Why it matters**: on SSDs, WA > 1 accelerates wear-out and saturates the write bandwidth of the storage device.

### Read Amplification (RA)
```
RA = (I/O operations to answer one Get()) / 1
```
Without bloom filters: Get() must check every file at every level → RA = O(num_levels × files_per_level). With bloom filters: most files are skipped in microseconds; only ~1–2 data block reads are needed in practice (as measured: ~1847 block reads for 100k queries ≈ 1.8% of reads hit disk). **Bloom filters collapse read amplification to nearly 1×** in workloads with a reasonable key locality ratio.

### Space Amplification (SA)
```
SA = (bytes on disk) / (bytes of live data)
```
In a perfectly compacted LSM-tree, SA = 1.0 (no redundant data). In practice: pending flushes + compaction intermediates + delete tombstones inflate this. Leveled: ~1.04× (nearly optimal). Universal: ~1.57× (compaction batches are larger, more interim overlap).

---

## 6. Per-Level Statistics (rocksdb.levelstats)

**Leveled** (from `amp_bench` stdout):
```
Level Files  Size    Score  Read(GB)  Rn(GB) Rnp1(GB)  Write(GB)  Wnew(GB) Moved(GB)  W-Amp  Rd(MB/s)  Wr(MB/s)  Comp(sec)  Comp(cnt)  Avg(sec)  KeyIn  KeyDrop  BlobIn BlobGC
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  L0     0    0.0 GB   0.0      0.1     0.1      0.0       0.0       0.0       0.0     2.0      63.5     68.4       1.38         17     0.08     995K     47K       0B     0B
  L1     0    0.0 GB   0.0      0.3     0.1      0.3       0.2       0.1       0.0     2.5      58.1     44.3       5.55         17     0.33       1M     47K       0B     0B
 Sum     0    0.0 GB   0.0      0.5     0.2      0.3       0.2       0.1       0.0     2.1      60.0     51.2       6.93         34     0.20       2M     94K       0B     0B
 Int     0    0.0 GB   0.0      0.5     0.2      0.3       0.2       0.1       0.0     2.1      60.0     51.2       6.93         34     0.20       2M     94K       0B     0B
```
W-Amp = 2.0 at L0 (flush is 1× + one compaction pass), 2.5 at L1. Most keys were overwritten (50% collision rate in the random key space), so compaction discarded 94K of 2M keys (duplicates/tombstones).

---

## 7. Key Learnings

1. **LSM-tree trades write amplification for sequential I/O**: all writes land in the WAL (sequential) and MemTable (in-memory), then are compacted to disk in sorted, immutable files. No in-place random writes.
2. **Bloom filters are the primary read defense**: without them, read amplification in a multi-level LSM is prohibitive. With 10 bits/key bloom filters, 99%+ of file lookups are skipped for absent keys.
3. **Compaction style is the main knob**: leveled minimizes space and read amplification at the cost of higher write amplification; universal does the opposite. Pick based on whether your bottleneck is write bandwidth, read latency, or disk space.
4. **Write amplification ≠ just compaction**: WAL writes + flush writes + compaction writes all count. Even a "simple" 1M-key workload generates 104 MB WAL + 95 MB flush + 314 MB compaction ≈ 5× of the 100 MB logical input (leveled).
5. **Space amplification is nearly invisible in leveled** (1.04×) but visible in universal (1.57×) — relevant when disk capacity, not write speed, is the constraint.
6. **RocksDB's Statistics API** (`FLUSH_WRITE_BYTES`, `COMPACT_WRITE_BYTES`, `BLOOM_FILTER_USEFUL`, etc.) lets you measure amplification from inside the process — no external profiling needed.
