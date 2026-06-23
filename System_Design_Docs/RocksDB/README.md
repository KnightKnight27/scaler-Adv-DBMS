# RocksDB Architecture (LSM-Tree Storage)

> Advanced DBMS, System Design Discussion
> RocksDB and `db_bench` were not installed in my environment, so the benchmark numbers in section 5 are illustrative. They show the *shape* of the results `db_bench` produces and the trade-offs it reveals, not live captures. The architecture and reasoning are the main focus.

---

## 1. Problem Background

RocksDB is an embedded, high-performance **key-value store** built by Facebook/Meta (2012), forked from Google's LevelDB and optimized for fast storage (SSD/flash) and server workloads. It is a library (like SQLite, not a server), and it sits underneath many bigger systems: **MyRocks** (RocksDB engine for MySQL), **TiKV** (TiDB), CockroachDB historically, **Kafka Streams** state stores, Flink, Ceph, and more.

The problem it targets is write-heavy workloads on flash. A traditional B+tree (like InnoDB) updates data in place, which means every write is a random write to wherever the key lives. Random writes are expensive: they cause page splits, write amplification on SSDs, and they do not use disk bandwidth well. RocksDB uses a different structure, the **Log-Structured Merge tree (LSM-tree)**, whose core idea is to never update in place. It only ever appends, and merges later. This turns random writes into sequential writes, which is exactly what flash (and spinning disks) are good at.

---

## 2. Architecture Overview

```
   WRITE                                                    READ
     │ 1. append to WAL (durability)                          │ check newest → oldest,
     │ 2. insert into active MemTable                         │ stop at first hit:
     ▼                                                        ▼
  ┌─────────────────────────────────┐                  ┌───────────────────┐
  │  MemTable (skiplist, in RAM)     │ ◄────────────────│  active MemTable   │
  │  active  ───full──►  immutable   │                  ├───────────────────┤
  └───────────────┬─────────────────┘                  │ immutable MemTable │
                  │ flush (sequential write)            ├───────────────────┤
                  ▼                                     │  L0 (all files,    │
   ┌────────────────────────────────────────────┐      │   ranges overlap)  │
   │  L0   [sst][sst][sst]   (overlapping ranges) │ ◄────├───────────────────┤
   │  L1   [────sst────][────sst────]  sorted, no │      │  L1 (1 file:       │
   │  L2   [─sst─][─sst─][─sst─]…      overlap     │ ◄────│   binary search)   │
   │  ...                       ~10× per level     │      │  ... bloom filter  │
   │  Ln   [────────────── biggest ─────────────]  │      │  + block cache     │
   └────────────────────────────────────────────┘      └───────────────────┘
                  ▲ COMPACTION merges level N into N+1, drops dead keys
```

**Write path.** A write is (1) appended to the WAL for durability and (2) inserted into the in-memory MemTable. When the MemTable fills, it becomes immutable and a fresh one takes over, and the immutable MemTable is flushed to disk as a sorted SSTable in L0. Background compaction later merges SSTables down the levels.

**Read path.** Check the active MemTable, then the immutable MemTable, then L0 (all files), then L1, L2, and so on, stopping at the first match, using bloom filters and the block cache to skip work.

---

## 3. Internal Design

### 3.1 MemTable (in-memory write buffer)

All writes first land in the MemTable, an in-memory sorted structure (default a skiplist, which supports concurrent inserts and is cheap to flush in sorted order). Defaults:
- `write_buffer_size` = 64 MB, the size of one MemTable;
- `max_write_buffer_number` = 2, how many can exist (one active plus one being flushed). If flushing cannot keep up and this is exceeded, writes stall (back-pressure).

When the active MemTable hits 64 MB it is sealed (becomes immutable), a new active one is created, and a background thread flushes the immutable one to disk.

### 3.2 WAL (durability for the in-memory part)

Since the MemTable lives in RAM, a crash would lose it. So every write is appended to the WAL before or with the MemTable insert. After a crash, RocksDB replays the WAL to rebuild the MemTable. Once a MemTable is safely flushed to an SSTable, its WAL can be discarded. This is the same write-ahead principle as PostgreSQL's WAL and InnoDB's redo log (see the other topics).

### 3.3 SSTables (Sorted String Tables on disk)

