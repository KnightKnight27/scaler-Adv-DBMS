# RocksDB Architecture

RocksDB is an embeddable, persistent key-value store built by Facebook, derived
from Google's LevelDB and tuned for fast storage (flash and SSD) and server
workloads. Unlike the B-tree engines behind PostgreSQL and InnoDB, RocksDB is built
on a Log-Structured Merge-tree (LSM-tree), a design that turns random writes into
sequential ones and trades some read and space efficiency for very high write
throughput. This document explains how an LSM-tree works (MemTable → SSTables →
compaction), why it is write-optimized, and the amplification trade-offs it
accepts, backed by a small LSM-tree simulator I wrote in C++ to measure those
trade-offs directly.

RocksDB is a library, not a server. You link it into your application, and it powers
systems like MySQL's MyRocks, CockroachDB, Kafka Streams, and TiKV. It replaces an
embedded storage engine, not a full SQL database.

---

## 1. Problem Background

By the early 2010s two things had changed about storage. First, SSDs made
sequential writes much cheaper than random writes and could handle high
parallelism. Second, web-scale services were generating write-heavy workloads
(logging, metrics, messaging queues, counters) where a traditional B-tree
struggles, because every update is a random in-place write that may dirty a
different page and eventually force a random disk write.

A B-tree optimizes for reads, since one O(log n) descent finds any key, at the cost
of random writes. RocksDB's LSM-tree makes the opposite bet: optimize for writes by
never updating data in place. Every write is buffered in memory and later flushed
sequentially, and older data is reorganized in the background. This is ideal when
you ingest far more than you read, or when write latency dominates.

RocksDB's stated assumptions and goals:
- Run as a library embedded in the application, on fast local storage.
- Optimize for high write throughput and range scans over sorted keys.
- Be highly configurable. Compaction style, memory, and filters are all tunable
  knobs, because no single setting fits all workloads.

---

## 2. Architecture Overview

An LSM-tree spreads data across a small in-memory part and a large on-disk part
organized into levels. Writes only ever touch memory and the log; disk files are
immutable once written, and are reorganized only by compaction.

```
   WRITE PATH                                    READ PATH
   ─────────                                     ─────────
   put(k,v)                                      get(k) checks newest → oldest:
      │                                            1. active MemTable
      ├──► WAL (sequential append, for crash)      2. immutable MemTable(s)
      ▼                                            3. L0 SSTs (may overlap)
   ┌──────────────┐  full                          4. L1 SST (1 file via bloom)
   │ active MemTab │ ───► immutable MemTable        5. L2 SST ...
   └──────────────┘          │ flush (sequential)   ▼
                             ▼                      bloom filter skips SSTs
                    ┌───────────────────┐           that can't hold the key
                    │  L0: SSTables      │  (overlapping ranges)
                    ├───────────────────┤
                    │  L1: SSTables      │ ← non-overlapping, sorted
                    │  L2: 10x bigger    │
                    │  ...               │
                    │  Ln: largest       │
                    └───────────────────┘
                         ▲ compaction merges level i into i+1,
                           dropping overwritten/deleted keys
```

Key components:
- MemTable. An in-memory sorted structure (a skip list by default) holding the
  newest writes.
- WAL (write-ahead log). Every write is appended here first, so an unflushed
  MemTable survives a crash.
- Immutable MemTable. When the active MemTable fills, it is frozen and a new one
  takes over, and the frozen one is flushed in the background.
- SSTable (Sorted String Table). An immutable, sorted on-disk file. Once written it
  is never modified, only eventually replaced by compaction.
- Levels L0 to Ln. L0 files come straight from flushes and may have overlapping key
  ranges; L1 and below are kept non-overlapping, and each level is roughly 10x the
  size of the one above.
- Bloom filters. A compact probabilistic structure per SSTable that answers "is key
  K definitely not here?", letting reads skip files cheaply.

---

## 3. Internal Design

### 3.1 The write path — why LSM is write-optimized

A write does only two cheap, sequential things: append to the WAL and insert into
the in-memory MemTable. That's it. The expensive work of organizing data on disk is
deferred. When the MemTable fills, it is frozen and flushed to a new L0 SSTable in
one sequential write. There are no random in-place updates, ever. An update or
delete is just a newer entry (a delete writes a tombstone) that shadows the old one
until compaction removes it.

