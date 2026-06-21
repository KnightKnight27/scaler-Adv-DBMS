# RocksDB — LSM-Tree Storage, Compaction and Bloom Filters

**Name:** Pratyush Mohanty
**Roll No.:** 24BCS10238
**Version:** RocksDB 11.1.1 (Homebrew, Apple Silicon)
**Data used:** 1,000,000 keys × 116 bytes (16-byte key + 100-byte value), ~110 MB of live data per run.

---

## 1. Setup

RocksDB is a library, not a server, so there's nothing to connect to. I wrote a small C++ program against the RocksDB API to run controlled experiments, and used the `ldb` and `sst_dump` tools (shipped with the Homebrew build) to look at the on-disk files. The Homebrew bottle doesn't include `db_bench`, so the harness plays that role.

A few choices in the harness matter for reading the numbers later:

```cpp
options.compression = kNoCompression;          // so amplification isn't hidden by compression
options.write_buffer_size = 8 * 1024 * 1024;   // 8 MB memtable -> forces many flushes on 110 MB
options.target_file_size_base = 8 * 1024 * 1024;
options.statistics = CreateDBStatistics();     // for write-amp / bloom counters
BlockBasedTableOptions tbl;
tbl.filter_policy.reset(NewBloomFilterPolicy(10));  // 10 bits/key, ~1% false positive
```

Turning compression off is deliberate: the whole point is to measure how many times the engine rewrites data, and compression would muddy that. The 8 MB memtable is small on purpose so that 110 MB of data produces real flushes and compactions instead of sitting in memory.

I ran the harness in several configurations: leveled vs universal compaction, bloom vs no-bloom, and a single bulk load vs repeated overwrites.

---

## 2. The LSM-tree, and why it's the opposite of a B-tree

InnoDB and PostgreSQL both store data in B-trees and update pages more or less in place. That makes reads cheap (one tree, a few page reads) but writes involve random I/O to scattered pages. RocksDB is built on a Log-Structured Merge tree, which inverts the priorities: every write is turned into a sequential append, and the cost is moved to reads and to a background process called compaction.

The write path:

```
  Put(key,value)
     │
     ▼
  1. append to WAL (sequential, on disk)      <- durability
     │
     ▼
  2. insert into MemTable (sorted, in memory)  <- the write is now "done"
     │
     ▼ (memtable fills to 8 MB)
  3. flush MemTable -> a new SSTable at L0      <- sequential write of a sorted file
     │
     ▼ (L0 accumulates files)
  4. compaction merges SSTables down L0 -> L1 -> ... -> L6
```

The key idea is that a write never updates an existing file. It goes to memory, the memtable is later written out whole as an immutable sorted file (an SSTable), and SSTables are only ever merged into new SSTables and deleted, never modified. Nothing on disk is updated in place. This is what makes writes fast, and it's measurable:

```
write throughput (1M keys, leveled): ~485,000 ops/s, full load in ~2.0 s
```

**Observation:** Half a million writes a second on a laptop, single-threaded, because each one is just an in-memory skiplist insert plus a sequential WAL append. There is no B-tree page to locate, read, modify and write back. This is the entire reason LSM trees are chosen for write-heavy workloads.

---

## 3. What's actually on disk

After a load, the database directory looks like this:

```
 000061.log          <- the WAL (write-ahead log)
 000062.sst ... 000075.sst   <- SSTables (the sorted data files)
 MANIFEST-000005     <- the log of which SSTables exist at which level
 CURRENT             <- points at the current MANIFEST
 OPTIONS-000007      <- the options the DB was opened with
 LOG                 <- human-readable RocksDB event log
```

`ldb` shows which SSTable sits at which level:

```
$ rocksdb_ldb --db=... dump_live_files
 000062.sst  level:0
 000063.sst  level:6
 000064.sst  level:6
 ...
```

And `sst_dump` shows what's inside a single SSTable:

```
$ rocksdb_sst_dump --file=000063.sst --show_properties
 raw key size:        1,344,000
 raw value size:      5,600,000
 filter block size:   70,021
 # entries for filter: 56,000
 filter policy name:  bloomfilter
```

**Observation:** Each SSTable is self-contained. It holds sorted key/value data blocks, an index, and its **own bloom filter** baked in. The filter here covers 56,000 keys in 70,021 bytes, which works out to almost exactly 10 bits per key, the value I configured. This matters for the read path: every SSTable can answer "I definitely don't contain key X" from its own embedded filter without reading any data blocks. More on that in section 6.

---

## 4. Levels and compaction

The `rocksdb.levelstats` property shows how the data is distributed across levels. After a single 1M-key load with leveled compaction:

```
Level Files Size(MB)
  0        5       33      <- freshly flushed memtables, keys overlap across files
  1        0        0
  ...
  6       10       78      <- the bulk, compacted into non-overlapping files
```

L0 is special: its files come straight from memtable flushes, so their key ranges **overlap** each other. Every level below L0 is kept fully sorted with **non-overlapping** files, so within L1..L6 a key can be in at most one file. Compaction is the process that takes overlapping L0 files and merges them down into the sorted lower levels, throwing away superseded versions as it goes.

Compaction is not free, and the statistics make the cost concrete. For a single 1M-key load:

```
flush bytes (memtable -> L0): 111.6 MB
compaction write bytes:       164.1 MB
compaction read bytes:        165.6 MB
WRITE AMPLIFICATION:          2.49x   (total bytes written to disk / user bytes)
```

**Observation:** I asked the database to store 110 MB, and it wrote 276 MB to disk (112 MB of flushes plus 164 MB of compaction). That ratio, 2.49x, is **write amplification**: the same data gets rewritten as it's merged from L0 down toward L6. This is the price LSM trees pay for fast writes, and it's why "compaction is expensive" is a real concern, the engine is doing background read-merge-rewrite work that competes with foreground traffic. Importantly, all of that rewriting is still sequential I/O, which is why it's tolerable.

---

## 5. Write vs space amplification: leveled vs universal compaction

A single bulk load doesn't show the real trade-off between compaction strategies, because every key is written exactly once, so there's no stale data to manage. To expose it I ran an **overwrite** workload: write all 1M keys, then overwrite them 4 more times (5 passes, 553 MB written, but still only 110 MB of live data). Then I compared the two main compaction styles.

| 5 overwrite passes | Write amplification | Space amplification | On-disk size |
|--------------------|---------------------|---------------------|--------------|
| **Leveled** (default) | 3.79x | 1.52x | 168 MB |
| **Universal** | 3.38x | **3.13x** | 347 MB |

**Observation:** This is the heart of the LSM design space, and the two numbers move in opposite directions:

- **Universal compaction writes less** (3.38x vs 3.79x). It compacts lazily, merging large runs together less often, so each byte is rewritten fewer times.
- **Universal keeps far more dead data on disk** (space amp 3.13x vs 1.52x). For the same 110 MB of live data it held 347 MB of files, because old, superseded versions of keys hang around in older runs until a big compaction finally removes them. Leveled keeps the layout tighter (168 MB) by compacting more aggressively.

So leveled trades extra write work for a compact, read-friendly layout; universal trades disk space and slower reads for less write work. There is no setting that wins on all three of write cost, read cost, and space at once. This is the **RUM conjecture** (Read, Update, Memory: you can optimize for two, at the expense of the third) made visible in two runs of the same program.

---

## 6. Read path, read amplification, and bloom filters

A read in an LSM tree is inherently more work than in a B-tree. The key could be in the memtable, or in any of the overlapping L0 files, or in exactly one file per lower level. So a lookup may have to check several places, newest to oldest, until it finds the key. That extra checking is **read amplification**.

