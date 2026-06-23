# RocksDB Architecture (LSM-Tree Storage)

> Advanced DBMS — System Design Discussion
> Topic 4: RocksDB Architecture

---

## Table of Contents

1. [Problem Background](#1-problem-background)
2. [Architecture Overview](#2-architecture-overview)
3. [Internal Design](#3-internal-design)
4. [Design Trade-Offs](#4-design-trade-offs)
5. [Experiments / Observations](#5-experiments--observations)
6. [Key Learnings](#6-key-learnings)
7. [References](#references)

---

## 1. Problem Background

RocksDB is an **embeddable, persistent key-value store** developed at **Facebook
(2012)**, forked from Google's **LevelDB** and heavily optimized for fast storage
(SSD/flash) and multi-core servers. It is a *library* (like SQLite, not a server) that
applications link in; it is the storage engine inside MySQL's MyRocks, CockroachDB,
TiKV, Kafka Streams, Ceph, and many others.

The problem it targets: **write-heavy workloads** where a traditional B-tree struggles.
A B-tree updates data **in place**, so every write may trigger a **random** read of a
page and a **random** write back. On spinning disks random I/O is catastrophic, and
even on SSDs random in-place writes cause write amplification and wear. RocksDB instead
uses an **LSM-tree (Log-Structured Merge-tree)**, which turns random writes into
**sequential** writes by *only ever appending* and reorganizing data in the background.

> Core idea: **never update in place.** Buffer writes in memory, flush them as sorted
> immutable files, and merge those files later. Writes become cheap and sequential;
> the cost is deferred to background **compaction** and paid on the read path.

---

## 2. Architecture Overview

```
   WRITE PATH                                          READ PATH
   ─────────                                           ─────────
   put(k,v)                                            get(k)
      │                                                   │ newest → oldest
      ├─►(1) append to WAL  ──► durability                ▼
      │                                          ┌─► MemTable        (in RAM)
      └─►(2) insert into  ┌───────────────┐      ├─► Immutable MemTables
            MemTable ────►│   MemTable     │(RAM) ├─► L0 SSTables  (may overlap)
                          └───────┬────────┘      ├─► L1 SSTables  ─┐
                       full → switch to           ├─► L2 SSTables   │ sorted,
                          ┌───────▼────────┐      ├─► ...           │ non-overlap
                          │ Immutable      │(RAM) └─► Ln SSTables  ─┘ per level
                          │ MemTable       │
                          └───────┬────────┘   each level checks: Bloom filter →
                          flush (sequential)    index block → data block
       ┌──────────────────────────▼───────────────────────────────────┐
       │  ON DISK (sorted string tables, immutable)                     │
       │   L0:  [sst][sst][sst]      (overlapping key ranges)           │
       │   L1:  [────sst────][────sst────]   (sorted, non-overlapping)  │
       │   L2:  [──sst──][──sst──][──sst──]  ~10x larger than L1        │
       │   ...                                                          │
       │   Ln:  largest, oldest data                                    │
       │            ▲                                                   │
       │   compaction merges level i into level i+1 (background)        │
       └────────────────────────────────────────────────────────────────┘
```

**Components at a glance:**
- **MemTable** — in-memory, sorted, writable buffer (default a skip list).
- **Immutable MemTable** — a full MemTable, now read-only, awaiting flush.
- **WAL** — write-ahead log on disk for durability before data is flushed.
- **SSTable (Sorted String Table)** — immutable, sorted on-disk file of key-value
  pairs, organized into **levels L0…Ln**.
- **Bloom filters** — per-SSTable probabilistic filters that let reads skip files that
  certainly don't contain a key.
- **Compaction** — background process that merges SSTables, drops obsolete versions,
  and pushes data down the levels.

---

## 3. Internal Design

### 3.1 Write Path

1. A `Put(key, value)` (or `Delete`, which writes a **tombstone** marker) is first
   **appended to the WAL** — a sequential disk write that guarantees durability.
2. The key-value is inserted into the active **MemTable** (a sorted in-memory structure,
   default skip list). The write is now "done" from the client's perspective — no disk
   seek, no in-place update. **This is why LSM writes are fast.**
3. When the MemTable reaches `write_buffer_size`, it becomes an **immutable MemTable**
   and a new active MemTable is started (writes never block on flush).
4. A background thread **flushes** the immutable MemTable to a new **L0 SSTable** — one
   large **sequential** write. The corresponding WAL can then be discarded.

Every record carries an internal **sequence number**, so multiple versions of a key
coexist with a clear ordering (newest wins). RocksDB is thus inherently multi-version.

### 3.2 SSTables and the Level Structure

An **SSTable** is immutable and **sorted by key**. Internally it holds:
- **data blocks** (the sorted key-values),
- an **index block** (maps key ranges → data block offsets for binary search),
- a **Bloom filter block**,
- metadata/footer.

Levels:
- **L0** is special: it receives flushed MemTables directly, so its SSTables can have
  **overlapping key ranges** (a key may be in several L0 files). Reads must check *all*
  L0 files.
- **L1 … Ln**: within each level, SSTables are **sorted and have non-overlapping key
  ranges**, so at most **one** file per level can contain a given key (binary search
  picks it). Each level is roughly **10× larger** than the one above
  (`max_bytes_for_level_multiplier`). Data gets older/colder as it sinks toward Ln.

### 3.3 Read Path

A `Get(key)` searches from **newest to oldest** and stops at the first hit:
1. Active **MemTable** → 2. **Immutable MemTables** → 3. **L0** SSTables (all of them,
   newest first) → 4. **L1, L2, … Ln** (one candidate file per level via binary search).

To avoid touching files that don't have the key, each SSTable read does:
**Bloom filter check → index block → data block.** If the Bloom filter says "not
present," the file is skipped without any data-block I/O. The result is the newest
version found, or — if a **tombstone** is found first — "not found."

> Because a key may live in many places (MemTable + several SSTables across levels),
> a read can touch multiple files. This is the LSM **read-amplification** cost that
> Bloom filters and compaction exist to contain.

### 3.4 Bloom Filters

A **Bloom filter** is a compact, probabilistic bitmap that answers "is this key
possibly in this SSTable?" with **no false negatives** (if it says "no," the key is
definitely absent) and a tunable **false-positive** rate (a "yes" might be wrong).

- Configured in bits per key (e.g. 10 bits/key → ~1% false positives).
- **Why it matters:** without filters, a point lookup for a missing key would have to
  binary-search a candidate file at *every* level. The Bloom filter lets reads skip
  almost all of those files, turning many potential disk reads into a few bitmap
  checks — the single biggest read optimization in an LSM.

### 3.5 Compaction

Because SSTables are immutable and writes only append, **obsolete versions and
tombstones accumulate** and key ranges in L0 overlap. **Compaction** is the background
job that fixes this: it reads several SSTables, **merge-sorts** them, **discards
superseded versions and tombstones**, and writes new SSTables one level down.

Two main strategies:

- **Leveled compaction (default).** Maintains the size-tiered L1…Ln invariant
  (non-overlapping, ~10× growth). Merges a file from level *i* with the overlapping
  files in level *i+1*. Keeps **read and space amplification low** (≤1 file per level
  to check, little dead data) at the cost of **higher write amplification** (data is
  rewritten as it descends each level).

- **Universal (tiered) compaction.** Merges similarly-sized SSTables together, allowing
  more overlap and more files per level. **Lower write amplification** (data rewritten
  fewer times) but **higher read and space amplification** (more files to check, more
  stale data retained). Good for write-saturated workloads that can spare space.

This is the central LSM tuning dial; see the trade-offs and experiments below.

### 3.6 Durability & Recovery

- The **WAL** is the durability mechanism: a write is durable once its WAL append is
  persisted (flush/`fsync` behavior is configurable, trading durability for throughput,
  much like InnoDB's commit-flush setting).
- On **crash recovery**, RocksDB **replays the WAL** to rebuild the MemTable(s) that
  had not yet been flushed to SSTables. SSTables are immutable, so already-flushed data
  needs no recovery.
- A **MANIFEST** file records the current set of SSTables and level layout (the "version"
  of the LSM), so RocksDB knows exactly which files are live after restart.
- **Column families** allow multiple independent key spaces in one database sharing a
  single WAL.

---

## 4. Design Trade-Offs

The LSM design is best understood through the **three amplification factors** — every
tuning choice trades one against the others (the "RUM conjecture": you can't minimize
Read, Update, and Memory/space cost all at once).

| Factor | Meaning | LSM behavior |
|---|---|---|
| **Write amplification** | bytes written to disk ÷ bytes of user data | High — data is rewritten repeatedly by compaction as it descends levels |
| **Read amplification** | files/blocks read per logical read | Moderate–high — a key may sit in MemTable + multiple SSTables; mitigated by Bloom filters |
| **Space amplification** | disk used ÷ live data size | From obsolete versions & tombstones awaiting compaction |

**Advantages**
- **Excellent write throughput:** writes are sequential appends to WAL + in-memory
  insert; no random in-place page writes. Ideal for write-heavy and ingest workloads.
- **Sequential I/O everywhere:** flushes and compactions write large sequential files —
  friendly to SSDs (less wear) and even HDDs.
- **Good compression:** immutable, sorted SSTables compress well block-by-block.
- **Embeddable & highly tunable:** runs in-process; dozens of knobs (compaction style,
  Bloom bits, level sizes, block cache).

**Limitations / costs**
- **Read amplification:** point and especially range reads may consult many files;
  Bloom filters help point lookups but not range scans.
- **Compaction is expensive and bursty:** it consumes CPU, disk bandwidth, and can cause
  **write stalls** if ingestion outruns compaction (L0 files pile up).
- **Space overhead** from stale versions/tombstones until compaction reclaims it.
- **Deletes are deferred:** a `Delete` only writes a tombstone; space isn't freed until
  compaction, and many tombstones can slow range scans.

**Strategy trade-off (the key engineering decision):**
- *Leveled* → low read & space amp, **high write amp** (good for read-mostly / balanced).
- *Universal/tiered* → **low write amp**, high read & space amp (good for write-saturated).

**vs B-tree (e.g. InnoDB/PostgreSQL):** B-trees optimize **reads** (a key lives in one
place; bounded, predictable lookup) and update in place (low space amp) but suffer on
random writes. LSM-trees optimize **writes** (sequential appends) and pay on reads and
compaction. Choose by workload: write-heavy → LSM; read-heavy / point-lookup-latency
sensitive → B-tree.

---

## 5. Experiments / Observations

RocksDB ships with **`db_bench`**, which makes the amplification trade-offs measurable.

### 5.1 Measuring write throughput and amplification

```bash
# Fill the DB with random writes, then read randomly.
./db_bench --benchmarks=fillrandom,readrandom \
           --num=10000000 --value_size=100 \
           --statistics --histogram

# Compare compaction styles on the SAME workload:
./db_bench --benchmarks=fillrandom --num=10000000 \
           --compaction_style=0   # 0 = Leveled
./db_bench --benchmarks=fillrandom --num=10000000 \
           --compaction_style=1   # 1 = Universal
```
**What to observe:** with `--statistics`, RocksDB reports bytes written by compaction
vs bytes ingested — the **write amplification** ratio. *Leveled* shows **higher** write
amplification; *Universal* shows **lower** write amp but uses **more disk space**
(higher space amplification). This is §3.5 made quantitative.

### 5.2 Read amplification and the effect of Bloom filters

```bash
# Point lookups for keys that don't exist stress the read path the most.
./db_bench --benchmarks=readmissing --num=10000000 \
           --bloom_bits=10            # ~1% false positive
./db_bench --benchmarks=readmissing --num=10000000 \
           --bloom_bits=0             # Bloom filters DISABLED
```
**What to observe:** with Bloom filters on, missing-key lookups are fast (most SSTables
skipped via the filter). With `--bloom_bits=0`, the same lookups get dramatically
slower because every candidate file's index/data blocks must be searched. This directly
demonstrates why Bloom filters are the LSM read path's most important optimization
(§3.4).

### 5.3 Observing the LSM state

```bash
# Inspect level sizes, file counts, and compaction stats at runtime:
#   (via the RocksDB property API in code)
db->GetProperty("rocksdb.stats", &out);
db->GetProperty("rocksdb.levelstats", &out);
db->GetProperty("rocksdb.num-files-at-level0", &out);
```
**What to observe:** `levelstats` shows the ~10× size growth per level and how many
files sit at each level. A large/growing **L0 file count** signals flushes outrunning
compaction — the precursor to a **write stall**. Watching L0 drain after a write burst
shows compaction doing its job.

### 5.4 Space amplification from tombstones

```
1. Put 1,000,000 keys.        → SSTables created across levels
2. Delete all 1,000,000 keys. → 1,000,000 TOMBSTONES written (DB size barely drops!)
3. Trigger compaction.        → tombstones + dead versions purged, space reclaimed
```
**Observation:** disk usage does **not** drop immediately after the deletes — deletes
are just tombstone writes. Only **compaction** reclaims the space, demonstrating
deferred deletes and space amplification (§3.5, §4).

---

## 6. Key Learnings

1. **LSM trades the write problem for a read+compaction problem.** By never updating in
   place — buffer in MemTable, append WAL, flush immutable SSTables — random writes
   become sequential and writes get cheap. That deferred work reappears as read
   amplification and background compaction. Nothing is free; the cost is *moved*, not
   removed.

2. **The three amplifications are the real design space.** Write, read, and space
   amplification trade off against each other (RUM conjecture). *Leveled* compaction
   buys low read/space amp with high write amp; *Universal* does the reverse. Choosing a
   compaction strategy **is** choosing which amplification you can afford.

3. **Compaction is the heart of the system.** It's not housekeeping — it's what keeps
   reads bounded (non-overlapping levels), reclaims space (drops stale versions and
   tombstones), and can also be the system's biggest source of CPU/I/O load and write
   stalls. Most RocksDB tuning is really compaction tuning.

4. **Bloom filters make LSM reads viable.** Since a key can live in many files across
   levels, point reads would be painfully slow without a way to skip files. Bloom
   filters (no false negatives) eliminate almost all unnecessary file probes — but they
   don't help range scans, which is why those remain an LSM weak spot.

5. **Deletes are writes, and durability is the WAL.** A `Delete` is a tombstone, so
   space is reclaimed only at compaction; durability comes from the WAL, replayed on
   crash to rebuild unflushed MemTables. Both are direct consequences of "append-only,
   immutable files."

6. **Workload dictates the engine.** RocksDB's LSM is the right tool for write-heavy,
   ingest-heavy, SSD-backed workloads (which is why it underpins MyRocks, CockroachDB,
   TiKV). For read-latency-critical, point-lookup-heavy workloads, an in-place B-tree
   is often still the better fit.

---

## References

- RocksDB Wiki — Overview & Architecture: https://github.com/facebook/rocksdb/wiki/RocksDB-Overview
- RocksDB Wiki — Leveled Compaction: https://github.com/facebook/rocksdb/wiki/Leveled-Compaction
- RocksDB Wiki — Universal Compaction: https://github.com/facebook/rocksdb/wiki/Universal-Compaction
- RocksDB Wiki — MemTable, WAL, SST File Formats, Bloom Filters
- RocksDB Wiki — Benchmarking with `db_bench`: https://github.com/facebook/rocksdb/wiki/Benchmarking-tools
- O'Neil et al. — "The Log-Structured Merge-Tree (LSM-Tree)" (1996)
- Athanassoulis et al. — "Designing Access Methods: The RUM Conjecture" (2016)

---

*Submitted for the Advanced DBMS System Design Discussion. All analysis and prose are
original; cited sources were used for fact-checking architectural details.*
