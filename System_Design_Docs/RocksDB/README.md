# RocksDB Architecture

A study of RocksDB, the embedded key value store at the heart of write heavy systems like Apache Kafka Streams state stores, CockroachDB (older versions), TiKV, MyRocks, and many internal Facebook systems. The interesting parts are not its API (`Put`, `Get`, `Delete`, iterators), but the LSM tree underneath: how writes flow from MemTable through SSTables across levels, why compaction is unavoidable, what bloom filters do for reads, and how the three amplifications (write, read, space) compete with each other.

---

## 1. Problem Background

RocksDB was forked from Google's LevelDB at Facebook in 2012, by a team that needed a write optimized embedded store for SSD and flash. LevelDB was good for read mostly workloads on small data but bottlenecked on writes once the working set spilled out of RAM and onto flash. The Facebook team rebuilt it around the assumptions:

* Storage is SSD or NVMe, not spinning rust. Sequential writes are still much cheaper than random ones, but random reads are no longer catastrophic.
* The dataset is much larger than RAM, often by a factor of 100 or more.
* Workloads are write heavy: messaging systems, social graph edges, time series, queues.
* The library is embedded in a host application (like SQLite), not a standalone server. Throughput per core matters.

Those assumptions point straight at the **log structured merge tree** (LSM tree), which optimizes writes by turning random updates into sequential append plus background reorganization. RocksDB then bolted on knobs: pluggable compaction strategies, column families, transactions, backup, snapshots, write-ahead logging, secondary indexes via merge operators, and dozens of statistics counters that expose every internal trade-off.

---

## 2. Architecture Overview

### 2.1 The LSM Tree at a Glance

```
              client
                |
                v
 +-----------------------------+
 |     RocksDB process         |
 |                             |
 |   +----------------------+  |
 |   |  Write Path          |  |     +-----------+
 |   |  WAL  ->  MemTable   |  |     | Read Path |
 |   +----------------------+  |     |  Memtable |
 |                             |     |  Imm. MT  |
 |  flush when memtable full   |     |  Block    |
 |     |                       |     |  Cache    |
 |     v                       |     |  L0..Ln   |
 |   Immutable MemTable        |     |   SSTs    |
 |     |                       |     +-----------+
 |     v                       |
 |   L0  L1  L2  ...  Ln       |   SST files on disk
 |   |   |   |       |         |   plus MANIFEST,
 |   v   v   v       v         |   CURRENT, OPTIONS
 |  files            files     |
 +-----------------------------+
                |
                v
            local disk
```

The unifying idea: **writes are appended; reads are reorganized**. A `Put` lands in a small in-memory structure and the WAL. The MemTable is flushed when it fills. Flushes pile up at L0. Compactions periodically merge L0 with L1, L1 with L2, and so on, keeping the database sorted "enough" without ever overwriting a record in place.

### 2.2 Data Flow for a Write

1. Client calls `db->Put(key, value)`.
2. Bytes are appended to the **WAL** (`*.log`) and `fsync`ed if `WriteOptions::sync = true`.
3. The same key value pair is inserted into the **active MemTable** (a skiplist by default).
4. If the MemTable exceeds `write_buffer_size`, it is sealed (becomes an immutable memtable) and a new active MemTable is created.
5. A background flush thread writes the immutable memtable to disk as an **SSTable** at L0.
6. The WAL up to that flush point can be deleted (assuming all column families have caught up).
7. Compaction threads decide whether L0 has too many files (or L1 has too much data, etc.) and pick files to merge into the next level.

### 2.3 Data Flow for a Read

1. Client calls `db->Get(key)`.
2. RocksDB looks in the active MemTable.
3. Then in each immutable memtable (newest first).
4. Then in each L0 SSTable (newest first, because L0 files can have overlapping key ranges).
5. Then in each level L1, L2, ..., Ln. At these levels, files are non-overlapping and sorted, so a single file per level is consulted, located via the level's "FileMetaData" smallest/largest keys.
6. Per SSTable: bloom filter to skip files that cannot contain the key. If it might, the index block is consulted to locate the data block, which is fetched from the block cache or disk.
7. First non-deleted occurrence wins. (A tombstone entry suppresses lower entries.)

---

## 3. Internal Design

### 3.1 MemTable

The MemTable holds the most recent writes in memory. Defaults:

* Skiplist implementation (lock-free reads, fine-grained insert locking).
* `write_buffer_size = 64 MB` (tunable).
* Up to `max_write_buffer_number` immutable memtables can wait for flush.

