# Topic 4: RocksDB Architecture

> **Author:** Akshansh Sinha | Advanced DBMS — System Design Discussion

---

## 1. Problem Background

### Why LSM-Trees? Why RocksDB?

By 2011, Google had published its experience running **LevelDB** — a key-value store built on the **Log-Structured Merge Tree (LSM-Tree)** data structure, originally described by O'Neil et al. in 1996. The core insight of the LSM-Tree paper was this:

> Disk I/O is not symmetric. Sequential writes are 5–100× faster than random writes on both spinning disks and SSDs. Traditional B-Tree databases perform random I/O on every update because they must write to the position in the tree where the key belongs. Can we restructure the storage engine to convert all writes to sequential appends?

Facebook needed a storage engine optimized for **flash (SSD) storage** with high write throughput for their social graph and message databases. LevelDB was a good starting point, but it lacked:
- Production-grade operational tooling
- Concurrent write support
- Column families (logical namespaces)
- Tunable compaction strategies
- Fine-grained performance controls

In 2012, Facebook engineers (Dhruba Borthakur, Siying Dong, et al.) forked LevelDB and created **RocksDB** — a production-grade LSM-based embedded key-value store that is now used as the storage backend for:
- **MyRocks** (MySQL + RocksDB)
- **TiKV** (TiDB's distributed KV layer)
- **CockroachDB** (Pebble, a RocksDB-inspired Go implementation)
- **Apache Flink** (state backend)
- **Cassandra** (experimental)

---

## 2. Architecture Overview

```
WRITE PATH:
  Client write (key, value)
         │
         ▼
  ┌──────────────┐     ┌─────────────────────────────────────────────┐
  │  WAL (Write- │     │              In-Memory Layer                │
  │  Ahead Log)  │     │                                             │
  │  Sequential  │────►│  ┌──────────────────────────────────────┐  │
  │  append to   │     │  │  Active MemTable (RB-Tree or         │  │
  │  log file    │     │  │  SkipList, sorted by key)            │  │
  └──────────────┘     │  │  Writes land here first              │  │
                       │  └───────────────────┬──────────────────┘  │
                       │                      │ When full (64MB)     │
                       │  ┌───────────────────▼──────────────────┐  │
                       │  │  Immutable MemTable (frozen, pending) │  │
                       │  │  Still serves reads; being flushed   │  │
                       │  └───────────────────┬──────────────────┘  │
                       └──────────────────────┼─────────────────────┘
                                              │ Flush (background thread)
                                              ▼
READ PATH:              ┌────────────────────────────────────────────────┐
  Client read(key)      │             On-Disk Layer (SSTs)              │
         │              │                                               │
         │ Check        │  L0: [SST0.sst][SST1.sst][SST2.sst]...      │
         │ in order:    │      (recently flushed, may overlap keys)     │
         │              │                                               │
         ▼              │  L1: [SST_a.sst]...[SST_b.sst]              │
  MemTable (newest)     │      (non-overlapping, sorted, ~256MB total) │
         │              │                                               │
  Immutable MemTable    │  L2: [SST files, ~2.56GB total]             │
         │              │                                               │
  L0 SSTs (newest       │  L3: [SST files, ~25.6GB total]             │
           first)       │                                               │
         │              │  L4: [SST files, ~256GB total]              │
  L1 SSTs               │                                               │
         │              │  Ln: [SST files, 10x previous level]        │
  L2, L3, ... Ln SSTs   │                                               │
  (return first found)  └────────────────────────────────────────────────┘
```

---

## 3. Internal Design

### 3.1 MemTable — The In-Memory Write Buffer

All writes in RocksDB go first to the **MemTable**, an in-memory sorted data structure. This converts all writes into sequential memory operations, avoiding random disk I/O on the write path.

**Default implementation: SkipList**

A SkipList is a probabilistic data structure that supports O(log N) insert, lookup, and range queries — similar to a balanced BST, but lockable at finer granularity for concurrent access.

```
SkipList for MemTable (keys sorted):
  Level 3: [---> "apple" ──────────────────────────────► "mango"]
  Level 2: [---> "apple" ──────► "grape" ──────────────► "mango"]
  Level 1: [---> "apple" ──► "cherry" ──► "grape" ──────► "mango"]
  Level 0: ["apple"]["banana"]["cherry"]["date"]["grape"]["lemon"]["mango"]
```

Why a SkipList over a B-Tree for the MemTable?
- **Concurrent access**: SkipList nodes can be locked at the node level. Multiple reader threads can traverse different levels simultaneously with minimal contention.
- **Memory efficiency**: No page fragmentation. Each entry allocates only what it needs.
- **Ordered iteration**: Forward/backward range scans are naturally O(1) per step after finding the start position.

When the MemTable reaches its size limit (`write_buffer_size`, default 64MB):
1. It becomes an **immutable MemTable** (frozen, read-only)
2. A new empty MemTable becomes the active write target
3. A background thread flushes the immutable MemTable to disk as a new L0 SSTable

### 3.2 WAL — Durability Before MemTable Flush

Writes go to **both** the WAL (disk) and the MemTable (memory) simultaneously. The WAL is a sequential-append file (`000003.log`).

```
WAL record format:
  CRC | Length | RecordType | Data
  
RecordType:
  kFullType      — complete record fits in one block
  kFirstType     — first fragment of a large record
  kMiddleType    — middle fragment
  kLastType      — last fragment

WAL block size: 32 KB
```

**Crash recovery**: On restart, RocksDB replays all WAL records for unflushed MemTables. Once an immutable MemTable is flushed to an SST file, its corresponding WAL segment is deleted (no longer needed for recovery).

**WAL vs PostgreSQL WAL**: Both are sequential append logs for crash recovery. The key difference: RocksDB's WAL is only needed until the MemTable is flushed — after that, the SST files themselves are the durable source of truth. PostgreSQL's WAL is needed until a checkpoint confirms dirty buffer pages have been written.

### 3.3 SSTables (Sorted String Tables)

An SSTable is an **immutable, sorted file** written once during a flush or compaction. It is never modified after creation.

```
SSTable File Structure:
┌─────────────────────────────────────────────────────────────┐
│  Data Blocks (4KB each, compressed by default):            │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ key1:value1 | key2:value2 | key3:value3 | ...        │  │
│  │ (keys sorted, prefix-compressed for space efficiency) │  │
│  └──────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│  Index Block:                                              │
│  │  first_key_of_block → block_offset, block_size         │
│  │  (binary search to find which data block holds a key)  │
├─────────────────────────────────────────────────────────────┤
│  Bloom Filter Block (per SSTable or per data block):       │
│  │  Probabilistic bit array to quickly check "is key X    │
│  │  possibly in this file?" without reading data blocks   │
├─────────────────────────────────────────────────────────────┤
│  Metaindex Block                                           │
│  Footer (fixed 48 bytes): magic number, index offsets     │
└─────────────────────────────────────────────────────────────┘
```

**Immutability is a key design choice**: Because SSTables never change after creation, there is no need for complex locking during reads. Multiple readers can safely access the same SSTable file concurrently. This is also why RocksDB achieves high read throughput — reads are lock-free at the file level.

### 3.4 Bloom Filters — Reducing Read Amplification

A Bloom filter is a **probabilistic data structure** that answers "is X possibly in this set?" with:
- **No false negatives**: If the key is in the SST, the Bloom filter always says "yes"
- **Possible false positives**: If the key is NOT in the SST, the Bloom filter might say "yes" (with small probability, ~1%)

```
Bloom Filter (10 bits per key, 7 hash functions):
  bit array: [0,1,0,1,1,0,0,1,0,1,1,0,...] (millions of bits)

  To check if "pear" is in the SST:
    hash1("pear") mod N → check bit position
    hash2("pear") mod N → check bit position
    ...
    hash7("pear") mod N → check bit position
    
    If ANY bit is 0 → "pear" is DEFINITELY NOT in this SST → skip!
    If ALL bits are 1 → "pear" is PROBABLY in this SST → read data block

False positive rate ≈ (1 - e^(-kn/m))^k
where k = hash functions, n = elements, m = bit array size
```

**Impact on read performance**:

In a worst case without Bloom filters, reading a key that doesn't exist would require checking every SST file at every level — potentially hundreds of files. With Bloom filters:
- ~99% of "not found" lookups are rejected after checking a few bits
- Only ~1% of lookups incorrectly read a data block (false positives)
- Net effect: **point lookups are nearly O(1)** after MemTable + immutable MemTable miss

### 3.5 Compaction — The Heart of LSM Performance

Compaction is the process of merging SSTables from one level into the next level. It is necessary because:

1. **L0 SSTables can have overlapping key ranges** (each flush creates a new L0 file). Without compaction, a point lookup at L0 must check ALL L0 files.
2. **Space reclamation**: When a key is updated, the old version lives in an older SST. The new version lives in a newer SST. Without compaction, old versions accumulate forever.
3. **Read amplification**: Without merging, reading a key might require checking many SSTables at many levels.

#### Leveled Compaction (default)

```
Algorithm:
  When L(i) exceeds its size limit:
    1. Pick one SST file from L(i) with the most overlap with L(i+1)
    2. Pick all L(i+1) SSTs whose key range overlaps with the chosen L(i) SST
    3. Merge-sort all selected SSTs (N-way merge)
    4. Write output as new SSTs at L(i+1)
    5. Delete input SSTs

Key property: L1 and above have NON-OVERLAPPING key ranges within a level.
  L0: [a-z][c-m][b-w]   ← overlapping (problem for reads)
  L1: [a-d][e-h][i-m]   ← non-overlapping (one SST per key lookup)
```

**Write amplification** with leveled compaction:

When data moves from L0 → L1 → L2 → ... → Ln, each byte is rewritten multiple times. For a 10-level hierarchy:
- A byte written to MemTable is eventually written to L0, then merged into L1, L2, ..., Ln
- **Write amplification factor**: typically 10–30× (a 1 GB write generates 10–30 GB of actual disk writes through compaction)

#### Tiered Compaction (Universal)

```
Algorithm:
  Organize SSTables into "runs" (sorted sets of non-overlapping SSTables)
  When run count exceeds threshold:
    Merge the smallest adjacent runs into one larger run

Key property: Data moves through the system in large batches.
  - Write amplification: lower (3–5×)
  - Read amplification: higher (must check more files)
  - Space amplification: higher (duplicates exist during merge)
```

**The Amplification Trilemma**: RocksDB exposes this fundamental tension:

```
         Space Amplification
               │
               │    Tiered (Universal)
               │   ╱
               │  ╱  (write-optimized)
               │ ╱
               └──────────────────────► Write Amplification
              ╱
             ╱ Leveled
            ╱ (read-optimized)
           ╱
          ▼
     Read Amplification
```

You cannot minimize all three simultaneously. RocksDB's default (leveled) optimizes for read at the cost of write amplification.

### 3.6 Read Path (Point Lookup)

```
GET(key):
  1. Check Active MemTable (O(log N) SkipList lookup)
  2. Check Immutable MemTable(s) (newest first)
  3. Check L0 SSTs (newest first, must check ALL L0 files — overlapping ranges)
     → Use Bloom filter to skip files that can't contain the key
  4. Check L1, L2, ... Ln (one SST per level — non-overlapping ranges)
     → Binary search index block to find candidate SST
     → Use Bloom filter
     → Read data block if needed
  5. Return newest version found (or "not found")
```

**Read amplification** = number of disk reads per GET operation.
- Best case (key in MemTable): 1 in-memory lookup
- Worst case (key only at Ln, many L0 files, no Bloom filter): O(L0_count + n_levels) reads

### 3.7 Write Path

```
PUT(key, value):
  1. Serialize WAL record → append to WAL file (sequential write)
  2. Insert (key, value, sequence_number) into Active MemTable
  3. Return to caller (write complete)

Background work:
  When MemTable is full:
    - Switch to Immutable MemTable
    - Flush thread: sort + write → new L0 SST file
    - Compaction thread: merge L0→L1, L1→L2, etc. as needed
```

**Key insight**: The caller returns after step 2. ALL disk I/O is done in background threads. This is why RocksDB write latency is extremely low and consistent — writes are never blocked by disk I/O (unless the MemTable fill rate exceeds the flush rate, causing write stalls).

---

## 4. Design Trade-Offs

### 4.1 LSM-Tree vs B-Tree: The Fundamental Comparison

| Metric | B-Tree (InnoDB, PostgreSQL) | LSM-Tree (RocksDB) |
|---|---|---|
| Write latency | O(log N) random writes | O(1) sequential memory write |
| Read latency | O(log N) sequential traversal | O(levels × log N), Bloom helps |
| Write amplification | Low (~2–3×) | High (10–30× leveled) |
| Read amplification | Low (1 tree traversal) | Medium (multiple SSTables) |
| Space amplification | Low (in-place updates) | Medium (delete markers, old versions) |
| SSD wear | Higher (random writes) | Lower (sequential writes) |
| Range scan | Efficient (tree traversal) | Efficient (merge read of sorted files) |
| Compaction overhead | None (in-place) | Significant (background CPU/IO) |
| Concurrency | Complex (tree locking) | Simpler (immutable SSTables) |

### 4.2 Why Compaction is Both Necessary and Expensive

Compaction is unavoidable in LSM-Tree designs because:
- Without it: L0 grows without bound, reads degrade to O(L0_count)
- Delete markers (tombstones) accumulate and slow reads
- Space usage grows without bound

But compaction is expensive because:
- It reads and rewrites entire SST files
- At peak, compaction bandwidth can be **10× write bandwidth**
- Compaction competes with user reads/writes for disk bandwidth

**RocksDB's mitigations**:
- **Rate limiting**: `SetOptions({rate_limiter})` caps compaction disk I/O
- **Compaction filters**: Filter/drop old keys during compaction (time-to-live)
- **Sub-compaction**: Parallel compaction of non-overlapping key ranges
- **Column family compaction isolation**: Different CFs compact independently

### 4.3 Tombstones and the Delete Problem

In LSM-Trees, deletes are NOT performed by removing the key. Instead, a **tombstone** (delete marker with the key + no value) is written to the MemTable and eventually to SSTables.

```
Timeline of a deletion:
  t=1: PUT("key1", "value1") → MemTable → flush → L1 SST A
  t=2: DELETE("key1") → MemTable: tombstone{key1, seq=200}
       → flush → L0 SST B
  t=3: Compaction merges L0+L1:
       → Sees tombstone > original entry → both are DROPPED
       → "key1" no longer appears in output SST

But between t=2 and t=3:
  - GET("key1") must read both the tombstone AND the original SST
  - The tombstone "shadows" the original but doesn't immediately remove it
```

A critical edge case: if a tombstone reaches the **bottom level** (Ln) without the original key being in the same compaction batch, the tombstone persists at Ln. Meanwhile the original key may not even exist — but the tombstone wastes space and slows scans.

---

## 5. Experiments / Observations

### Experiment 1: Write Amplification Under Leveled Compaction

Using `db_bench` (RocksDB's built-in benchmark tool):

```bash
# Benchmark: sequential writes with leveled compaction
./db_bench --benchmarks=fillseq \
  --num=10000000 \
  --value_size=100 \
  --db=/tmp/rocksdb_bench \
  --compression_type=none \
  --statistics

# After benchmark, check compaction stats:
# Look for: "Compaction Stats" in output
# Key metrics:
#   W-Amp: write amplification (bytes written by compaction / bytes written by user)
#   R-Amp: read amplification (SST files read per user read)
```

**Expected output pattern:**
```
Level  Files   Size     Score  Read(GB)  Rn(GB)  Rnp1(GB)  Write(GB)  W-Amp
  L0      0    0 MB      0.0     0.00    0.00      0.00       1.00      -
  L1      4  256 MB      1.0     8.50    1.00      7.50       8.50     8.50
  L2     40  2560 MB     1.0    85.00   10.00     75.00      85.00     8.50
  L3    400  25.6 GB     0.8   850.00  100.00    750.00     850.00     8.50
```

**Observation**: Each level amplifies writes by ~8.5× in this scenario. Total write amplification from L0 to L3 is multiplicative — the data read and rewritten during compaction far exceeds the original data size.

### Experiment 2: Bloom Filter Effectiveness

```python
# Pseudocode for observing Bloom filter behavior
import rocksdb

opts = rocksdb.Options()
opts.create_if_missing = True
# Configure Bloom filter: 10 bits per key
opts.table_factory = rocksdb.BlockBasedTableFactory(
    filter_policy=rocksdb.BloomFilterPolicy(10)
)
db = rocksdb.DB('/tmp/bloom_test', opts)

# Insert 1 million keys
for i in range(1_000_000):
    db.put(f'key_{i}'.encode(), b'value')

# Force flush to SSTables
db.compact_range()

# Now measure: point lookup for non-existent keys
import time
start = time.time()
for i in range(1_000_000, 2_000_000):  # Non-existent keys
    db.get(f'key_{i}'.encode())
elapsed_with_bloom = time.time() - start

# Compare with Bloom filter disabled — observe ~10x slowdown
```

**Expected observation**: With Bloom filters, ~99% of non-existent key lookups are resolved without reading any data block (only the Bloom filter bits are checked). Without Bloom filters, each non-existent lookup reads the index block and potentially a data block from every candidate SST file.

### Experiment 3: Compaction Strategy Comparison (Write vs Read Amplification)

```bash
# Leveled Compaction:
./db_bench --benchmarks=fillrandom,readrandom \
  --compaction_style=0 \  # 0 = leveled
  --num=5000000 --value_size=200

# Universal (Tiered) Compaction:
./db_bench --benchmarks=fillrandom,readrandom \
  --compaction_style=1 \  # 1 = universal
  --num=5000000 --value_size=200

# Expected results:
# Leveled:   fillrandom slower, readrandom faster
# Universal: fillrandom faster, readrandom slower
```

**Observation table (approximate, hardware-dependent):**

| Metric | Leveled | Universal |
|---|---|---|
| Write throughput | 200k ops/sec | 350k ops/sec |
| Read throughput | 350k ops/sec | 200k ops/sec |
| Write amplification | ~20× | ~5× |
| Space overhead | ~1.1× | ~1.5× |

This validates the amplification trilemma: optimizing one axis degrades others.

### Experiment 4: Impact of Compaction on Write Latency

```bash
./db_bench --benchmarks=fillrandom \
  --histogram=1 \
  --num=10000000

# Look for P99, P999 write latency in output
# Expected: occasional spikes during compaction
# P50: ~0.1ms  (MemTable write)
# P99: ~2ms    (waiting for MemTable flush slot)
# P999: ~50ms  (write stall during compaction catch-up)
```

**Observation**: RocksDB write latency is bimodal — most writes are extremely fast (microsecond-level MemTable operations) but occasionally experience a "write stall" when the flush/compaction system cannot keep up with write rate. Tuning `level0_slowdown_writes_trigger` and `level0_stop_writes_trigger` controls this.

---

## 6. Key Learnings

### Architectural Lessons

1. **LSM-Trees trade write efficiency for read complexity**: The fundamental insight is that random writes on disk are expensive, so LSM-Trees convert all writes to sequential appends. The cost is that reads must check multiple places (MemTable + multiple SST levels). Bloom filters and leveled compaction exist specifically to recover the read performance lost by this trade.

2. **Immutability simplifies concurrent access**: Because SSTables are never modified after creation, there is no need for complex concurrent access protocols. Read-write conflicts simply don't exist at the file level. This is a powerful simplification — RocksDB achieves high concurrency with much simpler locking than B-Tree databases.

3. **Compaction is not free — it must be budgeted**: In a write-heavy production system, compaction can consume 5–10× the user write bandwidth. If you provision storage for 100 MB/s user writes, you must provision for 1 GB/s of total disk write bandwidth to account for compaction. Failure to do this causes compaction debt, write stalls, and latency spikes.

4. **The choice of compaction strategy is an application-level decision**: RocksDB exposes multiple compaction strategies because different workloads have different requirements. A write-heavy logging system should use universal compaction. A read-heavy analytics cache should use leveled compaction. There is no universally correct choice.

5. **Sequence numbers enable multi-version reads without a separate undo log**: Every key in RocksDB has a sequence number. A snapshot is just a sequence number; a read at snapshot `S` ignores all entries with sequence number > S. This is MVCC without a separate undo log or heap version chain — the sorted immutable SSTables are the version history.

6. **Delete is not delete**: In an LSM-Tree, DELETE writes a tombstone. The actual removal happens at the next compaction that brings the tombstone and the original key together. This means space from deleted keys is not reclaimed immediately, and long-lived tombstones are a real operational concern (especially in systems with time-to-live semantics).

### Surprising Observations

- RocksDB can be configured with `max_write_buffer_number` to allow multiple immutable MemTables in memory simultaneously. This is a buffer against slow compaction — if the flush thread falls behind, writes can continue into additional MemTables rather than stalling immediately.
- The **block cache** (separate from the buffer pool in B-Tree databases) caches individual 4KB SST data blocks. A single SST access might read 16 data blocks, but only the needed block is cached. This is more granular than PostgreSQL's page-level cache.
- RocksDB supports **Column Families** — separate keyspaces within a single database, each with its own MemTable and compaction settings. This is similar to tables in a relational DB, but at the storage engine level.
- **Prefix Bloom filters**: If your keys have a common prefix structure (e.g., `user_<id>_<field>`), RocksDB can build Bloom filters per prefix, dramatically accelerating prefix-range scans.

---

*References:*
- *O'Neil, P. et al. "The Log-Structured Merge Tree (LSM-Tree)" (1996)*
- *Dong, S. et al. "Optimizing Space Amplification in RocksDB" (CIDR 2017)*
- *RocksDB Wiki: https://github.com/facebook/rocksdb/wiki*
- *"RocksDB: Evolution of Development Priorities in a Key-value Store Serving Large-scale Applications" (VLDB 2021)*
- *Idreos, S. et al. "Designing Access Methods: The RUM Conjecture" (EDBT 2016)*
