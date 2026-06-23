# RocksDB Architecture (LSM-Tree Storage Engine)

**Name:** Ayaan Singh &nbsp;|&nbsp; **Roll Number:** 24BCS10659

> A concise study of RocksDB — Facebook's embeddable **LSM-tree** key-value store — explaining the MemTable → SSTable write path, the layered read path, Bloom filters, compaction, and the key performance trade-offs. The amplification numbers below come from a **custom C++ benchmark I wrote and compiled against `librocksdb` 11.1.1** and ran locally; results are quoted inline.

---

## 1. Problem Background

### The Write Bottleneck in B+Tree Systems

B-tree storage engines (PostgreSQL, InnoDB) **update pages in place**. On write-heavy workloads that means lots of **random** disk writes — expensive on spinning disks and a source of write amplification on SSDs (which must erase whole blocks).

The workloads that exposed this most aggressively:
- **Logging and monitoring** — millions of events/second, always appending
- **Time-series databases** — sensor data, metrics, telemetry; writes dwarf reads
- **Distributed key-value stores** — Cassandra, TiKV, CockroachDB storing state at massive scale

These workloads do not need their data instantly organized on disk. They need it *durably accepted* as fast as possible.

RocksDB (forked by Facebook in 2012 from Google's LevelDB, itself based on the 1996 **LSM-tree** paper by O'Neil et al.) was built for exactly this pain: high-ingest workloads on flash. Its core idea:

> **Don't pay for disk organization at write time. Accept writes quickly into sequential structures, and let background processes reorganize later.**

This trades cheap writes for more expensive reads and background merging — a deliberate inversion of the B-tree trade-off. RocksDB is embeddable (a C++ library, like SQLite) and powers MySQL/MyRocks, Kafka Streams, CockroachDB's storage layer (historically), and many others.

---

## 2. Architecture Overview

```
        WRITE path                              READ path
        ──────────                              ─────────
        Put(key, value)                         Get(key)
             │                                     │
             ├──► WAL  (sequential log,            ▼
             │         durability first)      Active MemTable (RAM)
             ▼                                     │ miss
        Active MemTable                       Immutable MemTable
        (RAM sorted skiplist)                      │ miss
             │  fills up                           ▼
             ▼                                 L0 SSTables ───────┐
        Immutable MemTable                         │ miss        │ each file's
             │  flush (sequential)                 ▼             │ Bloom filter
             ▼                                 L1 … Ln  ◄─────────┘ skips files
        L0 SSTables  (may overlap)                                 that can't
             │  compaction (merge-sort)                            hold the key
             ▼
        L1 → L2 → … → Ln
        (each ~10× larger, non-overlapping below L0)
```

### Components at a Glance

| Component | Role |
|---|---|
| WAL | Append-only log; ensures a write survives a crash even before it hits an SSTable |
| MemTable | In-memory sorted structure (skip list); absorbs writes at memory speed |
| Immutable MemTable | Frozen MemTable queued for background flush to disk |
| SSTable | Immutable on-disk sorted file; never modified after creation |
| Level 0 to N | Hierarchy of SSTable files; L0 may overlap, L1+ are non-overlapping |
| Compaction | Background merge process; reclaims space, removes stale versions, reduces read amplification |
| Bloom Filter | Per-SSTable probabilistic structure; eliminates disk reads for absent keys |

Each immutable **SSTable** is laid out as:

```
   +----------------------+
   | Data blocks          |  sorted key-value pairs (optionally compressed)
   +----------------------+
   | Index block          |  one entry per data block → (last key, offset)
   +----------------------+
   | Bloom filter block   |  "might this file contain key X?"  (skips reads)
   +----------------------+
   | Footer               |  magic number + pointers to index / metaindex
   +----------------------+
```

**Write path:** a `Put` is appended to the **WAL** (for crash recovery) and inserted into the in-memory **MemTable** (a sorted skiplist). When the MemTable fills, it becomes **immutable** and a background thread **flushes** it to an **L0 SSTable** (sorted, immutable). **Compaction** later merges SSTables down through levels L1...Ln, where each level is ~10× larger and (below L0) non-overlapping. **Read path:** check MemTable → immutable MemTable → L0 files → L1...Ln, using each SSTable's **Bloom filter** to skip files that cannot contain the key.

