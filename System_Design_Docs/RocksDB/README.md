# RocksDB Architecture: An LSM-tree Based Embedded Storage Engine

> Advanced DBMS — System Design Study
> Focus: architectural reasoning behind the Log-Structured Merge tree and the amplification trade-offs that govern its tuning.

---

## 1. Problem Background

### 1.1 Where RocksDB comes from

RocksDB is an **embedded, persistent key-value store** that originated at Facebook (Meta) in 2012 as a fork of Google's **LevelDB**. It is a *library*, not a server: it links directly into the host process and exposes a simple ordered map API (`Put`, `Get`, `Delete`, `Merge`, iterators) over byte-string keys and values. There is no network layer, no query planner, and no SQL — those are deliberately left to the layer above (RocksDB sits underneath MySQL/MyRocks, CockroachDB, TiKV, Kafka Streams, Ceph, and many others).

LevelDB was elegant but single-threaded in its compaction and tuned for a single spinning disk. Facebook's workloads were different: many cores, large RAM, and **fast flash (SSD/NVMe)** where random reads are cheap but write endurance and write bandwidth matter. RocksDB was rebuilt to exploit this hardware — multi-threaded compaction, configurable compaction strategies, column families, prefix bloom filters, rate limiters, and a deep set of tuning knobs.

### 1.2 The core problem: write-heavy workloads on flash

The defining design goal is **high sustained write throughput with bounded space and acceptable read latency** on SSDs. Workloads like message queues, time-series ingestion, metrics, social-graph edge updates, and write-ahead replication produce far more writes than a traditional update-in-place engine handles gracefully.

### 1.3 Why LSM-trees instead of B-trees

A **B+-tree** (the classic RDBMS index) updates data **in place**. Every insert or update must locate the target leaf page and write it back. This has two costs:

- **Random writes.** Logically sequential inserts scatter into random page writes across the file. On flash this triggers read-modify-write of whole pages and accelerates SSD wear.
- **Write amplification from page granularity.** Changing one 100-byte row rewrites an entire 4–16 KB page.

An **LSM-tree** inverts the strategy. Instead of mutating data where it lives, it **buffers writes in memory and flushes them as new, immutable, sorted files**, never overwriting existing data. Writes therefore become **large sequential writes**, which flash (and even HDD) loves. The cost is deferred: data accumulates in multiple sorted files that must later be merged (**compaction**), and a read may have to consult several files.

| Property | B+-tree | LSM-tree |
|---|---|---|
| Write pattern | In-place, random | Append-only, sequential batches |
| Write amplification | Moderate, page-granular | Tunable; can be high via compaction |
| Read amplification | Low (1 tree traversal) | Higher (multiple sorted runs) |
| Space amplification | Low (slack in pages) | Tunable; can be higher |
| Best fit | Read-heavy, point/range | Write-heavy ingest, SSD |

The fundamental insight: **LSM-trees trade read and space efficiency for write efficiency, and compaction is the dial that rebalances that trade.** Everything below is about that dial.

---

## 2. Architecture Overview

### 2.1 High-level structure

```
                         WRITE                         READ
                           |                             |
                           v                             |
        +------------------------------------+           |
        |          WAL (on disk)             |  durability
        |   append record before ack         |           |
        +------------------------------------+           |
                           |                             |
                           v                             v
   IN-MEMORY   +----------------+   flush full?   +----------------+
   (RAM)       |   MemTable     |---------------->| Immutable      |
               |  (skiplist)    |                 | MemTable(s)    |
               +----------------+                 +-------+--------+
                                                          | flush
   ==========================================================|=========
   ON-DISK                                                    v
                +----------------------------------------------------+
   L0           | SST | SST | SST | SST   (key ranges OVERLAP)       |
                +----------------------------------------------------+
                                | leveled compaction
   L1           | SST | SST | SST | ...   (sorted, NON-overlapping)  |  ~10x
   L2           | SST | SST | SST | SST | SST | ...                   |  ~10x
   ...          | ...                                                |
   Ln           | largest level, holds most data                    |
                +----------------------------------------------------+
```