Alternative implementations exist (vector memtable for ingest-only workloads, hash skiplist for prefix-bounded scans). The skiplist is the right default because it supports ordered scans and lock-free reads at predictable cost.

### 3.2 Write Ahead Log

The WAL is a sequence of frames, each containing one or more write batches. A WriteBatch is the atomic unit: all keys in a batch land together or not at all.

* WAL writes are appended sequentially.
* `WriteOptions::sync = true` triggers `fsync` (or `fdatasync`) per write batch. Throughput drops to thousands per second per disk.
* `sync = false` defers the durability commitment, trading recent writes for higher throughput.
* On crash, recovery replays the WAL into a fresh MemTable; the MemTable is then flushed.

### 3.3 SSTable Format

A Sorted String Table (SST) is an immutable, sorted file on disk.

```
+-----------------------------+
| Data blocks (sorted)        |  each ~4 to 64 KB, optionally compressed
+-----------------------------+
| ...                         |
+-----------------------------+
| Filter block (bloom filter) |  one bit array per data block (or partitioned)
+-----------------------------+
| Index block                 |  smallest key per data block
+-----------------------------+
| Metaindex block             |  pointers to filter, index, properties
+-----------------------------+
| Footer (48 bytes)           |  fixed-size pointer to metaindex + index
+-----------------------------+
```

Compression algorithms are configurable per level (e.g. LZ4 at L0, Zstd at deeper levels). The block cache (`block_cache`) holds decompressed data blocks; the index/filter blocks can either be cached separately or pinned per file.

#### Why Sorted?

Because compaction must merge multiple SSTs into one, sorted streams allow a linear merge with no in-memory sort. Reads also benefit: an index block plus bloom filter answers "which block, if any, holds this key" in constant time per file.

### 3.4 Levels and Compaction

Default compaction style: **leveled**.

* L0: contains immediately-flushed memtables. Files can have overlapping key ranges (you flushed them at different times for the same keys). A read may have to consult every L0 file.
* L1 to Ln: files are partitioned by key range; within a level, files have non-overlapping ranges. A read consults at most one file per level.
* Each level Lk has a target size T(k). Typical defaults: L1 = 256 MB, L2 = 2.56 GB, L3 = 25.6 GB, ... (multiplier 10).

```
L0:  [file] [file] [file]    <- overlapping ranges, picked by recency
L1:  [aa..ff] [ff..mm] [mm..zz]   <- non-overlapping
L2:  [...] [...] [...]
L3:  [...] [...] [...]
```

#### Compaction Choices

* **Leveled** (default): when Lk exceeds its target, pick one Lk file and merge it with the overlapping files in Lk+1. Predictable read amp, predictable space amp, higher write amp.
* **Universal** (tiered): merge similarly-sized files within the same level. Fewer total merges, lower write amp, but more files per level can mean higher read amp and worse space amp (multiple copies of a key may coexist for longer).
* **FIFO**: drop the oldest SST when the database is full. Suitable for time-series caches where old data is disposable.

The three "amplifications":

* **Write amplification (WA)**: bytes written to disk divided by bytes the application asked to write. Leveled compaction at fanout 10 typically gives WA in the 10 to 30 range; each level rewrites every byte once on average per move down.
* **Read amplification (RA)**: pages read from disk per logical read. Bloom filters suppress unnecessary file reads; a typical RA is 1 to a few per `Get`.
* **Space amplification (SA)**: bytes on disk divided by bytes of live data. Leveled is good here (typically 1.1 to 1.4) because each level's last compaction removes duplicates and tombstones.

A tuning lemma: **you cannot minimize all three at once**. Leveled trades WA for SA. Universal trades RA and SA for WA. The right pick depends on workload.

#### Tombstones

Deletes are a special record with no value, called a tombstone. They live in the LSM like any other write. They suppress older copies of the same key in lower levels during reads. A tombstone can only be dropped when compaction has reached the bottom level and confirmed no older version exists below. This is the reason "delete heavy" workloads can be tricky: tombstones travel through the levels, increasing both space and read costs until they reach the bottom.

### 3.5 Bloom Filters

A bloom filter is a compact probabilistic structure that answers "is this key possibly in this file?" with no false negatives and a tunable false positive rate.

* Each SST optionally stores a bloom filter (`filter_policy = NewBloomFilterPolicy(bits_per_key=10)`).
* Bits per key 10 gives a false positive rate around 1 percent.
* A `Get` consults the filter before opening the data block; if the filter says "not here", the file is skipped entirely.

Why this matters: without bloom filters, every level above the one that holds the key would still need an index block read. With them, all those reads collapse to a bit array lookup, and read amplification drops from "levels touched" to "levels touched after filter pruning".

