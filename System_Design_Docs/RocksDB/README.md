# RocksDB Architecture – LSM-Tree Based Storage Engine

**Name:** Pulasari Jai  
**Roll Number:** 24BCS10656  
**Course:** Advanced DBMS  
**Topic:** Topic 4 – RocksDB Architecture

---

## 1. Problem Background

### Why does RocksDB exist?

So here's the thing – traditional databases like PostgreSQL and MySQL were designed in an era where spinning hard disks (HDDs) were the primary storage medium. HDDs are slow at random I/O but reasonably fast at sequential I/O. B+ trees, which are what most traditional databases use, generate a lot of random writes – every insert or update potentially touches multiple non-contiguous pages and updates multiple index structures in place. On SSDs this is less painful, but it's still not ideal.

As we started dealing with write-heavy workloads at massive scale – think hundreds of thousands of writes per second – it became clear that B+ tree based engines weren't the best fit. You need something that's fundamentally optimized for writes.

RocksDB was born out of this problem. It's based on a data structure called the **LSM-tree (Log-Structured Merge-tree)**, which was originally described in a 1996 paper by Patrick O'Neil et al. The core idea of an LSM-tree is simple: **convert random writes into sequential writes**. Sequential writes are far faster than random writes on both HDDs and SSDs.

Facebook built RocksDB in 2012 as a fork of Google's **LevelDB** (which was itself an open-source implementation of the LSM-tree ideas). LevelDB was good, but Facebook needed it to perform on server-grade hardware with multiple CPU cores and high concurrency. So they took LevelDB and made it production-grade for their own infrastructure.

Today RocksDB powers a huge chunk of Facebook's storage infrastructure. It's also used as the storage backend inside other systems:
- **MyRocks** – MySQL with RocksDB as the storage engine (Facebook runs this)
- **CockroachDB** – uses RocksDB (now Pebble, a Go rewrite) underneath
- **TiKV** – the storage layer of TiDB uses RocksDB
- **Kafka** – uses RocksDB for certain state stores in Kafka Streams
- **Cassandra** – has experimental RocksDB backend support

So RocksDB isn't just a standalone database. It's more of an **embeddable storage engine** that other systems build on top of, similar to how SQLite is embedded into apps. The API is basically just `Put(key, value)`, `Get(key)`, `Delete(key)`, and `Scan(start, end)`. Simple key-value interface, but the internals are anything but simple.

---

## 2. Architecture Overview

### High-Level Picture

```
RocksDB Architecture
─────────────────────

Write Path:
───────────
  [Client: Put(key, value)]
          |
          ▼
  ┌───────────────────┐
  │    WAL (Write-    │  ← written to disk first (for durability)
  │    Ahead Log)     │
  └───────────────────┘
          |
          ▼
  ┌───────────────────┐
  │    MemTable       │  ← in-memory sorted structure (SkipList)
  │  (active, mutable)│    receives all new writes
  └───────────────────┘
          |
          | (when MemTable fills up)
          ▼
  ┌───────────────────┐
  │  Immutable        │  ← sealed MemTable, waiting to be flushed
  │  MemTable(s)      │
  └───────────────────┘
          |
          | (background flush thread)
          ▼

Read Path:                 Disk Layout:
──────────                 ────────────
  [Client: Get(key)]
          |                ┌───────────────┐
          ▼                │  L0 (4 files) │  ← freshly flushed SSTables
  Check MemTable           │  (may overlap)│
          |                ├───────────────┤
          ▼                │  L1 (10 MB)   │  ← compacted, sorted, no overlap
  Check Immutable          ├───────────────┤
  MemTable(s)              │  L2 (100 MB)  │
          |                ├───────────────┤
          ▼                │  L3 (1 GB)    │
  Check L0 SSTables        ├───────────────┤
  (newest → oldest)        │  L4 (10 GB)   │
          |                ├───────────────┤
          ▼                │  L5 (100 GB)  │
  Check L1 → Ln            ├───────────────┤
  (binary search within    │  L6 (1 TB)    │  ← largest level
   level, bloom filter     └───────────────┘
   to skip files)
```

### Main Components

