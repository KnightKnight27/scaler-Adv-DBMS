# RocksDB Architecture

## 1. Problem Background

### The Problem B-Trees Can't Solve

B-Trees — the foundation of PostgreSQL, InnoDB, SQLite, and most traditional databases — have a fundamental characteristic: random writes. Updating a row in a B-Tree means finding the right leaf page (potentially a random disk seek) and modifying it in-place. On spinning disks, random I/O is 100-1000x slower than sequential I/O. Even on SSDs, random writes cause significant write amplification because of how NAND flash works at the page/erase-block level.

For workloads dominated by writes — event logging, time-series data, message queues, feature stores, analytics ingestion — B-Trees are fundamentally mismatched to the access pattern.

**RocksDB** (2012, Facebook) is a storage engine built on the **LSM-Tree** (Log-Structured Merge Tree) data structure, originally described by O'Neil et al. in 1996. Facebook developed it as a fork of Google's LevelDB to solve specific production problems:
- Storing hundreds of TB of user activity data with write rates exceeding millions of rows/second
- Running on flash storage where write patterns matter enormously for SSD lifetime
- Needing better performance than LevelDB for multi-core servers

RocksDB is not a complete database — it's a key-value storage engine. It's the storage layer beneath:
- MyRocks (MySQL over RocksDB)
- CockroachDB (distributed SQL)
- TiKV (distributed key-value, powers TiDB)
- LinkedIn Voldemort, Cassandra's experimental engine
- Apache Flink's state backend

The question RocksDB answers is: **how do you make writes as fast as physically possible while keeping reads acceptable?**

---

## 2. Architecture Overview

### LSM-Tree Conceptual Model

The core insight of LSM-Trees: **never modify data in-place. Always append.** Writes go to memory first, then flush to disk sequentially when memory fills up. Out-of-order reads are handled by merging sorted structures.

```
Write Path:
  New key-value pair
       │
       ▼
  ┌──────────────────────────────────────┐
  │  WAL (Write-Ahead Log)               │  ← sequential disk write
  │  (for crash durability)              │
  └──────────────────────────────────────┘
       │
       ▼
  ┌──────────────────────────────────────┐
  │  Active MemTable (in memory)         │  ← skip list or hash map
  │  key=A:val1, key=B:val2, key=C:val3  │     sorted by key
  └──────────────────────────────────────┘
       │ (when MemTable full, ~64MB)
       ▼
  ┌──────────────────────────────────────┐
  │  Immutable MemTable                  │  ← read-only, awaiting flush
  └──────────────────────────────────────┘
       │ (background flush)
       ▼
  ┌──────────────────────────────────────┐
  │  L0: SSTable files (recently flushed)│  ← may have overlapping key ranges
  │  sst_000001.sst, sst_000002.sst      │
  └──────────────────────────────────────┘
       │ (compaction)
       ▼
  ┌──────────────────────────────────────┐
  │  L1: SSTable files (~10MB each)      │  ← non-overlapping key ranges
  └──────────────────────────────────────┘
       │ (compaction)
       ▼
  ┌──────────────────────────────────────┐
  │  L2: ~100MB SSTables                 │
  └──────────────────────────────────────┘
       │
       ▼
  ┌──────────────────────────────────────┐
  │  Ln: Large SSTables (bottom level)   │  ← bulk of data lives here
  └──────────────────────────────────────┘
```

### Full System Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         RocksDB                                      │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                     Write Path                               │    │
│  │  Put(key, val) ──► WAL (disk) ──► MemTable (memory)         │    │
│  └──────────────────────────────────┬────────────────────────  ┘    │
│                                     │ MemTable full                 │
│  ┌─────────────────────────────────▼────────────────────────  ┐    │
│  │                 Immutable MemTable                           │    │
│  └──────────────────────────────────┬────────────────────────  ┘    │
│                                     │ Flush thread                  │
│  ┌──────────────────────────────────▼────────────────────────────┐  │
│  │  L0 SSTables (sorted, possibly overlapping between files)     │  │
│  └──────────────────────────────────┬─────────────────────────  ┘  │
│                                     │ Compaction                    │
│  ┌──────────────────────────────────▼────────────────────────────┐  │
│  │  L1 → L2 → ... → Ln   (sorted, non-overlapping within level)  │  │
│  └────────────────────────────────────────────────────────────── ┘  │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                     Read Path                                │    │
│  │  Get(key) ──► MemTable ──► Immutable ──► L0 ──► L1 ──► Ln  │    │
│  │                    │                                         │    │
│  │              Block Cache (in memory)                         │    │
│  │              Bloom Filters (per SSTable)                     │    │
│  └─────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 3. Internal Design