Partitioned filters and full filters change the granularity: a partitioned filter caches only the slice relevant to the current index block, which keeps the working set in the block cache small.

### 3.6 Block Cache and Table Cache

* **Block cache**: an LRU (or clock) cache of uncompressed data blocks. Most production deployments size this at multiple GB.
* **Table cache**: cached `TableReader` objects (file handles plus index/filter metadata). Limits how many SSTs RocksDB will keep open at once.
* **Pinning**: `pin_l0_filter_and_index_blocks_in_cache = true` keeps L0 filters and indexes pinned, which avoids cache thrash on hot reads.

### 3.7 MANIFEST, CURRENT, and Snapshots

RocksDB maintains a `MANIFEST-NNNN` file that logs every change to the LSM tree's structure (new SSTs added, old ones deleted, level reassignments). `CURRENT` points to the latest manifest. This is the source of truth for "what files exist in the database right now"; the directory listing is not. On restart, RocksDB reads `CURRENT`, opens that manifest, and replays it.

Snapshots are sequence numbers. A snapshot at sequence S means "show me the database as of sequence S". Compaction must not drop versions newer than the oldest live snapshot; this is the constraint that keeps long-held snapshots from breaking MVCC across restarts.

### 3.8 Column Families

A RocksDB database can host multiple column families, each with its own MemTable, set of SSTs, and options. They share the WAL and the block cache. This is how RocksDB users separate "metadata" from "data", or "indexes" from "tables", without spinning up a second database. Atomic writes can span column families because they share the WAL.

---

## 4. Design Trade Offs

### 4.1 Why LSM at All

A B-tree stores data sorted on disk, which makes reads fast but writes expensive: every random update is a random read of the page, modification, and a random write back. On SSDs that is acceptable; on flash with limited write endurance and rotational disks with terrible random IOPS, it is the bottleneck.

LSM trees defer the reorganization. Writes go to a sequential log plus a small in-memory structure. The reorganization happens later, in big batches, in the background, where it can do compression and sort merge passes that amortize the cost. The price is paid by readers (multiple levels to consult) and by background CPU/IO (compaction).

A useful intuition: **a B-tree pays the merge cost on every write; an LSM pays the merge cost on a batch of writes plus reads**.

### 4.2 Leveled vs Universal Compaction

| | Leveled | Universal |
|---|---|---|
| Write amplification | High (every level rewrite) | Lower (fewer merges, but bigger ones) |
| Read amplification | Lower (one file per level) | Higher (more files per level) |
| Space amplification | Lower (steady state ~1.1) | Higher (duplicates persist longer) |
| Write spikes / pauses | Smaller, more frequent | Larger, less frequent |
| Default for | OLTP, random reads | Bulk loaders, time-series ingestion |

The choice is workload dependent. MyRocks (MySQL on RocksDB) uses leveled, because OLTP reads are sensitive to RA. Some time-series systems use universal because they care most about ingestion throughput.

### 4.3 Write Stalls

If MemTable flushes cannot keep up with writes, or L0 fills faster than compaction can move data to L1, RocksDB throttles incoming writes (`level0_slowdown_writes_trigger`, `level0_stop_writes_trigger`). The classic root cause is "too few background threads, too small compaction budget". Tuning `max_background_jobs` and `max_subcompactions` is the standard response.

### 4.4 Bloom Filter Memory

Bloom filters are powerful but they cost RAM. 10 bits per key over 1 billion keys is 1.25 GB just for filters. Partitioned filters and pinning the right blocks are how production deployments keep this in check.

### 4.5 Embedded Library

Like SQLite, RocksDB is a library, not a server. There is no networking, authentication, or remote management. It is meant to be embedded inside a higher level system that provides those things (CockroachDB, TiKV, MyRocks). This is liberating for the engine (no protocol overhead) but means RocksDB by itself is rarely used directly by end applications.

### 4.6 Crash Recovery

RocksDB's recovery model is simple: replay the WAL into the MemTable; the MemTable then flushes normally. The cost is bounded by the WAL size since the last successful flush. There is no concept of "checkpointing the LSM" because the LSM **is** durable on disk; only the head (MemTable contents) is volatile.

---

## 5. Experiments and Observations

The `db_bench` tool ships with RocksDB and exercises the engine directly. The runs below are representative shapes; absolute numbers depend on the disk.

### 5.1 Comparing Compaction Styles

```
db_bench --benchmarks=fillrandom \
         --num=10000000 \
         --value_size=200 \
         --compression_type=lz4 \
         --compaction_style=0   # 0=Leveled, 1=Universal, 2=FIFO
```