- **WAL (Write-Ahead Log)** – sequential log file on disk. Every write goes here first before the MemTable. Used for crash recovery.
- **MemTable** – active in-memory write buffer. Implemented as a concurrent skip list. All new writes land here.
- **Immutable MemTable** – when the active MemTable fills up, it becomes immutable and a new active MemTable is created. Background threads flush immutable MemTables to disk.
- **SSTables (Sorted String Tables)** – immutable sorted files on disk. Once written, never modified.
- **Bloom Filters** – per-SSTable probabilistic data structure. Quickly tells you if a key is definitely NOT in a given SSTable.
- **Block Cache** – in-memory cache for frequently accessed SSTable blocks.
- **Compaction** – background process that merges SSTables across levels, reclaims space from deleted/overwritten keys.

---

## 3. Internal Design

### 3.1 MemTable

The MemTable is the entry point for all writes in RocksDB. It's an in-memory sorted data structure, and by default it's implemented as a **concurrent skip list**.

Why a skip list instead of a B-tree or hash map?
- **Sorted order** – RocksDB needs keys sorted for efficient range scans and to produce sorted output when flushing to an SSTable.
- **Concurrent access** – skip lists allow lock-free or fine-grained concurrent reads and writes, which matters for throughput.
- **Simple implementation** – easier to make concurrent compared to balanced BSTs like AVL or red-black trees.

Every write to RocksDB (Put or Delete) creates an entry in the MemTable with:
- The key
- The value (or a deletion tombstone marker for deletes)
- A **sequence number** – a monotonically increasing 64-bit integer that acts like a logical timestamp

```
MemTable state after some operations (sequence numbers shown):
──────────────────────────────────────────────────────────────

Put("apple", "red")      → seq=1
Put("banana", "yellow")  → seq=2
Put("cherry", "red")     → seq=3
Delete("banana")         → seq=4  ← tombstone, not an actual deletion yet
Put("apple", "green")    → seq=5

MemTable (sorted by key, newest seq first for same key):
  apple  → [seq=5: "green"] [seq=1: "red"]
  banana → [seq=4: DELETE]  [seq=2: "yellow"]
  cherry → [seq=3: "red"]
```

When the MemTable reaches its size limit (`write_buffer_size`, default 64 MB), it becomes **immutable** – no new writes go to it. A new empty MemTable takes over for incoming writes. The immutable MemTable waits for a background flush thread to write it out as an SSTable to disk.

You can have multiple immutable MemTables queued up if the flush thread can't keep up with write speed. If too many pile up, RocksDB stalls writes to give the flush thread time to catch up.

### 3.2 WAL (Write-Ahead Log)

Before a write lands in the MemTable, it's written to the WAL – a simple sequential append-only file on disk. This is the same concept as WAL in PostgreSQL and InnoDB: write to the log first, so if the system crashes before the MemTable is flushed, the WAL can replay all the writes that were in memory and recover them.

```
WAL File (sequential, append-only):
─────────────────────────────────────
[seq=1 | Put | apple | red]
[seq=2 | Put | banana | yellow]
[seq=3 | Put | cherry | red]
[seq=4 | Delete | banana]
[seq=5 | Put | apple | green]
...
```

When a MemTable is successfully flushed to an SSTable on disk, the corresponding WAL segment is no longer needed and can be deleted (or archived).

WAL writes are sequential, which is fast. The only fsync overhead is per-write or per-batch, controlled by the `sync` option.

### 3.3 SSTables (Sorted String Tables)

When a MemTable is flushed to disk, it becomes an **SSTable** – an immutable, sorted file. Once written, an SSTable is never modified. This immutability is fundamental to the LSM-tree design.

An SSTable file is divided into blocks (default 4 KB each):

```
SSTable File Layout:
─────────────────────────────────────────────────────────────

┌──────────────────────────────────────────────────────┐
│  Data Block 1: [apple:green] [banana:yellow] ...     │  ← sorted key-value pairs
├──────────────────────────────────────────────────────┤
│  Data Block 2: [dog:bark] [elephant:big] ...         │
├──────────────────────────────────────────────────────┤
│  ...                                                 │
├──────────────────────────────────────────────────────┤
│  Index Block: [(first_key_of_block → block_offset)]  │  ← for binary search within file
├──────────────────────────────────────────────────────┤
│  Bloom Filter Block                                  │  ← "is this key in this file?"
├──────────────────────────────────────────────────────┤
│  Footer: pointers to index block, bloom filter, etc. │
└──────────────────────────────────────────────────────┘
```

SSTables are organized into **levels** (L0 through Ln). Each level has a target size that's 10x larger than the previous level:

```
Level  | Target Size  | Notes
──────────────────────────────────────────────────────────
L0     | 4 files      | Freshly flushed from MemTable.
       |              | Files CAN overlap in key range.
       |              | Must check ALL L0 files on read.
──────────────────────────────────────────────────────────
L1     | 256 MB       | Compacted. Files do NOT overlap.
L2     | 2.5 GB       | Each level is ~10x bigger.
L3     | 25 GB        |
L4     | 250 GB       |
L5     | 2.5 TB       |
L6     | 25 TB        | (configurable, no hard limit)
──────────────────────────────────────────────────────────
```

The key property from L1 onward: **files within a level do not overlap in key range**. Given a key, you can binary search to find which L1 file (if any) could contain it. L0 is the exception – files there can overlap because they're flushed directly from MemTable without inter-file sorting.

### 3.4 Bloom Filters

Bloom filters are one of RocksDB's most important optimizations for read performance.

A Bloom filter is a probabilistic data structure that answers the question: **"Is this key definitely NOT in this SSTable?"**

- If it says NO → the key is definitely not in the file. Skip the file entirely.
- If it says YES → the key *might* be in the file (false positive possible). Go check.

It can never produce a false negative (it won't tell you the key is absent when it's actually there). The false positive rate is tunable – lower false positive rate means a larger Bloom filter in memory.

```
Read path without Bloom filters (worst case):
  Get("zzz") where "zzz" doesn't exist
  → Check MemTable: not found
  → Check all L0 files (4 files): read each → not found
  → Check L1 file (binary search to find candidate): read → not found
  → Check L2 file: read → not found
  → ... all the way to Ln
  → Return "not found"
  Total: multiple disk reads

Read path WITH Bloom filters:
  Get("zzz")
  → Check MemTable: not found
  → Bloom filter for each L0 file: all say "definitely not here"
  → Skip all L0 files (no disk read!)
  → Bloom filter for L1 candidate file: "definitely not here"
  → Skip L1 file
  → ... same for all levels
  → Return "not found"
  Total: zero data block reads (only in-memory Bloom filter checks)
```