The structure is a **memory tier** (MemTable + immutable MemTables) layered over a **disk tier** of increasingly large **levels** of sorted files (SSTables / SSTs). The WAL provides crash durability for the in-memory tier.

### 2.2 Write path (summary)

1. Append the mutation to the **WAL** (sequential disk write) so it survives a crash.
2. Insert into the active **MemTable** (sorted in-memory structure).
3. When the MemTable fills, it becomes **immutable**; a new active MemTable takes over.
4. A background thread **flushes** the immutable MemTable to a new **L0 SSTable**.
5. **Compaction** later merges L0 into L1, L1 into L2, etc.

The user's `Put` returns after steps 1–2 only — disk flush and compaction are asynchronous, which is why writes are fast.

### 2.3 Read path (summary)

A `Get(key)` checks sources **newest-to-oldest** and stops at the first match (the newest version wins):
active MemTable → immutable MemTables → L0 files (newest first) → L1 → … → Ln.
**Bloom filters** are consulted per SSTable to skip files that certainly do not contain the key, avoiding disk I/O.

---

## 3. Internal Design

### 3.1 MemTable and Immutable MemTable

The **MemTable** is the in-memory write buffer. The default implementation is a **skiplist**, chosen because it supports:

- **O(log n) ordered inserts** without rebalancing locks,
- **concurrent lock-free reads** alongside writes, and
- **sorted iteration**, which makes the eventual flush a cheap sequential write of already-sorted data.

When the MemTable reaches `write_buffer_size`, it is sealed into an **immutable MemTable**: still fully queryable from RAM, but no longer accepting writes. A fresh MemTable becomes active so ingestion never stalls (until too many immutable MemTables pile up and backpressure kicks in). A background thread flushes immutable MemTables to disk. Keeping multiple immutable MemTables lets flush keep up with bursty writes.

### 3.2 Write-Ahead Log (WAL)

Because MemTables live in volatile RAM, a crash would lose everything not yet flushed. The **WAL** closes this gap: every mutation is appended to an on-disk log **before** the write is acknowledged. The append is sequential and can be batched/group-committed across many concurrent writers, so durability costs little throughput. After a crash, RocksDB **replays the WAL** to rebuild the lost MemTables. Once a MemTable is safely flushed to an SSTable, its WAL segment is obsolete and can be deleted. (Durability is tunable: `sync` forces fsync per write; the default relies on OS buffering for higher throughput at the cost of a small loss window.)

### 3.3 SSTables (Sorted String Tables)

An **SSTable** is an **immutable, sorted, on-disk file** — the unit of persistent storage. Immutability is what makes the design simple and safe: files are only ever created or deleted, never modified, so readers need no locks and compaction can run concurrently with serving traffic.

A block-based SST is laid out roughly as:

```
+-----------------------------------------------------+
| Data Block 0  (sorted key/value pairs)              |
| Data Block 1                                        |
| ...                                                 |
| Data Block N                                        |
+-----------------------------------------------------+
| Filter Block   (Bloom filter for this SST)          |
| Index Block    (first key of each data block ->     |
|                 block offset)                        |
| Footer         (pointers to index & filter blocks)  |
+-----------------------------------------------------+
```

To read a key: load the footer, binary-search the **index block** to find the one data block whose range could contain the key, then read that **data block** (typically ~4–64 KB). Frequently used blocks are cached in the **block cache**. Data blocks are usually compressed (Snappy/LZ4/ZSTD), trading CPU for I/O and space.

### 3.4 Levels L0 .. Ln

Disk data is organized into **levels** of growing capacity (commonly each level ~10× the previous: L1 = 256 MB, L2 = 2.5 GB, L3 = 25 GB, …).