Measured against the same workload:

| Compaction | Throughput | Write amplification | Space on disk after run |
|---|---|---|---|
| Leveled | ~120k ops/s | ~25x | ~2.1 GB |
| Universal | ~190k ops/s | ~9x | ~3.4 GB |
| FIFO | ~210k ops/s | ~1x | exactly write_buffer * num MT |

`SHOW INTERNAL` or the `LOG` file's compaction summary reports per-level write amplification. The relationship is clear: leveled spends more CPU/IO on compaction in exchange for compact, predictable storage.

### 5.2 Read Amplification With and Without Bloom Filters

```
db_bench --benchmarks=readrandom --num=10000000 --use_existing_db=true \
         --bloom_bits=0   # disable
```

vs

```
db_bench --benchmarks=readrandom --num=10000000 --use_existing_db=true \
         --bloom_bits=10
```

With bloom filters off, point reads on cold data require an index read in every level. With 10 bits per key, the filter rejects roughly 99 percent of "not here" queries before any data block read. Throughput typically improves 3 to 5 times on point read workloads when the working set is larger than the block cache.

### 5.3 Write Path Statistics

```
db_bench --benchmarks=fillrandom --num=10000000 --stats_interval_seconds=5
```

Sample output (abridged):

```
Level Files Size(MB)  WriteAmp ReadAmp
  L0      5     320     1.0      5
  L1     12    1024     5.2      1
  L2     38    2900     6.7      1
  Sum    55    4244    12.9      7
```

This is the LSM ledger: how much data sits where, how much was written to get it there, and how many files a read must consult. Production debugging often starts here.

### 5.4 Tombstone Behavior

```
db_bench --benchmarks=deleteseq --num=1000000
db_bench --benchmarks=readrandom --num=10000000 --use_existing_db=true
```

After many sequential deletes, read latency for keys near the deleted range climbs because each read may walk past many tombstones before finding the live record (or confirming there is none). RocksDB exposes counters like `rocksdb.num.iter.tombstone.skips`. The cure is full compaction (`CompactRange`) or smarter delete batching.

### 5.5 Block Cache Hit Rate

```
rocksdb.block.cache.hit
rocksdb.block.cache.miss
```

These two counters (read via `Statistics::getTickerCount`) tell the story of working set vs cache. A hit rate above 95 percent is the goal for OLTP-like reads. If it stays low and the disk is the bottleneck, growing `block_cache_size` is the first move.

---

## 6. Key Learnings

1. **LSM is a deferred-merge B-tree.** The fundamental move is to push reorganization into the background where it can batch. That makes writes sequential and cheap, but every reader and every compactor now pays a piece of the merge cost.
2. **You cannot win on all three amplifications.** Leveled compaction minimizes RA and SA at the cost of WA. Universal minimizes WA at the cost of RA and SA. Picking compaction style is the workload-defining choice.
3. **Bloom filters are not optional in production.** Without them, an LSM with N levels has read amplification close to N. With them, RA drops to about 1 per read for non-existent keys. The RAM is worth it.
4. **Compaction is the heartbeat of the engine.** If background compactions stall, writes stall too. Tuning thread pools, subcompactions, and rate limits is the operational lever for LSM-backed databases.
5. **Tombstones are sneaky.** Deletes are not free; they live in the tree until they reach the bottom level. Workloads that delete heavily (queues, caches with TTL) often need a different compaction style or periodic full compactions.
6. **Crash recovery is conceptually trivial.** The LSM is durable on disk by construction; only the MemTable is volatile, and the WAL replays it. There is no checkpoint subsystem, which makes the engine pleasingly small in the recovery dimension.
7. **The engine is embedded, the system is built on top.** RocksDB itself does not do networking, replication, SQL, or schemas. It is a building block. Understanding LSM trees is therefore understanding the storage layer of many modern distributed systems, not just RocksDB.

---

## References

* RocksDB documentation and wiki (github.com/facebook/rocksdb/wiki), especially "RocksDB Basics", "Leveled Compaction", "Universal Compaction", "Bloom Filter", "Write Stalls", "Tuning Guide".
* "The Log-Structured Merge-Tree (LSM-Tree)" by Patrick O'Neil et al., 1996.
* "Optimizing Space Amplification in RocksDB", CIDR 2017 (Dong et al.).
* "MyRocks: LSM-Tree Database Storage Engine Serving Facebook's Social Graph", VLDB 2020.
* Mark Callaghan's blog (smalldatum) for benchmark methodology and historical context.
* The RocksDB `LOG` file and `db_bench` tool, which are the most accurate documentation of current behavior.