A flushed MemTable becomes an SSTable: an immutable, sorted file of key-value pairs. The default `BlockBasedTable` format:

```
 ┌──────────────────────────────────────────────┐
 │ Data block │ Data block │ … (sorted KV, ~4KB) │  ← unit of caching/compression
 ├──────────────────────────────────────────────┤
 │ Filter block (Bloom filter)                   │  ← "is key X possibly here?"
 ├──────────────────────────────────────────────┤
 │ Index block (key → which data block)          │
 ├──────────────────────────────────────────────┤
 │ Footer (fixed size, points to index/meta)     │
 └──────────────────────────────────────────────┘
```

SSTables are immutable. They are never modified, only created and deleted. The block cache (an LRU cache) keeps hot uncompressed data blocks in memory, layered on top of the OS page cache.

### 3.4 Levels (L0 to Ln)

SSTables are organized into levels, and the difference between L0 and the rest is crucial:
- L0 holds freshly flushed MemTables, so its SSTables have overlapping key ranges. A key could be in any of them, so a read must check all L0 files.
- L1 and below have SSTables that are non-overlapping and sorted within the level, so a point lookup checks at most one file per level (binary search on ranges, then within the file).
- Each level is about 10 times larger than the one above (`max_bytes_for_level_multiplier` = 10), starting from `max_bytes_for_level_base` of about 256 MB for L1. So data cascades L0 → L1 → L2 and so on as it ages.

### 3.5 Compaction, and why it is mandatory

Because nothing is updated in place, an `UPDATE` or `DELETE` just writes a newer record (a delete writes a tombstone). Over time the same key has many stale copies scattered across SSTables. Compaction is the background process that merges SSTables, keeps only the newest version of each key, drops tombstoned or expired data, and pushes the result to the next level down. Without it, space would balloon and reads would have to check ever more files.

Three strategies (a tunable trade-off):
| Strategy | How | Write amp | Space amp | Best for |
|---|---|---|---|---|
| Leveled (default) | merge Ln into the overlapping files of Ln+1 | high (often >10x) | low | read-heavy, space-sensitive |
| Universal / tiered | merge similar-sized sorted runs | low | high | write-heavy, can spare disk |
| FIFO | just drop oldest SSTables past a size/TTL | lowest | n/a | caches / time-series |

### 3.6 Bloom filters and the read path

A point read could, in the worst case, check the MemTables and one file per level. Bloom filters make most of that work disappear. Each SSTable stores a small probabilistic filter (default `bits_per_key` = 10, giving about a 1% false-positive rate) that answers "is this key definitely not here?" If the filter says no, RocksDB skips the entire SSTable without reading it from disk. So a typical point read is: MemTables, then for each candidate SSTable check the bloom filter (skip if absent), then the block cache, then only then read a data block from disk.

---

## 4. Design Trade-Offs: the three amplifications

LSM design is best understood through three competing costs (and the **RUM conjecture**: you cannot minimize Read, Update/write, and Memory/space overhead all at once, because improving two worsens the third):

- **Write amplification** is bytes written to disk divided by bytes the app wrote. Compaction rewrites the same data many times as it moves down levels, so LSM trades extra background writes for cheap sequential foreground writes.
- **Read amplification** is how many places a read must check (MemTables plus SSTables per level). Bloom filters and the block cache keep this low, and L0's overlapping files are the main offender.
- **Space amplification** is bytes on disk divided by bytes of live data. Stale versions and tombstones waiting for compaction take space.

**Why LSM is great for writes.** Foreground writes are just a WAL append plus an in-memory insert, which is O(1), sequential, with no random page I/O and no in-place B+tree updates. This is the opposite of InnoDB's clustered B+tree, which does random in-place writes (see [MySQL_InnoDB](../MySQL_InnoDB/README.md)).

**The cost.** Reads are harder (they may touch multiple levels), and compaction is expensive and bursty. It consumes CPU and disk bandwidth in the background and can cause latency spikes or write stalls if it cannot keep up. The strategy knob (leveled vs universal vs FIFO) lets you slide along the write-amp, space-amp, and read-amp trade-off to match your workload.

**LSM vs B+tree in one line.** A B+tree optimizes reads and space (in-place, one place per key) at the cost of random writes, while LSM optimizes writes (append-only, sequential) at the cost of read and space amplification.