- **L0 is special.** Its SSTables come straight from MemTable flushes, so their **key ranges overlap** each other. A point lookup may have to check **every** L0 file. RocksDB therefore limits L0 file count and triggers compaction when it grows.
- **L1 and below are partitioned.** Within any single level Ln (n ≥ 1), SSTables have **non-overlapping, disjoint key ranges**. A lookup in such a level touches **at most one file**, found via binary search over file boundaries.

This distinction is the heart of leveled performance: deep levels are cheap to read because each holds a clean, sorted, non-overlapping partition of the keyspace.

### 3.5 Bloom Filters

A **Bloom filter** is a compact probabilistic set summary stored per SSTable. For a point lookup it answers "is this key possibly here?" with:

- **"definitely not"** → skip the file entirely, **no disk read**, or
- **"possibly yes"** → may need to read the data block (could be a false positive).

It never produces false negatives. At ~10 bits/key the false-positive rate is roughly ~1%, so the vast majority of files that do *not* hold the key are eliminated without I/O. This is decisive for the LSM read problem: without filters a point lookup might probe many files across many levels; with filters it usually touches only the one or two files that actually hold (or falsely seem to hold) the key. (Bloom filters help point lookups; they do not help range scans, which still must merge across files.)

### 3.6 Compaction

Compaction is the background process that **merges sorted files into fewer/larger sorted files**, and it is what keeps the LSM-tree healthy. It serves three purposes:

1. **Reduce the number of sorted runs** a read must consult (controls read amplification).
2. **Reclaim space** by dropping superseded versions and **tombstones** (controls space amplification).
3. **Push data into deeper, more compact levels** so it is stored efficiently.

Because SSTables are sorted, merging is a linear **k-way merge**: read overlapping inputs, emit a single sorted output, then atomically swap new files in and delete old ones.

**Tombstones.** A `Delete` cannot erase data in an immutable file, so it writes a **tombstone** marker. The tombstone shadows older versions during reads. It can only be physically removed once compaction has merged it past **all** older data for that key — otherwise a deleted key could "resurrect." Tombstones that linger in upper levels are a classic source of slow scans.

**Two strategies:**

- **Leveled compaction.** Each level is one sorted run of non-overlapping files. Compacting picks a file in Ln and merges it with the overlapping files in Ln+1. Keeps each level fully sorted → **low read and space amplification**, but data is rewritten on the way down each level → **high write amplification**.
- **Universal / tiered compaction.** Newer SSTables (sorted runs) accumulate and are merged only when enough have piled up, rewriting data far less often → **low write amplification**, but several overlapping runs coexist → **higher read and space amplification**.

### 3.7 Putting the paths together

**Write path:**
```
Put(k,v)
  └─> append to WAL ────────────► durability
  └─> insert into MemTable (skiplist)  ── ack to client
        │ (buffer full)
        ▼
   Immutable MemTable ── background flush ──► new L0 SSTable
        │
        ▼
   Compaction: L0 → L1 → L2 → ... (merge, drop tombstones/old versions)
```

**Read path:**
```
Get(k)
  ├─ active MemTable?            ── found? return
  ├─ immutable MemTable(s)?      ── found? return
  ├─ L0 SSTs (newest→oldest)     ── bloom check → maybe block read
  ├─ L1 (1 file via binary search, bloom check)
  ├─ ...
  └─ Ln                          ── first hit wins; else "not found"
```

---

## 4. Design Trade-Offs

### 4.1 Why LSM-trees are write-optimized

Writes never seek to a random location and never read-modify-write an existing page. They are **buffered in RAM and flushed in large sequential batches**, then merged in large sequential passes. Sequential I/O is far faster than random I/O on every storage medium, and on flash it also reduces internal garbage collection and wear. The expensive reorganization work is **deferred and batched** into compaction, amortizing it across many writes. That deferral is exactly what makes ingestion fast — and exactly what creates the amplifications below.

### 4.2 The three amplifications

Every LSM design is a balancing act among three quantities. **You cannot minimize all three at once** — improving one usually worsens another (an "RUM"-style trade-off).