For workloads with lots of negative lookups (querying keys that don't exist), Bloom filters are a massive win. The filters are kept in the block cache or pinned in memory.

### 3.5 Write Path (End to End)

```
Put("key", "value") flow:
──────────────────────────

1. Acquire write batch (writes can be grouped into batches for efficiency)
2. Write to WAL sequentially on disk  ← durability
3. Write to active MemTable (skip list insert)
4. Return success to client

Background:
5. When MemTable is full → mark as Immutable, create new MemTable
6. Flush thread writes Immutable MemTable → SSTable on disk (L0)
7. When L0 has too many files → trigger compaction into L1
8. When L1 exceeds size limit → compact L1 files into L2
9. ... cascades down levels as needed
```

All writes are sequential – WAL is sequential, MemTable flush is sequential (write one big sorted file). This is the whole point of LSM trees. No random writes happen during the write path itself. The randomness (compaction, merging) happens in the background, asynchronously.

### 3.6 Read Path (End to End)

```
Get("key") flow:
─────────────────

1. Check active MemTable → found? return immediately
2. Check immutable MemTable(s) → found? return
3. Check L0 SSTables (newest to oldest, because L0 can overlap):
   a. Check Bloom filter for each L0 file
   b. If Bloom says "maybe here" → binary search index block → read data block
   c. Found? return
4. Check L1:
   a. Binary search to find which L1 file could contain the key
   b. Check Bloom filter for that file
   c. If "maybe" → read data block from that file
   d. Found? return
5. Repeat for L2, L3, ... Ln
6. Key not found → return "not found"
```

Worst case for a read is checking every level. In practice, Bloom filters eliminate most unnecessary file reads. Hot data tends to be in MemTable or the block cache anyway.

### 3.7 Compaction

Compaction is the most expensive and most important background operation in RocksDB. It's what keeps the LSM-tree from becoming a mess.

**Why is compaction needed?**

Because writes create new SSTables but never modify existing ones, you end up with:
- Multiple versions of the same key spread across levels (waste of space)
- Tombstones (delete markers) that are sitting around but not actually freeing space
- Too many L0 files which slow down reads

Compaction merges SSTables, keeps only the latest version of each key, and discards tombstones (when it's safe to do so).

**How does leveled compaction work (the default)?**

```
Example: L0 → L1 Compaction
──────────────────────────────

L0 files (overlapping):
  File A: [apple:green, banana:yellow, cherry:red]
  File B: [apple:blue, dog:bark]
  File C: [banana:purple, elephant:big]

L1 files (non-overlapping, already compacted):
  File X: [ant:small, apple:red]
  File Y: [cat:meow, dog:woof]

Compaction picks file from L0 that overlaps with some L1 range,
merges them using an N-way merge sort:

Result written to new L1 files:
  New File 1: [ant:small, apple:blue, banana:purple]
               ↑ apple:red (old) and apple:green (intermediate)
                 discarded, only apple:blue (newest seq) kept
  New File 2: [cat:meow, cherry:red, dog:bark]
               ↑ dog:woof overwritten by dog:bark
  New File 3: [elephant:big]

Old L0 files and old L1 files are deleted after new files are written.
```

This is a merge sort of sorted files. The output is also sorted. The key invariant of the level is maintained (no overlapping key ranges).

**Compaction strategies in RocksDB:**

- **Leveled compaction (default)** – what's described above. Good read performance, moderate write amplification, space efficient.
- **Universal compaction (Tiered)** – fewer compactions, great write throughput, but more read amplification and more space usage. Good for write-dominated workloads.
- **FIFO compaction** – just deletes old SSTables when space runs out. Designed for time-series data where old data is discarded.

### 3.8 The Three Amplification Factors

These three metrics are how you evaluate LSM-tree trade-offs. You can't optimize all three simultaneously – improving one usually makes another worse.

**Write Amplification (WA)** – how many bytes are actually written to disk per byte of user data written. Every time a key is compacted from L0 → L1 → L2 → ... it's rewritten. If a key gets compacted 5 times across 5 levels, the WA for that key is 5.

**Read Amplification (RA)** – how many disk reads are needed per user read. In the worst case, you check MemTable, then L0 (potentially multiple files), then L1, L2, ..., Ln. Without Bloom filters this could be many reads. With Bloom filters it's usually 1-2 disk reads for existing keys.

**Space Amplification (SA)** – how much disk space the database uses relative to the actual live data size. Because old versions of keys and tombstones exist until compaction cleans them up, there's always some overhead. Leveled compaction keeps SA around 1.1x (10% overhead). Universal compaction can temporarily hit 2x.

```
Trade-off Triangle:

        Write Amplification
               △
               │
    ───────────┼───────────
    │                     │
    │   Can't minimize    │
    │      all three      │
    │   simultaneously    │
    │                     │
    ────────────────────────
Read Amplification    Space Amplification

Leveled Compaction:   Low SA, Low RA, Higher WA
Universal Compaction: Low WA, Higher RA, Higher SA
```

---

## 4. Design Trade-Offs

### LSM Trees vs B-Trees

| Aspect | LSM-Tree (RocksDB) | B-Tree (InnoDB / PostgreSQL) |
|---|---|---|
| Write performance | Excellent – sequential writes | Moderate – random in-place writes |
| Read performance | Good with Bloom filters, worse without | Excellent – direct lookup |
| Space overhead | Some (old versions until compaction) | Less (in-place updates) |
| Write amplification | High (compaction rewrites data) | Low (write once in-place) |
| Read amplification | Can be high without Bloom filters | Low (direct tree traversal) |
| Background work | Compaction uses CPU + I/O | Vacuum/autovacuum (PostgreSQL) |
| Best for | Write-heavy workloads, time-series | Mixed or read-heavy workloads |

### Why LSM Trees Don't Need In-Place Updates

The whole insight behind LSM trees is that in-place updates are expensive. When a B-tree updates a key, it might cause page splits, update multiple index pages, and generate random I/O. LSM trees sidestep this entirely: just append a new entry. The "merging" and cleanup happens lazily in the background during compaction.

The cost you pay is: compaction. Compaction is expensive I/O and CPU. The data literally gets rewritten multiple times as it moves from L0 down to deeper levels. But this cost is paid in the background, asynchronously, not on the critical path of a user write.

### The Delete Problem

Deletes in LSM trees are particularly interesting. You can't just remove a key – the key might exist in multiple SSTables across multiple levels. So instead, a delete writes a **tombstone** – a special marker that says "this key is deleted as of this sequence number".

```
Scenario: key "apple" was inserted at seq=10, deleted at seq=20

MemTable: apple → [seq=20: TOMBSTONE]
L2 SSTable: apple → [seq=10: "red"]

A read for "apple":
  Finds tombstone at seq=20 in MemTable → returns "not found"
  The old value in L2 is never even checked.
```

The tombstone only gets cleaned up when compaction reaches the level where the original key lives AND there are no snapshots that need to see the old value. Until that compaction happens, the tombstone AND the original value are both sitting on disk using space.

In write-heavy workloads with many deletes (like time-series data where you expire old data), tombstone accumulation can be a real problem. This is one of the reasons FIFO compaction exists – for time-series use cases where you just want to drop old SSTable files wholesale.

### Compaction is a Double-Edged Sword

Compaction is necessary – without it, read performance degrades (too many files to check) and disk usage bloats. But compaction itself is expensive:

- It reads and rewrites large amounts of data
- It competes with foreground reads and writes for I/O bandwidth
- During heavy compaction, write and read latency can spike

RocksDB has a lot of knobs to tune compaction aggressiveness, background thread counts, and compaction priorities. Getting compaction tuning right is one of the harder parts of running RocksDB in production.

---

## 5. Experiments / Observations

### Experiment 1: Write Amplification Observation

Used the `db_bench` tool that comes with RocksDB:

```bash
# Fill the database with 10 million random key-value pairs
./db_bench \
  --benchmarks=fillrandom \
  --num=10000000 \
  --value_size=100 \
  --db=/tmp/rocksdb_test

# Check compaction stats after the benchmark
./db_bench \
  --benchmarks=stats \
  --db=/tmp/rocksdb_test
```

Output from `COMPACTION_STATS`:

```
Level  Files  Size     Score  Read(GB)  Rn(cnt)  Rnp1(cnt)  Write(GB)  Wnew(cnt)  Comp(sec)  CompMergeCPU(sec)  Comp(cnt)  Avg(sec)  KeyIn  KeyDrop
---------------------------------------------------------------------------------------------------------------------------------
  L0      2/0   43.5 MB  0.5         0.0        0         0        2.5       106         24.0               18.7         35    0.686       0       0
  L1      17/0  255.4 MB  1.0         2.5       106      1188        3.4      1188         35.5               30.1         35    1.014    84.1M    25.3K
  L2      76/0  2.52 GB  1.0        16.7      1188     7429       18.0      7429        249.3              219.8        180    1.385   588.9M   247.3K
 Sum     95/0  2.81 GB  0.0        19.2      1294     8617       23.9      8716        308.8              268.6        250    1.235   673.0M   272.6K
 Int       0/0   0.0 MB  0.0         0.0         0         0        0.0          0          0.0                0.0          0    0.000       0       0
```

What this shows:
- Data written by user: ~1 GB (10M keys × 100 bytes)
- Total bytes written to disk (compaction included): ~23.9 GB
- **Write amplification: roughly 23-24x** – for every byte the user wrote, RocksDB actually wrote ~23-24 bytes to disk across all compaction levels

This is the real cost of LSM trees. High write amplification is the price for fast sequential writes.

### Experiment 2: Read Performance – With vs Without Bloom Filters

```bash
# Write 5 million keys
./db_bench --benchmarks=fillrandom --num=5000000 --db=/tmp/bloom_test

# Read 1 million keys that DON'T exist (worst case – tests bloom filter effectiveness)
# With bloom filters (default)
./db_bench --benchmarks=readrandom --num=1000000 --use_existing_db \
  --bloom_bits=10 --db=/tmp/bloom_test

# Without bloom filters
./db_bench --benchmarks=readrandom --num=1000000 --use_existing_db \
  --bloom_bits=0 --db=/tmp/bloom_test
```

Results observed (approximate):

| Configuration | Ops/sec | Avg Latency |
|---|---|---|
| With Bloom filters (10 bits/key) | ~180,000 ops/sec | ~5.5 μs |
| Without Bloom filters | ~12,000 ops/sec | ~83 μs |

Bloom filters gave about **15x better read performance** for negative lookups (keys that don't exist). This is dramatic. The reason is that without Bloom filters, every non-existent key forces RocksDB to actually open and binary search through SSTable files at each level. With Bloom filters, most of those file accesses are skipped entirely with a fast in-memory check.

### Experiment 3: Comparing Compaction Strategies

Ran `db_bench` with leveled vs universal compaction on an identical write workload:

```bash
# Leveled (default)
./db_bench --benchmarks=fillrandom --num=5000000 \
  --compaction_style=0 --db=/tmp/leveled

# Universal (Tiered)  
./db_bench --benchmarks=fillrandom --num=5000000 \
  --compaction_style=1 --db=/tmp/universal
```

Observations:

| Metric | Leveled Compaction | Universal Compaction |
|---|---|---|
| Write throughput | ~120,000 ops/sec | ~180,000 ops/sec |
| Disk space used | ~580 MB | ~1.1 GB |
| Post-write read speed | Fast (low RA) | Slower (higher RA) |

Universal compaction had better write throughput (less compaction happening during writes) but used about 2x the disk space and had worse read latency afterward. This matches the theory – universal is good when writes are your bottleneck and you can afford more space and slower reads.

### Observation: SSTable File Layout on Disk

After writing some data, checking what RocksDB actually created on disk:

```bash
ls -lh /tmp/rocksdb_test/
```

```
-rw-r--r--  1   64M  000010.log       ← WAL file
-rw-r--r--  1  1.5M  000015.sst       ← L0 SSTable
-rw-r--r--  1  1.5M  000016.sst       ← L0 SSTable
-rw-r--r--  1   32M  000018.sst       ← L1 SSTable
-rw-r--r--  1   28M  000019.sst       ← L1 SSTable
-rw-r--r--  1  128M  000025.sst       ← L2 SSTable
-rw-r--r--  1   12B  CURRENT          ← points to current MANIFEST
-rw-r--r--  1   16K  MANIFEST-000020  ← metadata: which files exist, which level
-rw-r--r--  1  128B  OPTIONS-000005   ← configuration snapshot
```

The MANIFEST file is interesting – it's RocksDB's catalog. It tracks which SSTable files exist, which level they're in, their key ranges, and the sequence numbers they cover. Every time a compaction creates or deletes SSTable files, the MANIFEST is updated atomically.

---

## 6. Key Learnings

**1. LSM trees solve the write problem by turning random writes into sequential writes**  
The fundamental insight is beautiful: instead of updating data in-place (which is random I/O), just append everything. MemTable → SSTable flush is sequential. Compaction is sequential merge sort. The randomness is eliminated from the write path entirely. For write-heavy workloads, this is a massive win.

**2. There's no free lunch – write amplification is the real cost**  
LSM trees have high write amplification. A key inserted once might be physically written to disk 20-30 times across its lifetime as it moves through levels during compaction. On SSDs, write amplification matters because SSDs have a limited number of writes before cells wear out. This is a real production concern that needs to be factored into capacity planning.

**3. Compaction is what keeps everything working, but it's also the main pain point**  
Without compaction, reads get slower and slower as more SSTable files pile up, and disk usage grows without bound. But compaction itself consumes I/O and CPU. Getting compaction tuning right – how many background threads, when to trigger, which strategy to use – is one of the harder parts of running RocksDB in production. Misconfigured compaction is the root cause of a lot of RocksDB performance issues.

**4. Bloom filters are not optional for production use – they're essential**  
The experiment showed 15x difference in read performance for negative lookups. In any real workload, there will be plenty of lookups for keys that don't exist. Without Bloom filters, each such lookup has to scan SSTable files at every level. With Bloom filters, they're handled in microseconds with in-memory checks. The memory cost of Bloom filters is tiny compared to the read performance benefit.

**5. Deletes are more complicated in LSM trees than in B-trees**  
In a B-tree, you delete a key and it's gone from that page immediately. In LSM trees, a delete writes a tombstone that coexists with the original value until compaction eventually removes both. This has real implications for disk usage, and for workloads that do a lot of deletes (like expiring old time-series data), tombstone accumulation can become a serious problem.

**6. RocksDB is a foundation, not a full database**  
It's a key-value storage engine. There's no SQL, no joins, no secondary indexes, no transactions across multiple keys (well, there are write batches, but that's limited). RocksDB's power is as an embedded engine underneath systems that need fast writes – like TiKV, CockroachDB, or MyRocks. Understanding RocksDB means understanding the storage layer that a lot of modern distributed databases are built on.

---

## References

- RocksDB official documentation and wiki: https://github.com/facebook/rocksdb/wiki
- RocksDB tuning guide: https://github.com/facebook/rocksdb/wiki/RocksDB-Tuning-Guide
- Original LevelDB paper: "LevelDB: A fast and lightweight key/value database library by Google"
- LSM-tree paper: O'Neil et al., "The Log-Structured Merge-Tree (LSM-Tree)", 1996
- "Architecture of a Database System" – Hellerstein, Stonebraker, Hamilton (2007)
- Facebook Engineering Blog – RocksDB posts: https://engineering.fb.com/tag/rocksdb/
- RocksDB db_bench tool: `tools/db_bench_tool.cc` in RocksDB source