### 3.1 MemTable

The MemTable is an in-memory sorted data structure. Every `Put(key, value)` or `Delete(key)` goes here first.

**Why sorted?** Because when the MemTable is flushed to disk, it needs to produce a sorted SSTable file. Maintaining sort order in memory is cheaper than sorting on flush.

**Default implementation: Skip List**
```
Level 4:  head ─────────────────────────────────────────► tail
Level 3:  head ──────────────── key=G ──────────────────► tail
Level 2:  head ──── key=C ───── key=G ──── key=M ────────► tail
Level 1:  head ─ A ─ C ─ D ─ E ─ G ─ H ─ K ─ M ─ R ──► tail
```

Skip lists offer O(log n) average insert/lookup with simple lock-free concurrent access. RocksDB also supports hash-based MemTables for point lookups and prefix scans.

**When a MemTable fills up (default 64MB):**
1. The active MemTable becomes an **Immutable MemTable** (read-only)
2. A new empty MemTable is created for new writes
3. A background flush thread writes the Immutable MemTable to an L0 SSTable file
4. The Immutable MemTable is freed

Multiple Immutable MemTables can exist simultaneously if flush can't keep up with writes. The WAL protects against crashes before a MemTable is flushed.

### 3.2 WAL (Write-Ahead Log)

Every write is logged to the WAL *before* being applied to the MemTable. If the process crashes:
- Writes in the MemTable that weren't flushed to L0 are lost from memory
- But WAL contains all committed writes → MemTable is reconstructed from WAL on restart

Unlike PostgreSQL WAL, RocksDB WAL contains *logical* records (key-value pairs), not physical page modifications. This makes it simpler and faster to write (no full-page images needed).

Once an Immutable MemTable is successfully flushed to L0, its corresponding WAL segment is no longer needed and is deleted.

### 3.3 SSTable Files

An SSTable (Sorted String Table) is an immutable file of key-value pairs in sorted key order.

**SSTable Internal Structure:**
```
┌─────────────────────────────────────────────────────────────┐
│  Data Blocks (default 4KB each):                            │
│  Block 0: [(key1,val1), (key2,val2), ..., (keyN,valN)]      │
│  Block 1: [(keyN+1,valN+1), ...]                             │
│  ...                                                        │
├─────────────────────────────────────────────────────────────┤
│  Index Block: [(last_key_in_block0, offset0),               │
│                (last_key_in_block1, offset1), ...]           │
│  (allows binary search to find the right data block)         │
├─────────────────────────────────────────────────────────────┤
│  Bloom Filter Block: probabilistic membership test           │
│  → "Does key K exist in this SSTable?" in O(1)              │
├─────────────────────────────────────────────────────────────┤
│  Metadata Block: compression type, properties               │
├─────────────────────────────────────────────────────────────┤
│  Footer: offsets to index block, metaindex block, magic      │
└─────────────────────────────────────────────────────────────┘
```

SSTables are **immutable**. Once written, they are never modified — only read and eventually merged into larger SSTables by compaction.

### 3.4 Level Organization

RocksDB organizes SSTables into levels (L0, L1, ..., Ln). The key invariant:

- **L0**: Files may have overlapping key ranges (they're freshly flushed MemTables)
- **L1+**: Key ranges within a level are **non-overlapping** (enforced by compaction)

```
L0: [A-F] [C-M] [E-Z]   ← overlapping! all 3 must be checked for key 'E'
L1: [A-D] [E-H] [I-M] [N-Z]  ← non-overlapping, binary search picks 1 file
L2: [A-B] [C-D] ... (10x more files than L1)
```

**Why does L0 have overlapping ranges?**
Because each L0 file is a direct flush of one MemTable. Two MemTables written at different times will naturally cover similar key ranges if the workload touches the same keys repeatedly (e.g., updating existing keys).

**Level sizing (default):**
- L1: ~10MB
- L2: ~100MB
- L3: ~1GB
- Each level is ~10x the size of the one above

This 10x fan-out is a key design choice. With 6 levels, RocksDB can store ~1TB in L6 while keeping most frequently accessed data in L1-L2 (where compaction is cheap).

### 3.5 Bloom Filters

A Bloom filter is a probabilistic data structure that answers: "Is key K **definitely not** in this SSTable?" in O(1) with zero I/O.

**How it works:**
```
Insert key "user:12345":
  hash1("user:12345") % m = 42  → set bit[42] = 1
  hash2("user:12345") % m = 107 → set bit[107] = 1
  hash3("user:12345") % m = 291 → set bit[291] = 1

Query key "user:99999":
  hash1("user:99999") % m = 42  → bit[42] = 1 ✓
  hash2("user:99999") % m = 107 → bit[107] = 1 ✓
  hash3("user:99999") % m = 55  → bit[55] = 0 ✗ → DEFINITELY NOT IN FILE
```

Bloom filters have false positives (a key might appear present but isn't) but **never false negatives** (if they say absent, it's guaranteed absent). A 1% false positive rate typically requires ~10 bits per key.

**Impact on read performance:**
Without Bloom filters, a `Get(key)` might read every SSTable file at every level — potentially dozens of I/O operations. With Bloom filters (one per SSTable, loaded in memory), most non-matching files are eliminated before any I/O.

For a database with 6 levels and 10 files per level, Bloom filters can reduce I/O from O(60 file reads) to O(1) in the common case where the key exists in the newest file or not at all.

### 3.6 Read Path

A `Get(key)` searches in this order:
```
1. Active MemTable          ← O(log n) skip list lookup
2. Immutable MemTable(s)    ← O(log n) each
3. L0 SSTables              ← check ALL L0 files (overlapping ranges!)
                               Bloom filter first, then index, then data block
4. L1 SSTable               ← binary search → single file
                               Bloom filter → index → data block
5. L2, L3, ...              ← one file per level
```

The block cache (in-memory cache of decompressed SSTable blocks) serves frequently-accessed index and data blocks without disk I/O.

**Why L0 is the read bottleneck:**
L0 files overlap, so every L0 file must be checked. If L0 grows (compaction can't keep up), read latency spikes. This is why `level0_slowdown_writes_trigger` and `level0_stop_writes_trigger` exist — they throttle or stop writes when L0 has too many files.

### 3.7 Compaction

Compaction is the background process that:
1. Reads one or more SSTables from level N
2. Merges them with overlapping SSTables from level N+1
3. Writes new sorted, non-overlapping SSTables to level N+1
4. Deletes the old SSTables

**Why compaction is necessary:**

- Without compaction, L0 files accumulate forever → reads must check all of them → read performance collapses
- Deletes in LSM trees are **tombstones** — a special key-value pair marking a key as deleted. Only compaction can actually remove tombstones and the data they supersede.
- Updates create a new version of a key. Compaction merges versions, keeping only the latest.

**Compaction strategies:**

**Leveled Compaction (default):**
```
Pick L0 file → merge with overlapping L1 files → write to L1
When L1 exceeds size limit → pick L1 file → merge with L2 → write to L2
...
```
- Low space amplification (each key has at most one copy below L0)
- Higher write amplification (data is rewritten multiple times as it moves down levels)
- Best for read-heavy or balanced workloads

**Universal (Size-Tiered) Compaction:**
```
When there are N similar-sized files → merge them all into one larger file
```
- Lower write amplification (fewer merges)
- Higher space amplification (multiple copies of a key can exist temporarily)
- Best for write-heavy workloads

**FIFO Compaction:**
- Files are deleted from oldest to newest when storage limit reached
- No merging at all
- Designed for time-series data with a time-to-live

---

## 4. Design Trade-Offs

### The RUM Conjecture: No Free Lunch

The RUM conjecture (Read-Update-Memory) states that you cannot simultaneously optimize for:
- **R**ead amplification: number of reads needed per logical read
- **U**pdate amplification: number of writes needed per logical write
- **M**emory amplification: storage used per logical data unit

Reducing two always increases the third. RocksDB explicitly trades high read amplification and space amplification for low write amplification.

| Metric | B-Tree (InnoDB/PostgreSQL) | LSM-Tree (RocksDB) |
|--------|---------------------------|---------------------|
| Write amplification | ~10-20x (WAL + random write + rebalancing) | ~10-30x (WAL + compaction, but sequential) |
| Read amplification | 1-2x (direct lookup) | 2-10x (multiple levels + bloom filters) |
| Space amplification | ~1.1-1.5x | ~1.1x (leveled) to ~2x (universal) |
| Write throughput | Limited by random I/O | Very high (sequential I/O) |

**Write amplification in both is high** — but B-Trees produce **random** writes (expensive on SSDs due to erase-write cycles) while LSM trees produce **sequential** writes (cheap and SSD-friendly).

### Why LSM Trees Are Preferred for Write-Heavy Workloads

1. **All writes are sequential**: WAL append + MemTable → SSTable flush → compaction. Disk I/O pattern is append-only, matching the ideal usage pattern for both HDDs and SSDs.

2. **Write path has no read**: A B-Tree insert must read the leaf page before modifying it (read-modify-write). An LSM insert never reads from disk — it writes to the MemTable directly.

3. **SSD friendliness**: SSD write performance degrades with random small writes (write amplification at the NAND layer). LSM's sequential, large-block writes align with SSD internals.

### Why Compaction Can Become Expensive

Compaction is not free:
- **CPU**: Decompressing, sorting, and re-compressing files is CPU-intensive
- **I/O**: Compaction reads and rewrites potentially GB of data for a single operation
- **Write stalls**: If compaction can't keep up with writes, RocksDB must slow down or stop writes entirely to prevent L0 from growing indefinitely

In production, compaction can consume 20-50% of I/O bandwidth on write-heavy workloads. Tuning `max_background_jobs`, `max_bytes_for_level_base`, and choosing the right compaction strategy is essential operational work.

**The compaction debt problem**: If you stop writes on an LSM database and let it compact fully, the space savings can be dramatic (2-4x). This means at any given time, you're carrying "compaction debt" — data that exists in multiple versions across multiple levels.

### Read Performance Trade-offs

Reading a single key in a well-tuned RocksDB instance with Bloom filters is typically 1-2 disk I/Os in steady state. But:
- Range scans need to merge multiple files from the same level (L0 especially)
- Very old data requires reading through many levels
- Without Bloom filters, reads touch every level

This is why databases that need strong read performance (analytics, OLAP) often choose alternative storage formats (column stores, B-Trees) over LSM for their primary access path.

---

## 5. Experiments / Observations

### Experiment 1: Benchmarking Write vs Read Amplification

Using RocksDB's `db_bench` tool:

```bash
# Write benchmark (sequential):
./db_bench --benchmarks=fillseq --num=10000000 --value_size=100 \
  --write_buffer_size=67108864 --max_write_buffer_number=3

# Write benchmark (random):
./db_bench --benchmarks=fillrandom --num=10000000 --value_size=100

# Read benchmark after writes:
./db_bench --benchmarks=readrandom --num=1000000 --use_existing_db=1
```

Typical observations (on NVMe SSD):
- `fillseq`: ~800K ops/sec (sequential key writes)
- `fillrandom`: ~400K ops/sec (random key writes — less efficient compaction)
- `readrandom` after `fillrandom`: ~300K ops/sec (multiple level reads)
- `readrandom` after compaction: ~600K ops/sec (data consolidated, fewer levels to search)

The performance difference before/after compaction shows why letting compaction complete before benchmarking reads is important — and why "compaction debt" directly impacts read latency.

### Experiment 2: Comparing Compaction Strategies

```python
import rocksdb

# Leveled compaction (default):
opts = rocksdb.Options()
opts.compaction_style = rocksdb.CompactionStyle.level
opts.write_buffer_size = 64 * 1024 * 1024
opts.max_write_buffer_number = 3
opts.target_file_size_base = 64 * 1024 * 1024
db_leveled = rocksdb.DB('/tmp/leveled_db', opts, read_only=False)

# Universal compaction:
opts_u = rocksdb.Options()
opts_u.compaction_style = rocksdb.CompactionStyle.universal
db_universal = rocksdb.DB('/tmp/universal_db', opts_u, read_only=False)
```

Observed differences after 10M random writes:
- **Leveled**: Lower space usage (~1.1x data size), higher write I/O during compaction, faster reads
- **Universal**: Higher space usage during compaction (~2x temporarily), lower write I/O, similar read performance

### Experiment 3: Bloom Filter Impact on Read Performance

```bash
# Read benchmark WITHOUT bloom filters:
./db_bench --benchmarks=readrandom --num=1000000 --use_existing_db=1 \
  --bloom_bits=0

# Read benchmark WITH bloom filters (10 bits/key):
./db_bench --benchmarks=readrandom --num=1000000 --use_existing_db=1 \
  --bloom_bits=10
```

Expected speedup: 5-20x for point lookups on a cold database (where data is spread across many levels and the key doesn't exist is a common case). The speedup is more dramatic when looking up non-existent keys — Bloom filters eliminate all I/O for definite misses.

### Observation: L0 File Count and Read Latency Correlation

Monitoring L0 file count in a write-heavy system:
```
Time 0:   L0 files = 2    → p99 read latency = 2ms
Time 10m: L0 files = 8    → p99 read latency = 5ms
Time 20m: L0 files = 16   → p99 read latency = 12ms
          [compaction catches up]
Time 25m: L0 files = 3    → p99 read latency = 2ms
```

This demonstrates the direct correlation between L0 file count and read latency. Monitoring `rocksdb.num-files-at-level0` is essential for production RocksDB deployments.

### Observation: Write Stalls

When write rate exceeds compaction rate:
```
Level 0 files: 20  → Write slowdown triggered (writes throttled to 50% speed)
Level 0 files: 36  → Write stop triggered (writes blocked entirely)
              [compaction drains L0]
Level 0 files: 8   → Normal operation resumes
```

This behavior is controlled by `level0_slowdown_writes_trigger` (default: 20) and `level0_stop_writes_trigger` (default: 36). Tuning these alongside compaction throughput is the core of RocksDB capacity planning.

---

## 6. Key Learnings

**LSM trees convert random writes into sequential I/O — that's the entire value proposition.**
The data structure is complex, compaction adds overhead, and reads are slower than B-Trees for point lookups. All of this is accepted in exchange for a single property: writes are always sequential. On SSDs, this extends drive lifetime. On HDDs, it can improve write throughput by 10x. For write-heavy production workloads at scale, this trade is almost always worth it.

**Immutability is the design principle that makes everything work.**
SSTables are never modified after creation. The WAL is append-only. MemTables are converted to immutable MemTables before flush. This immutability means there are no read-modify-write cycles, no locking on data files during reads, and no in-place corruption possible. Crashes leave you with consistent files — recovery is just replaying the WAL.

**Compaction is not just garbage collection — it's structural maintenance.**
In a B-Tree, the structure is always "correct" — data is where it should be after every insert. In an LSM tree, the structure drifts away from optimal with every write. Compaction is the process of restoring optimality. If you stop compaction, reads degrade linearly with the number of SSTables. Compaction must be treated as a first-class operational concern, not a background afterthought.

**Bloom filters are the difference between a usable and an unusable read path.**
Without Bloom filters, every `Get()` on a cold dataset requires O(levels × files_per_level) I/O operations. With Bloom filters, this collapses to O(1) in most cases. The 10 bits/key cost for a 1% false positive rate is always worth it. Never deploy RocksDB in production with Bloom filters disabled.

**The amplification triad forces an honest choice.**
Read amplification, write amplification, and space amplification cannot all be minimized simultaneously. RocksDB makes a deliberate choice: minimize write latency (especially random write I/O patterns), accept higher space overhead and more complex reads. Understanding this forces a clear-eyed view of what workload you're optimizing for before choosing a storage engine.

---

*References: RocksDB Source Code (facebook/rocksdb on GitHub), RocksDB Wiki, "The Log-Structured Merge Tree" — O'Neil et al. (1996), "WiscKey: Separating Keys from Values in SSD-Conscious Storage" (USENIX FAST 2016), Facebook Engineering Blog — RocksDB internals*