Bloom filters are what make this affordable. To measure their effect I ran 100,000 lookups of keys that are **absent** but fall inside the stored key range (I stored even keys and queried odd ones, so the engine can't skip a file just by its min/max key, the bloom filter is the only thing that can save the read):

| Leveled compaction | Present-key read | Absent-key read | Bloom lookups that skipped an SST |
|--------------------|------------------|-----------------|-----------------------------------|
| **Bloom on** | 9.54 µs | **0.56 µs** | 438,000 |
| **Bloom off** | 9.06 µs | 4.66 µs | 0 |

**Observation:** With bloom filters, looking up a key that isn't there took 0.56 µs; without them it took 4.66 µs, about 8x slower. The bloom filter let RocksDB skip 438,000 SSTable data-block reads that would otherwise have been needed to prove the key was absent. Note the filter does **nothing** for present keys (9.5 µs either way): if the key is really there, the filter always says "maybe", and the data block has to be read regardless. Bloom filters specifically cut the cost of negative lookups, which dominate many real workloads (checking whether a key exists before inserting, point lookups for missing rows, etc.).

The effect is even larger under universal compaction, where there are more overlapping files to check:

```
Universal, bloom OFF:  absent-key read = 11.10 µs
Universal, bloom ON:   absent-key read =  0.82 µs   (636,000 SST reads skipped)
```

**Observation:** Universal's read amplification without bloom (11.1 µs) is worse than leveled's (4.66 µs), exactly because universal keeps more overlapping runs around. The bloom filter brings both back down to under a microsecond. This is why bloom filters are essentially mandatory on an LSM store: they're what stop read amplification from scaling with the number of files.

---

## 7. Trade-offs

**Writes vs reads.** The LSM tree is the write-optimized mirror image of the B-tree engines. Writes are sequential appends to memory and a log, giving ~485K ops/s here with no random page I/O. The cost lands on reads (which may touch the memtable plus several SSTables) and on compaction. InnoDB and PostgreSQL make the opposite trade: a read is a single tree descent, but a write may require random I/O to a specific page. Write-heavy ingestion, logging, time-series, and similar workloads favor the LSM side; read-heavy or point-update OLTP favors the B-tree side.

**Compaction is the central cost.** Nothing is updated in place, so the only way to reclaim space from overwritten or deleted keys and to keep reads fast is to periodically read data back and rewrite it merged. That produced a measured write amplification of 2.5x (load) to 3.8x (overwrites). It's the LSM's version of PostgreSQL's VACUUM or InnoDB's purge: a background process whose job is to clean up the consequences of never modifying data in place. And like those, it competes with foreground work and can fall behind under sustained load.

**The amplification triangle.** Section 5 showed you cannot minimize write, read, and space cost simultaneously. Leveled compaction (low space amp, higher write amp) is the sensible default for most workloads; universal (low write amp, high space amp) suits write-saturated workloads that can spare the disk. The compaction strategy is the main knob for choosing where on that triangle to sit.

**Bloom filters trade memory for read speed.** 10 bits per key (about 1.4 MB of filter for 1M keys here) bought roughly an 8x speedup on negative lookups. That's cheap memory for the benefit, which is why it's on by default in practice.

---

## 8. What I took away

The cleanest way I can summarize RocksDB is that it makes the exact opposite bet from the two B-tree engines I looked at, and the experiments show the bet from both sides. Writes are almost free because they never touch existing data, just an in-memory structure and a sequential log. In return, reads have to look in several places, and a background compaction process has to constantly rewrite data to keep the store tidy. The 2.5x–3.8x write amplification I measured is the bill for that, paid in the background.

The single most useful experiment was the overwrite test in section 5. Seeing leveled and universal compaction split apart, the same workload producing 168 MB vs 347 MB on disk with write amplification moving the other way, made the RUM trade-off concrete in a way no diagram had. You really do pick two of {cheap writes, cheap reads, small footprint}.

And bloom filters went from "a probabilistic set membership structure" to something I could see earning its keep: 438,000 disk reads skipped, negative lookups 8x faster, and the filter sitting right there inside each SSTable when I dumped it. For a structure whose whole job is to read from many files, the thing that makes it practical is the one structure that lets it avoid reading most of them.