---

## 3. Internal Design

### 3.1 MemTable and WAL (the write path)
- **MemTable**: default is a sorted **skiplist** allowing concurrent reads + a single writer; keeps recent writes in RAM. Sized by `write_buffer_size`.
- **WAL**: every write is first appended sequentially to the write-ahead log, so an unflushed MemTable can be reconstructed after a crash. WAL = durability; MemTable = speed.
- A `Put` therefore costs *one sequential WAL append + one in-RAM insert* — no random disk I/O on the hot path. This is why LSM ingests fast.

When the MemTable hits its size threshold, it is sealed into an immutable MemTable and a background thread flushes it to a new L0 SSTable. Writes never stall waiting for disk organization.

### 3.2 SSTables and levels (the on-disk format)
- An **SSTable** is an immutable file of **sorted** key-value pairs, plus a block index and a Bloom filter. Immutability makes them cache-friendly and safe to read without locks.
- **L0** files come straight from MemTable flushes and may have **overlapping** key ranges. **L1...Ln** are kept **non-overlapping** within a level, so a point lookup touches at most one file per level below L0.
- Verified layout from the experiment: after compaction, all data settled into **L6 (2 files, 111 MB)** — the deepest level holds the bulk of the data.

Finding a key in an SSTable: check Bloom filter → if maybe-present, binary search the index block → seek to the right data block → scan within block. All sequential within the file.

### 3.3 Bloom filters (the read accelerator)
A Bloom filter is a probabilistic set membership structure stored per SSTable. On a `Get`, RocksDB asks each candidate file's Bloom filter *"might you contain this key?"* — a **"no"** is definitive and skips the file's data-block read entirely; a **"yes"** might be a false positive (tunable, ~1% at 10 bits/key). This is what keeps reads of **absent** keys cheap despite many levels.

Bloom filters have **no false negatives** — if a key is in the file, the filter always says "possibly present." They can have false positives, but at a 1% false positive rate (typical at 10 bits/key), 99% of unnecessary disk reads are eliminated.