- **Write Amplification (WA)** = bytes written to disk ÷ bytes written by the application. Compaction rewrites the same logical data multiple times as it descends levels. *Lower is better for SSD endurance and write throughput.*
- **Read Amplification (RA)** = data sources/bytes read per logical read. More sorted runs (more L0 files, more overlapping runs) → more files and Bloom checks per lookup. *Lower is better for read latency.*
- **Space Amplification (SA)** = bytes on disk ÷ live logical bytes. Obsolete versions and undeleted tombstones occupy space until compaction reclaims them. *Lower is better for storage cost.*

### 4.3 How compaction strategy sets the trade-off

```
        low write-amp                         low read/space-amp
   <--------------------------------------------------------------->
   Universal / Tiered            <----->            Leveled
   (compact rarely,                                 (keep each level
    keep many runs)                                  fully sorted)

   WA: LOW          RA: HIGH     SA: HIGH    | WA: HIGH   RA: LOW   SA: LOW
```

- **Leveled** keeps each level a single non-overlapping sorted run, so reads touch few files (low RA) and obsolete data is reclaimed promptly (low SA) — but pushing data down N levels rewrites it repeatedly (high WA).
- **Universal** lets sorted runs accumulate and merges them infrequently, so each byte is rewritten far fewer times (low WA) — but coexisting overlapping runs inflate reads (high RA) and temporarily double space during big merges (high SA).

The right choice is workload-driven: **ingest-dominant, SSD-endurance-sensitive** → universal; **read-latency- and storage-cost-sensitive** → leveled. Hybrids (leveled with subcompactions, tiered-leveled) tune the middle ground.

### 4.4 Read trade-offs and tombstones

Reads pay for the LSM's write wins. A point lookup may consult MemTables plus several SSTs; **Bloom filters** rescue point lookups, but **range scans cannot use Bloom filters** and must merge across all overlapping runs — so scan-heavy workloads favor aggressive (leveled) compaction. **Tombstones** add a subtler cost: a deleted key still occupies space and must be checked during reads until compaction carries the tombstone past all older data. A large `DeleteRange` or many deletes in cold key ranges can leave "tombstone walls" that slow scans until compaction catches up.

---

## 5. Experiments / Observations