---

## 5. Experiments / Observations

These `db_bench` outputs are illustrative. The shapes and relationships are representative, but they are not live runs.

`db_bench` is RocksDB's standard micro-benchmark. It reports throughput (ops/sec, MB/s) and latency (micros/op) per workload:

```
$ ./db_bench --benchmarks="fillrandom,readrandom,overwrite" --num=10000000

fillrandom   :   3.1 micros/op   322000 ops/sec   35.6 MB/s   (random writes, fast: just WAL + memtable)
overwrite    :   4.8 micros/op   208000 ops/sec   23.0 MB/s   (slower: triggers more compaction)
readrandom   :   9.7 micros/op   103000 ops/sec               (slower: may check several levels)
```

**Observation 1: writes are faster than reads.** `fillrandom` (pure writes) outpaces `readrandom`, which is the inverse of a B+tree store. This is direct evidence that LSM is write-optimized: a write is an in-memory insert, while a read may have to consult multiple levels.

**Observation 2: compaction strategy moves the amplification numbers.** Running the same write load under different `compaction_style` produces the classic trade-off:

```
                        write amp    space amp    random-read latency
 Leveled (default)        ~15x         ~1.1x          low      ← rewrites a lot, compact on disk
 Universal (tiered)        ~5x         ~1.5-2x        higher    ← writes less, uses more space
```

This is the RUM conjecture made concrete. Leveled spends write effort to save space and keep reads fast, while universal saves writes by spending space.

**Observation 3: bloom filters cut read amplification.** Disabling bloom filters makes `readrandom` (especially for keys that do not exist) noticeably slower, because every candidate SSTable must be opened and searched instead of being skipped on a 1-bit "definitely absent" answer. With `bits_per_key=10`, about 99% of negative lookups skip the SSTable entirely.

---

## 6. Key Learnings

1. **Append, never update in place.** The whole LSM design follows from refusing random in-place writes: buffer in a MemTable, flush sequentially to immutable SSTables, and fix everything up later via compaction. That is why LSM dominates write-heavy workloads.
2. **Compaction is the price of cheap writes.** You do not pay for the write at write time, you pay later in background compaction (write amplification). Whether that is a good deal depends on your read/write/space mix, which the leveled/universal/FIFO knob lets you tune.
3. **The three amplifications are one trade-off triangle (RUM).** You cannot win all three. Leveled minimizes space and read amp at high write amp, while universal minimizes write amp at the cost of space and read amp. Choosing a compaction style is choosing your corner of the triangle.
4. **Bloom filters are what make LSM reads viable.** Without them, the "check every level" read path would be brutal. A tiny per-SSTable filter lets a read skip files it knows cannot contain the key, which slashes read amplification, especially for missing keys.
5. **L0 is special.** Its files overlap (they are raw flushed MemTables), so reads must check them all. That is exactly why compaction works hardest to drain L0 into the sorted, non-overlapping deeper levels.
6. **It is the same WAL idea everywhere.** RocksDB's WAL, PostgreSQL's WAL, and InnoDB's redo log all solve the same problem: make the fast in-memory write durable without a slow random disk write on every commit. This theme runs through all four topics in this study.

---

## References

- RocksDB Wiki: [MemTable](https://github.com/facebook/rocksdb/wiki/MemTable), [Leveled Compaction](https://github.com/facebook/rocksdb/wiki/Leveled-Compaction), [Compaction styles](https://github.com/facebook/rocksdb/wiki/Compaction), [Bloom Filter](https://github.com/facebook/rocksdb/wiki/RocksDB-Bloom-Filter), [BlockBasedTable format](https://github.com/facebook/rocksdb/wiki/Rocksdb-BlockBasedTable-Format), [Tuning Guide](https://github.com/facebook/rocksdb/wiki/RocksDB-Tuning-Guide), [Benchmarking tools](https://github.com/facebook/rocksdb/wiki/Benchmarking-tools)
- Luo and Carey, [*LSM-based Storage Techniques: A Survey*](https://arxiv.org/abs/1812.07527) (VLDB Journal, 2020)
- Athanassoulis et al., [*Designing Access Methods: The RUM Conjecture*](https://stratos.seas.harvard.edu/files/stratos/files/rum.pdf) (EDBT 2016)