> Verified in [Experiment 3](#experiment-3--bloom-filter-effect): with Bloom filters, absent-key reads ran at **3.72 M ops/s** and **198,069 / 200,000** reads skipped the SST block; with Bloom **off**, the same reads dropped to **478 K ops/s** — roughly **8× slower**.

### 3.4 Compaction (the price of cheap writes)
Compaction merges overlapping/old SSTables into new sorted files, discarding overwritten and deleted (tombstoned) keys. It is **why** reads stay bounded (fewer files to check) and space stays reclaimed — but it re-reads and re-writes data repeatedly in the background, which is the source of **write amplification**.
- **Level compaction** (default): strict level sizes, low space amplification, **higher write amplification**.
- **Universal compaction**: merges into fewer, larger sorted runs, **lower write amplification**, but can temporarily use more space (higher space amplification).

> Verified in [Experiment 2](#experiment-2--compaction-strategy-tradeoff): **Level = 6.09× write amplification, Universal = 3.88×** for the identical workload.

### The three amplifications

Every LSM design decision sits inside this triangle. You cannot minimize all three simultaneously:

```
         Write Amplification
              /\
             /  \
            /    \
           /      \
          /________\
Read Amp         Space Amp
```

| | Definition | Who pays |
|---|---|---|
| **Write amplification** | bytes written to disk / bytes written by user | compaction re-writing data |
| **Read amplification** | data read per logical read | checking MemTable + multiple SST levels |
| **Space amplification** | bytes on disk / live logical bytes | stale/overwritten versions before compaction |

LSM engines must **choose a point in this triangle** — you cannot minimize all three at once. Leveled compaction keeps read amplification low at the cost of write amplification. Universal compaction does the reverse.

---

## 4. Design Trade-Offs

| Decision | Benefit | Cost |
|---|---|---|
| **Out-of-place writes (LSM)** | Sequential, fast ingest; flash-friendly | Reads must check many places; needs compaction |
| **Immutable SSTables** | Lock-free reads, easy caching/backup | Updates/deletes are new records + tombstones (space until compacted) |
| **WAL + MemTable** | Durable yet fast writes | RAM use; WAL replay on recovery |
| **Bloom filters** | Cheap negative lookups (8× here) | Extra RAM/disk per file; false positives |
| **Level vs Universal compaction** | Tune write- vs space-amplification | Cannot win both — it is a dial, not a free lunch |

**LSM vs B-tree (vs PostgreSQL/InnoDB in the sibling topics):** a B-tree does ~1 random write per update but must seek on writes; an LSM does sequential writes but reads may touch several files and it burns background I/O on compaction. **LSM wins write-heavy workloads, B-tree wins read-heavy/range-scan latency-sensitive workloads.**

### RocksDB vs. B+Tree Systems

| Dimension | B+Tree (PostgreSQL/InnoDB) | RocksDB (LSM) |
|---|---|---|
| Write pattern | Random I/O (page modifications) | Sequential I/O (MemTable flush, compaction) |
| Write latency | Higher (maintains order immediately) | Lower (write to memory + WAL) |
| Read latency | Predictable O(log N) | Variable (depends on compaction state) |
| Write amplification | 3-5x | 10-30x (leveled) |
| Background work | Light (WAL + checkpoint) | Heavy (continuous compaction) |
| Best workload | Balanced read/write, OLTP | Write-heavy, high ingestion, time-series |

---

## 5. Experiments / Observations

**Harness:** a custom C++ program (~140 lines) compiled against `librocksdb` 11.1.1 with `clang++ -std=c++20 -lrocksdb` (clang/libc++ required — RocksDB 11's headers use C++20 and the lib is built with libc++). It writes **1,000,000 unique keys in random order** (100-byte values, `compression=kNoCompression` so amplification is not masked, 4 MB MemTable to force real compaction), then does 200k present-key and 200k absent-key point reads, reading RocksDB's internal statistics. Core of the harness:

```cpp
Options options;
options.compression = kNoCompression;          // measure pure LSM amplification
options.write_buffer_size = 4 * 1024 * 1024;    // small MemTable -> many flushes -> real compaction
options.statistics = CreateDBStatistics();
options.compaction_style = (comp=="universal") ? kCompactionStyleUniversal
                                               : kCompactionStyleLevel;
BlockBasedTableOptions t;
if (use_bloom) t.filter_policy.reset(NewBloomFilterPolicy(10, false)); // 10 bits/key
options.table_factory.reset(NewBlockBasedTableFactory(t));

// fillrandom: unique keys [0,N) written in shuffled order (overlapping SSTs -> compaction)
for (auto k : shuffled) db->Put(wo, key_of(k*2), val);          // EVEN keys present
db->Flush({}); db->CompactRange({}, nullptr, nullptr);
// readrandom present (even) and absent (ODD keys, interleaved within the key range)

uint64_t userB    = stats->getTickerCount(BYTES_WRITTEN);
uint64_t flushB   = stats->getTickerCount(FLUSH_WRITE_BYTES);
uint64_t compactB = stats->getTickerCount(COMPACT_WRITE_BYTES);
double write_amp  = double(flushB + compactB) / userB;          // write amplification
```

### Experiment 1 — Write amplification (Level compaction)
```
WRITE : 1,000,000 puts in 2.39s = 417,832 ops/s
user bytes written     : 134,000,000
flush bytes -> SST      : 117,708,716
compaction write bytes : 698,073,700
WRITE AMPLIFICATION    : 6.09x   ((flush + compaction_write) / user)
LSM layout: L6 = 2 files, 111 MB   (all data compacted to the deepest level)
```
> **Insight:** each user byte became ~6 bytes written to storage — the compaction tax. The data was rewritten as it migrated L0 → ... → L6. This is the cost LSM pays to keep reads bounded and space tight.

### Experiment 2 — Compaction strategy trade-off
Identical workload, only `compaction_style` changed:
```
Level     compaction:  WRITE AMPLIFICATION = 6.09x
Universal compaction:  WRITE AMPLIFICATION = 3.88x
Space amplification (both, after full compaction) = 1.00x
```
> **Insight:** universal compaction rewrote far less data (3.88x vs 6.09x) because it merges into fewer, larger runs. The textbook counter-cost is *higher space amplification*; here both finished at 1.00x only because the harness forces a final full `CompactRange` that collapses all versions — the space difference is a transient that the final compaction erased. The write-amp gap is the durable, honest result.

### Experiment 3 — Bloom filter effect (absent-key reads)
```
                       absent-read throughput   "bloom useful" (SST reads skipped)
Bloom filter ON   :    3,720,711 ops/s          198,069 / 200,000
Bloom filter OFF  :      477,765 ops/s                0
present-key reads :    ~440-490 K ops/s (all 200,000 found, both configs)
```
> **Insight:** ~8× faster absent-key reads with Bloom filters, because 99% of probes skipped the SST data-block read. *Design note:* absent keys had to be **interleaved** (odd keys among even-numbered present keys) — when I first probed keys lexically beyond all present keys, RocksDB rejected them via each SST's min/max boundary *before* the Bloom filter, so `bloom useful` was 0. The boundary check and the Bloom filter are two distinct read-pruning layers.

---

## 6. Key Learnings and Conclusion

1. **LSM inverts the B-tree trade-off.** Instead of organizing data immediately on disk, RocksDB prioritizes fast writes and performs organization in the background. By writing sequentially and merging later, RocksDB makes writes cheap (417 K ops/s ingest) and pushes the cost onto background compaction and multi-level reads. That is why it is chosen for write-heavy/flash workloads.
2. **Compaction is the load-bearing cost, and it is tunable.** Experiment 2 made the write- vs space-amplification dial real: 6.09x (Level) vs 3.88x (Universal). There is no setting that minimizes write, read, *and* space amplification simultaneously — the "RUM conjecture" triangle.
3. **Bloom filters are what make LSM reads tolerable.** 8x on absent keys (Experiment 3) — without them, every negative lookup would scan a data block at every level.
4. **The boundary check vs. the Bloom filter are separate layers.** My first (wrong) run taught me that SST min/max key bounds prune files *before* the Bloom filter is consulted — a debugging insight you only get by actually running and reading the stats.
5. **Surprising observation:** writing *unique* keys in *random order* is what creates write amplification — sequential/sorted inserts would land in non-overlapping L0 files and compact cheaply. The amplification is a property of the *workload's key order*, not just the engine.
6. **High write throughput comes from deferring organization, not eliminating it.** RocksDB does not make disk writes cheaper — it batches and sequences them. The MemTable converts many tiny random writes into one large sequential flush. Compaction then pays the deferred organization cost in the background, out of the critical write path.

---

## Architecture Reference Map

| Component | RocksDB Location | Key Config / API |
|---|---|---|
| MemTable | `memtable/` (skip list default) | `write_buffer_size`, `max_write_buffer_number` |
| WAL | `db/log_writer.cc` | `wal_dir`, `sync_log_entry_per_write` |
| SSTable | `table/block_based/` | `BlockBasedTableOptions` |
| Bloom Filter | `table/block_based/filter_block*` | `NewBloomFilterPolicy(bits_per_key)` |
| Compaction | `db/compaction/` | `compaction_style`, `max_bytes_for_level_base` |
| Statistics | `monitoring/statistics.cc` | `db->GetProperty()`, `rocksdb.stats`, `getTickerCount()` |

---

## References
- O'Neil, Cheng, Gawlick, O'Neil — *"The Log-Structured Merge-Tree (LSM-Tree)"* (1996)
- RocksDB Wiki — *RocksDB Overview*, *Leveled Compaction*, *Universal Compaction*, *RocksDB Bloom Filter*: https://github.com/facebook/rocksdb/wiki
- RocksDB source and headers: `include/rocksdb/` (used `db.h`, `options.h`, `statistics.h`, `filter_policy.h`, `table.h`), library version 11.1.1
- *RUM Conjecture* — Athanassoulis et al. (2016), on read/update/memory amplification trade-offs
- Hellerstein, Stonebraker, Hamilton — *Architecture of a Database System*

*The benchmark harness was written and run by me against RocksDB 11.1.1 locally; all numbers above are real measurements from that program's output. Original work.*