This is the core reason LSM-trees sustain huge write rates: random user writes are
absorbed in RAM and hit disk only as large sequential flushes.

### 3.2 The read path — and why it's harder

A read is the price you pay. Because a key can exist in several places (the
MemTable, multiple L0 files, and one file per lower level), a `get` must check them
newest-to-oldest and stop at the first hit. Without help, that is many file probes
per read. Two mechanisms rescue read performance:

- Bloom filters let a read skip any SSTable that definitely doesn't contain the key,
  turning "check every file" into "check about one file."
- Sorted, non-overlapping lower levels mean that below L0, at most one file per
  level can contain a given key, so a binary search locates it directly.

My simulator (below) measured this. With 4 sorted runs, a naive read touches all 4,
but with 1%-false-positive bloom filters it touches only 1.03 on average, a 74%
reduction in SSTable reads.

### 3.3 Compaction — the engine that keeps it usable

Because flushes keep producing new SSTables and overwrites and deletes pile up,
something has to merge files, drop shadowed versions, and keep the level structure
sorted. That background process is compaction. RocksDB offers several styles, and
the choice is the central tuning decision:

| Style | Optimizes for | Pays in |
|---|---|---|
| Leveled (default) | low space and read amplification | higher write amplification |
| Universal / tiered | low write amplification | higher space and read amplification |
| FIFO | time-series and cache (drop oldest) | not general purpose |

Leveled compaction keeps each level non-overlapping and about 10x the previous, and
merging level i into i+1 rewrites the overlapping slice. This keeps space and read
amplification low but rewrites data many times as it sinks through the levels, which
is where the high write amplification comes from.

### 3.4 The three amplifications — the LSM trade-off triangle

Every LSM design balances three competing costs, and you cannot minimize all three
at once:

- Write amplification is bytes written to disk divided by bytes of user data. It is
  high, because data is rewritten at every level during compaction.
- Read amplification is the number of files or runs probed per lookup. It is
  mitigated by bloom filters and sorted levels.
- Space amplification is bytes on disk divided by bytes of live data. It is caused
  by stale (overwritten or deleted but not yet compacted) entries.

Compaction is the lever that moves cost between these three, which is why RocksDB
exposes it as a first-class, pluggable choice.

### 3.5 Durability and column families

Durability comes from the WAL. A write is acknowledged only after its WAL record is
persisted, so an unflushed MemTable can be replayed after a crash. Once a MemTable
is safely flushed to an SSTable, the corresponding WAL segment can be discarded.
RocksDB also supports column families: independently configured key spaces inside
one database that share a WAL, which gives atomic cross-family writes and consistent
recovery.

---

## 4. Design Trade-Offs

Advantages
- Outstanding write throughput. Random writes become sequential WAL appends plus
  MemTable inserts, and disk sees only large sequential flushes.
- Strong compression and space efficiency on cold data, since immutable SSTables
  compress well and leveled compaction removes stale versions.
- Fast range scans over sorted keys, and tunability for very different workloads via
  compaction style, MemTable size, and filter settings.

Limitations
- Read amplification. A point lookup may consult several runs, and without bloom
  filters, reads are noticeably slower than a B-tree's single descent.
- Write amplification. Leveled compaction can rewrite data 10 to 30 times over its
  lifetime (my model measured 24x), which wears flash and consumes I/O bandwidth.
- Compaction is background work that competes with foreground traffic. Poorly tuned,
  it causes latency spikes and write stalls.
- No SQL and no built-in cross-system transactions by default. It is a storage
  engine, the foundation other systems build on.

### Contrast with B-tree engines (PostgreSQL / InnoDB)
A B-tree gives low, predictable read cost and updates in place, but random writes
are its weak point. An LSM-tree inverts this: cheap sequential writes, but reads and
space need active management through bloom filters and compaction. The right choice
is workload-driven. Write-heavy ingestion leans LSM; read-heavy with point or
secondary lookups leans B-tree. This is exactly why MyRocks (RocksDB under MySQL)
exists alongside InnoDB: same SQL layer, opposite storage trade-off.

---

## 5. Experiments / Observations