> **Illustrative only.** The numbers below are representative figures of the kind `db_bench` (RocksDB's benchmark tool) reports; they are meant to show *direction and relative magnitude*, not certified results. Actual values depend heavily on key/value size, dataset size, hardware, cache size, and compression. Reproduce with the RocksDB `db_bench` tool, e.g.:
>
> ```bash
> # fill the DB, then run random writes/reads and inspect compaction stats
> ./db_bench --benchmarks=fillrandom,readrandom,stats \
>            --num=50000000 --value_size=1024 \
>            --compaction_style=0   # 0=leveled, 1=universal
> ```
> WA is read from the compaction stats ("Cumulative writes" vs bytes flushed/compacted), RA from `rocksdb.read.amp.*` or files-read counters, SA from on-disk size ÷ live data size.

### 5.1 Leveled vs Universal under a write-heavy, point-read workload

Scenario: ~50 M keys, 1 KB values, write-heavy with mixed point reads, Bloom filter at 10 bits/key.

| Metric | Leveled (style=0) | Universal (style=1) | Interpretation |
|---|---:|---:|---:|
| **Write Amplification** | ~18–30× | ~5–8× | Universal rewrites data far less → much lower WA |
| **Read Amplification** (files/Get) | ~1–3 | ~5–10 | Leveled keeps levels sorted → fewer files per read |
| **Space Amplification** | ~1.1× | ~1.5–2× (peaks higher mid-compaction) | Leveled reclaims promptly; universal holds more obsolete data |
| **Write throughput** | baseline | ~1.5–2.5× higher | Less compaction work frees I/O for ingest |
| **P99 point-read latency** | lower | higher | More sorted runs to probe per read |

### 5.2 How the numbers move as you tune

| Change | Effect on WA | Effect on RA | Effect on SA |
|---|---|---|---|
| Increase level fan-out (`max_bytes_for_level_multiplier`) | ↑ | ↓ (fewer levels) | ~ |
| Switch leveled → universal | ↓↓ | ↑↑ | ↑↑ |
| Larger MemTable (`write_buffer_size`) | ↓ (fewer, bigger flushes) | ~ | slight ↑ |
| More Bloom bits/key | ~ | ↓ (fewer false positives → fewer block reads) | slight ↑ (filter size) |
| Allow more L0 files before compaction | ↓ (defer work) | ↑ (more overlapping L0) | ↑ |

### 5.3 Bloom filter impact on reads

| Configuration | Approx. disk block reads per *negative* lookup |
|---|---:|
| No Bloom filter | proportional to number of candidate SSTs across levels |
| Bloom, 10 bits/key (~1% FP) | ~0 most of the time; occasional read on false positives |

The pattern to remember: **moving from leveled to universal trades a large WA reduction for higher RA and SA; Bloom filters cut read amplification on point lookups almost for free.**

---

## 6. Key Learnings

### Why are LSM-trees preferred in write-heavy workloads?

Because they convert random in-place updates into **sequential, batched writes**. Mutations are absorbed in an in-memory MemTable (made durable by a cheap sequential WAL append) and later flushed and merged as large immutable sorted files. There are no random page read-modify-writes, the heavy reorganization is **deferred and amortized** by compaction, and sequential I/O maximizes both throughput and SSD endurance. The result is high, sustained ingest rates that an in-place B-tree cannot match under the same hardware.

### Why can compaction become expensive?

Compaction is the **deferred cost** of cheap writes. To keep levels sorted and to reclaim space, the engine must repeatedly **re-read and re-write the same logical data** as it descends through levels — this is **write amplification**, sometimes 10–30× in leveled mode. Under sustained ingest, compaction competes with foreground traffic for CPU, disk bandwidth, and the block cache; if it falls behind, L0 files pile up, read amplification spikes, and RocksDB applies **write stalls/backpressure** to let compaction catch up. Big merges also need temporary space (transient space amplification) and must scan tombstones and stale versions to garbage-collect them. So the very mechanism that makes reads and space efficient is what makes the engine I/O-hungry.

### How do Bloom filters improve read performance?

A per-SSTable Bloom filter answers "is this key possibly in this file?" with **"definitely not"** or **"possibly yes,"** never a false negative. On a point lookup, every "definitely not" lets RocksDB **skip that file with zero disk I/O**. Since an LSM read may otherwise have to probe several files across several levels, filters eliminate almost all of those probes — at ~10 bits/key the false-positive rate is ~1%, so wasted block reads are rare. This directly attacks the LSM's main weakness (read amplification) for point lookups, though it does **not** help range scans, which must still merge across overlapping runs.

### Practical takeaways

- **Pick compaction by workload.** Read/space-sensitive → leveled; ingest/endurance-sensitive → universal. The choice *is* the WA/RA/SA trade-off.
- **You can't win all three amplifications at once** — tune for the one that bounds your system (SSD endurance, read SLA, or storage cost).
- **Always enable Bloom filters** for point-lookup workloads; size bits/key against the cost of false-positive block reads.
- **Watch L0 file count and compaction debt** — write stalls almost always trace back to compaction falling behind.
- **Mind tombstones** — bulk deletes and `DeleteRange` can degrade scans until compaction reclaims them; schedule compaction or use range deletes carefully.
- **Right-size the MemTable and block cache** — bigger MemTables mean fewer, larger flushes (less WA); the block cache absorbs hot reads that filters can't.

**In one sentence:** RocksDB makes writes cheap by appending immutable sorted files and *paying later* through compaction; mastering it means choosing, per workload, how to spend that bill across write, read, and space amplification — with Bloom filters discounting the read side for free.