RocksDB ships `db_bench` for exactly this measurement, but it isn't packaged on this
machine. To measure the trade-offs first-hand I instead wrote a roughly 120-line
C++ LSM-tree simulator (`lsm_sim.cpp`) that models the real dynamics: a MemTable
that flushes to L0, leveled compaction with 10x fan-out and bounded overlap, and a
bloom-filter read model. It is a simplification, not RocksDB itself, but it
reproduces the same qualitative behavior. Workload: 2,000,000 writes of 100-byte
values over 200,000 distinct keys, so most writes are overwrites.

```
==== LSM-tree simulation (RocksDB-style leveled compaction) ====
user puts             : 2000000  (value=100B)
distinct keys         : 200000

-- WRITE amplification --
user bytes            : 200.0 MB
physical bytes written: 4820.8 MB
write amplification   : 24.1x          ← data rewritten ~24x by compaction

-- READ amplification --
sorted runs on disk   : 4
SST touches/read no bloom : 4.00       ← must check every run
SST touches/read w/ bloom : 1.03       ← bloom skips the rest
bloom read I/O reduction  : 74%

-- SPACE amplification --
live user data        : 20.0 MB
data on disk          : 40.0 MB
space amplification   : 2.00x          ← stale versions awaiting compaction
```

Observations:
1. Writes are amplified about 24x. Each piece of data is physically rewritten many
   times as it migrates down the levels. This is the cost the LSM-tree pays for
   absorbing all those writes sequentially. RocksDB's own documentation cites a
   similar 10 to 30x for leveled compaction, so the model is in the right regime.
2. Bloom filters are transformational for reads. They cut SSTable touches from 4.00
   to 1.03 per lookup, a 74% reduction. Without them, the read amplification of an
   LSM-tree would make it uncompetitive with a B-tree; with them, a point lookup is
   effectively one file access.
3. Space amplification is modest but real, around 2x here. Overwritten values linger
   on disk until compaction reclaims them, which is the same root cause as
   PostgreSQL's dead tuples, just managed by compaction instead of VACUUM.
4. Compaction is the dial. The point of the experiment is that these three numbers
   move together: pushing write-amp down (universal compaction) would push read- and
   space-amp up. There is no free lunch, only a choice of which cost to pay.

(`lsm_sim.cpp` is included alongside this README for reproducibility; build with
`c++ -O2 -std=c++17 lsm_sim.cpp -o lsm_sim`.)

---

## 6. Key Learnings

"Never update in place" really is the whole idea. Once writes only append to a log
and a MemTable, everything else follows as the machinery needed to make that
sustainable: immutable SSTables, levels, compaction, and tombstones for deletes. The
LSM-tree is a single idea taken to its logical conclusion.

The thing that clicked while watching the numbers is that the three amplifications
are one trade-off, not three separate problems. Compaction doesn't eliminate cost,
it relocates it among write-, read-, and space-amplification. Choosing a compaction
style is choosing which of the three you can afford to spend.

Bloom filters are what make LSM reads viable at all. A 74% reduction in file touches
is the difference between an LSM-tree being read-competitive or not, and it comes
from a tiny probabilistic structure bolted onto each SSTable.

Two broader observations:
- LSM and B-tree are mirror images. B-trees pay on random writes to keep reads
  cheap; LSM-trees pay on reads and space to keep writes cheap. Neither is "better,"
  and the existence of MyRocks alongside InnoDB under the same SQL layer is the
  industry admitting exactly that.
- Background work is a first-class design concern. PostgreSQL has VACUUM, InnoDB has
  undo purge, RocksDB has compaction. Every storage engine that defers cleanup has
  to schedule it carefully, because that background work competes with the
  foreground traffic it exists to support.

---

### References
- RocksDB Wiki — *RocksDB Overview* (primary reference):
  <https://github.com/facebook/rocksdb/wiki/RocksDB-Overview>
- RocksDB Wiki — *Leveled Compaction*, *Universal Compaction*, *Bloom Filter*,
  *Write Ahead Log*: <https://github.com/facebook/rocksdb/wiki>
- P. O'Neil et al., *"The Log-Structured Merge-Tree (LSM-Tree)"*, Acta Informatica
  1996 (the original paper).
- Facebook Engineering, *"Under the Hood: Building and open-sourcing RocksDB"*.

*The amplification numbers above come from `lsm_sim.cpp`, a simplified LSM-tree
model I wrote and ran. They illustrate RocksDB's documented behavior rather than
being output from RocksDB itself.*
