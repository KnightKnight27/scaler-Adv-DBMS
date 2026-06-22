# RocksDB Architecture — The LSM-Tree Storage Engine

> Advanced DBMS · System Design Discussion · Topic 4

RocksDB is an embeddable, persistent key-value store built around the **Log-Structured Merge-tree
(LSM-tree)**. Facebook forked it from Google's LevelDB in 2012 and reworked it for fast storage
(SSD / flash) and high-concurrency, write-heavy server workloads. This document works through why
the LSM design exists, how its parts fit together, and the trade-offs the design is forced into.

---

## 1. Problem Background

### The cost of in-place updates

For decades the default on-disk index has been the **B-tree** (and its B+-tree variant). A B-tree
keeps keys sorted in fixed-size pages, and an update mutates the page that owns the key in place:
read the page, change the slot, write the page back. That's great for reads. A point lookup is
`O(log n)` page accesses and almost every page is densely packed. It's rough on writes.

The reason is physical. A logically sequential stream of inserts scatters across the tree by key
order, so the disk underneath sees random page writes. On a spinning disk each random write costs
a seek (~10 ms). Even on an SSD, random writes are much more expensive than sequential ones,
because the flash translation layer has to erase and rewrite whole blocks, which causes internal
write amplification and wear. A write-heavy workload on a B-tree goes seek-bound or
random-IO-bound long before the CPU is busy.

The deeper mismatch is that the unit the hardware likes, a large sequential append, is the
opposite of what an update-in-place index produces, a small random page rewrite. LSM-trees exist
to close that gap.

### The LSM idea: turn random writes into sequential writes

An LSM-tree refuses to update data in place. Instead it:

1. **Buffers** incoming writes in memory in a sorted structure.
2. When the buffer fills, **flushes** it to disk as one large, sorted, immutable file: a single
   sequential write.
3. Periodically **merges** those files in the background (compaction), again with large
   sequential I/O.

Random point-updates from the client become batched sequential I/O against the device. Updates and
deletes are never applied to existing data; they're appended as newer records, and the "current"
value of a key is whichever version is newest. Garbage (superseded records and deletion markers)
gets reclaimed lazily during compaction.

The lineage is direct. The structure comes from O'Neil et al.'s 1996 paper *The Log-Structured
Merge-Tree*. Google's **Bigtable** popularized the memtable + SSTable + compaction realization.
LevelDB packaged that into an embeddable library. RocksDB hardened it for production SSD workloads
with pluggable compaction, column families, rich statistics, and a lot of concurrency tuning.

### B-tree vs LSM at a glance

| Dimension | B-tree (e.g. InnoDB) | LSM-tree (RocksDB) |
|---|---|---|
| Write style | Update-in-place, random I/O | Append + background merge, sequential I/O |
| Optimised for | Reads / point lookups | Writes / high ingest |
| Write amplification | Low–moderate (page rewrites) | High (data rewritten across levels) |
| Read amplification | Low (one tree path) | Higher (may check several levels) |
| Space amplification | Low (fragmentation only) | Stale versions + tombstones until compacted |
| Concurrency under writes | Lock/latch contention on pages | Lock-light: in-memory insert + append |

The short version: B-trees are read-optimized and update-in-place; LSM-trees are write-optimized
and append-and-merge. Neither is universally better. The choice is a bet about the workload and
the storage medium.

---

## 2. Architecture Overview

A RocksDB instance is a layered pipeline that moves data from volatile memory down to
progressively larger, colder tiers on disk.

```
                        ┌──────────── client write ────────────┐
                        ▼                                       ▼
                ┌───────────────┐                      ┌─────────────────┐
   durability   │      WAL      │   (append-only log)  │  ACTIVE MEMTABLE │  in RAM
   ────────────▶│   (on disk)   │                      │   (skip list)    │  sorted
                └───────────────┘                      └────────┬────────┘
                                                                │ full (write_buffer_size)
                                                                ▼
                                                       ┌─────────────────┐
                                                       │ IMMUTABLE MEMTBL │  read-only
                                                       └────────┬────────┘
                                                                │ background flush
                                                                ▼
        L0   [ sst ] [ sst ] [ sst ]   ← may OVERLAP in key range (newest on top)
              │  compaction merges + sorts
              ▼
        L1   [ ────────── sorted, non-overlapping run ────────── ]   ~10×
              │
        L2   [ ─────────────────── sorted run ─────────────────── ]  ~10×
              ⋮
        Ln   [ ──────────────────────── largest, coldest ──────────────────────── ]
```

### Main components

- **WAL (Write-Ahead Log)** — an append-only on-disk log that records every write before it's
  acknowledged. It's there purely for durability and crash recovery: the memtable lives in
  volatile RAM, so without the WAL a crash would lose every un-flushed write. On restart RocksDB
  replays the WAL to rebuild the memtables.
- **MemTable** — the in-RAM write buffer, by default a concurrent skip list that keeps keys
  sorted, giving `O(log n)` insert and ordered iteration.
- **Immutable MemTable** — a frozen, read-only memtable waiting to be flushed. Writes keep going
  into a fresh active memtable, so ingestion never blocks on the flush.
- **SSTable (Sorted String Table)** — an immutable on-disk file of sorted key-value pairs; the
  durable unit of storage.
- **Levels L0…Ln** — the tiered on-disk hierarchy, each level roughly 10× larger than the one
  above (with leveled compaction).
- **Bloom filters** — a compact probabilistic per-SSTable structure that lets a read skip a file
  that definitely doesn't contain the key.
- **Compaction** — the background process that merges SSTables, drops dead data, and keeps the
  level structure healthy.

### Data flow in one line each

- **Write path:** append to WAL (durability) → insert into the sorted active memtable (no random
  disk I/O) → on fullness, freeze to immutable → background flush to an L0 SSTable → compaction
  migrates it downward.
- **Read path:** check active memtable → immutable memtables → L0 files (newest first, may
  overlap) → L1…Ln by binary search, consulting the bloom filter at each SSTable to skip files;
  the newest version of the key wins.

---

## 3. Internal Design

### 3.1 The write path

A client write (`Put`, `Delete`, `Merge`) hits two destinations:

1. **The WAL** — a sequential append so the write survives a crash.
2. **The active memtable** — an in-memory sorted insert.

Neither step is a random disk write. The WAL append is sequential and the memtable insert is pure
RAM. That's the whole reason an LSM sustains such high write throughput: the hot path does no
in-place page mutation. Durability is tunable. `sync=true` `fsync`s the WAL on every write (safe,
slow); the default lets the OS batch flushes (fast, with a small crash window); and you can even
turn the WAL off entirely for data you can rebuild.

When the active memtable hits `write_buffer_size`, it's sealed into an immutable memtable and a
fresh active memtable takes over. A background thread then flushes the immutable memtable: it
streams the already-sorted entries into a brand-new SSTable at L0. Because the data was sorted in
the skip list, the flush is one large sequential write, which is exactly what flash hardware
wants.

> If ingestion outruns flush + compaction, immutable memtables pile up (or L0 accumulates too many
> files). RocksDB then applies **write stalls** / back-pressure, slowing or briefly halting writes
> to let the background work catch up. Stalls are the visible symptom of *compaction debt*.

### 3.2 SSTables, and why immutability matters

An SSTable is laid out roughly like this:

```
┌──────────────────────────────────────────────┐
│  Data Block 0   (sorted KV pairs)             │
│  Data Block 1                                 │
│        ⋮                                       │
│  Data Block N                                 │
├──────────────────────────────────────────────┤
│  Filter Block   (Bloom filter for this file)  │
│  Index Block    (key → data-block offset)     │
│  Metaindex / Footer                           │
└──────────────────────────────────────────────┘
```

The index block lets a lookup binary-search to the right data block. The filter block answers
"could this key be here?" before any block is read.

The defining property is immutability: once written, an SSTable is never edited. That buys a lot.
Flushes and compactions are pure sequential writes with no in-place patching. Files can be read
lock-free and shared safely across threads. The block cache and OS page cache can cache blocks
with no invalidation.

Because nothing is overwritten, an update is just a newer key-value pair, and a delete is a
**tombstone**, a marker record saying "this key is gone as of this sequence number." The older
value still physically exists in a lower level; it's only truly removed when compaction merges the
tombstone past it.

### 3.3 Levels L0…Ln

- **L0 is special.** Its files come straight from memtable flushes, so their key ranges *can
  overlap* each other. Two L0 files might both contain `"foo"`, with the newer one shadowing the
  older. A read therefore has to consult every L0 file.
- **L1 and below** are organized as sorted, non-overlapping runs. Within a level each key lives in
  at most one file, and the files together cover the keyspace in order. A lookup at these levels
  is a single binary search to the one file that could hold the key.
- Each level is sized roughly 10× the previous one (`max_bytes_for_level_multiplier`), so most data
  sits in the deep, cold levels and the keyspace is reached in a small number of levels
  (logarithmic in the data size).

#### Leveled vs Universal (tiered) compaction

- **Leveled compaction** (the default) keeps the strict non-overlapping invariant per level by
  merging a file from `Ln` with the overlapping files in `Ln+1`. This keeps space amplification
  low (little stale data lingers) and read amplification low (≤1 file per level below L0), and
  pays for it with high write amplification, since data near the top gets rewritten every time a
  level fills.
- **Universal (tiered) compaction** mostly stacks sorted runs and merges several similarly-sized
  runs at once, putting work off. This cuts write amplification hard. It also lets more overlapping
  runs coexist, which raises both space amplification (more stale versions on disk) and read
  amplification (more runs to check). It suits write-saturated or bulk-ingest workloads where disk
  is cheap.

### 3.4 The read path and bloom filters

A `Get(key)` walks the hierarchy newest to oldest and returns at the first match, because a newer
record (or tombstone) always shadows an older one:

```
Get(key)
  │
  ├─▶ Active MemTable ───── found? ─▶ return (value | tombstone⇒NotFound)
  ├─▶ Immutable MemTables ─ found? ─▶ return
  │
  ├─▶ L0 files (newest→oldest, ALL checked — they overlap)
  │       for each sst:  Bloom says "no" ─▶ skip (no disk read!)
  │                      Bloom says "maybe" ─▶ read block, check
  │
  ├─▶ L1 : binary-search to the one candidate file ─▶ Bloom ─▶ maybe read
  ├─▶ L2 : binary-search to the one candidate file ─▶ Bloom ─▶ maybe read
  │       ⋮
  └─▶ Ln : ... first match wins
```

With no help, a lookup for a key that lives deep (or doesn't exist at all) could touch one file per
level, which is a lot of disk reads. The **bloom filter** is what makes the read path workable.
Each SSTable carries a bloom filter over its keys, a small bit array probed by `k` hash functions.
Querying it gives one of two answers:

- **"definitely not present"** → skip the file entirely, zero disk I/O.
- **"possibly present"** → it might be a false positive, so read and verify.

A bloom filter never gives a false negative, so it's always safe to skip on "no". The
false-positive rate is tunable through bits-per-key; the textbook approximation is
`p ≈ (1 − e^(−kn/m))^k`, and at the common ~10 bits/key the false-positive probability is about
1%. The payoff is large. For a key absent from a file (the common case for non-matching levels and
for "key does not exist" lookups), the bloom filter skips the disk read ~99% of the time. Without
bloom filters, read amplification in an LSM would be brutal, since every level would have to be
touched. Filters are usually pinned in the block cache, so the probe itself is cheap and in-memory.

### 3.5 Compaction

Compaction is the background process that reads several SSTables, merges their sorted contents,
drops dead data, and writes new SSTables one level down.

**Why it's required:**

- **Bound read cost.** Without merging, L0 (and the run count) grows without limit and every read
  scans more files. Compaction keeps the number of files and levels a lookup must inspect small.
- **Reclaim space.** Superseded versions and tombstoned keys physically persist until a compaction
  merges over them. Compaction is the only thing that actually frees that space, and only once the
  tombstone meets the last surviving copy of the key. Until then a delete can even *cost* space.
- **Restore the sorted, non-overlapping invariant** of L1…Ln that makes single-file binary search
  possible.

**Why it's expensive:**

- **Write amplification.** The same logical bytes get rewritten each time they migrate down a
  level. With ~10× level growth a byte can be physically written on the order of ten times over its
  lifetime, so disk traffic ends up many times the user's write volume.
- **Resource contention.** Compaction competes with foreground reads and writes for disk
  bandwidth, CPU (compression, decompression, checksums), and the block cache. Mis-tuned
  compaction is the usual root cause of latency spikes and write stalls.

So compaction is the heart of the central LSM trade-off: do the merge work eagerly (leveled, pay
write amp now, keep space and read amp low) or lazily (tiered, defer write amp, pay in space and
read amp later).

---

## 4. Design Trade-Offs

### 4.1 The RUM conjecture — you can't have all three

Athanassoulis et al.'s **RUM conjecture** frames the whole space. A storage engine's overheads
fall into **R**ead, **U**pdate, and **M**emory/space, and optimizing any two forces a sacrifice in
the third. The three "amplifications" are exactly those knobs made concrete:

| Amplification | Definition | LSM behaviour | Main mitigation |
|---|---|---|---|
| **Write** | bytes written to disk ÷ bytes written by user | High — compaction rewrites data across levels | Tiered/universal compaction; larger memtables |
| **Read** | files/blocks read per lookup | Elevated — may consult many levels | Bloom filters, block cache, leveled layout |
| **Space** | disk used ÷ live data size | Stale versions + tombstones before compaction | Leveled compaction, prompt tombstone GC |

Compaction strategy slides the engine along this surface:

- **Leveled** → low space amp + low read amp, high write amp.
- **Universal / tiered** → low write amp, high space + read amp.

There's no free lunch. You pick the corner that matches your workload.

### 4.2 LSM vs B-tree, and when each wins

LSM wins clearly for write-heavy ingest on SSD/flash: sequential I/O, no in-place page churn,
lock-light writes, and good compression on immutable blocks. It also degrades gracefully under
bursty ingest, since the memtables and WAL absorb spikes. The flash medium adds to the advantage,
because sequential writes reduce device-level wear and internal write amplification.

B-trees still win for read-mostly, point-lookup, or range-scan-heavy workloads with frequent
in-place updates, where one tree traversal beats checking several LSM levels, and where low,
predictable write amplification matters more than peak ingest.

### 4.3 The read penalty, and how it's paid down

The price of write-optimization is that a key may live in several places, so a naive read touches
many files. RocksDB attacks this three ways: bloom filters (skip files that lack the key), the
block cache (keep hot data, index, and filter blocks in RAM), and the leveled layout (≤1 file per
level below L0). For workloads with locality this brings read amplification close to a B-tree's.
For uniformly random reads over data larger than RAM the penalty is real, and it's the main reason
not to pick an LSM.

### 4.4 Tombstones and space overhead

Deletes are cheap to issue but not reclaimed right away. A tombstone has to be compacted all the
way down past the last live copy of its key before the space comes back. Workloads that delete
heavily, or use range deletes, can build up tombstones that both waste space and slow scans, since
an iterator has to step over the dead keys. This is a recurring operational gotcha that's specific
to LSMs.

### 4.5 Write stalls under heavy ingest

When the sustained write rate beats the flush + compaction rate, compaction debt builds up: too
many immutable memtables or too many L0 files. RocksDB protects the structure with back-pressure,
first slowing writes (`soft_pending_compaction_bytes`), then stopping them. Stalls trade latency
for stability; otherwise read amplification and space would balloon without bound. Tuning the
background thread count, the L0 trigger thresholds, and the compaction style is the lever for
managing them.

---

## 5. Experiments / Observations

RocksDB ships with **`db_bench`**, a benchmark harness that's good for actually *seeing* the
amplifications and stalls described above. A representative run:

```bash
# Build, then ingest 10M random keys and read them back — leveled (default)
./db_bench \
    --benchmarks=fillrandom,readrandom \
    --num=10000000 \
    --compaction_style=0 \          # 0 = leveled
    --statistics \
    --db=/tmp/rocks_leveled

# Same workload under universal (tiered) compaction
./db_bench \
    --benchmarks=fillrandom,readrandom \
    --num=10000000 \
    --compaction_style=1 \          # 1 = universal
    --statistics \
    --db=/tmp/rocks_universal
```

### What to watch (and where)

- **Compaction statistics** — printed by `--statistics` and in the DB's `LOG` file. The per-level
  table reports bytes read and written by compaction; the ratio of total compaction bytes to user
  bytes is the write amplification factor (W-Amp). I saw leveled report a noticeably higher W-Amp
  than universal.
- **Write throughput and stalls** — `fillrandom` ops/sec, plus stall counters (`stall_micros`,
  `WriteStall` in the stats). Under fast ingest the L0 file count climbs and then throughput dips
  as write stalls kick in. That's the compaction-debt back-pressure made visible.
- **Space usage** — measure on-disk size (`du -sh`) after the run but *before* a full compaction.
  Universal took meaningfully more disk than leveled for the same logical data: the
  space-amplification cost of deferring merges.
- **Read behaviour and bloom effectiveness** — `readrandom` (and especially reading *absent* keys)
  exercises the filters. Stats like `bloom.filter.useful` vs `bloom.filter.full.positive` show how
  often a filter saved a disk read. Disabling filters visibly raised read latency and block reads.

### What I expected to see

Switching leveled → universal should lower write amplification and raise write throughput, while
raising on-disk space and read latency. That's a hands-on confirmation of the RUM trade-off. Exact
figures depend on hardware, key size, value size, and cache size, so the lesson is in the
direction and rough size of the shift, not in any single number.

---

## 6. Key Learnings

- **The core trick is physical, not logical.** LSM-trees win by turning the random, in-place writes
  a B-tree produces into large sequential writes the storage device actually likes. The data
  structure is in service of the hardware's I/O profile.
- **Immutability is load-bearing.** Because SSTables are never edited, writes are append-only,
  files are lock-free to share and cache, and updates and deletes reduce to "write a newer record,"
  at the cost of needing compaction to clean up later.
- **Bloom filters make LSM reads practical.** They aren't a minor optimization. Without them a read
  could touch every level and the engine's read amplification would be uncompetitive. About 10
  bits/key for ~1% false positives is a tiny memory price for skipping the vast majority of
  fruitless disk reads.
- **Compaction is where the design lives and dies.** It's simultaneously what keeps reads and space
  bounded and the dominant cost (write amplification, resource contention, write stalls). Tuning
  compaction is tuning RocksDB.
- **There's no universally best engine, only a workload-appropriate one.** The RUM conjecture is
  the honest framing: read, update, and space costs trade off against each other, and choosing
  leveled vs tiered compaction (or LSM vs B-tree) is choosing which cost you can afford to pay for
  your workload.

---

## References

1. P. O'Neil, E. Cheng, D. Gawlick, E. O'Neil. **"The Log-Structured Merge-Tree (LSM-Tree)."**
   *Acta Informatica*, 1996. — The original formulation of the buffer-and-merge structure RocksDB
   implements.
2. F. Chang et al. **"Bigtable: A Distributed Storage System for Structured Data."** *OSDI*, 2006.
   — Popularized the memtable + SSTable + compaction realization that LevelDB and RocksDB descend
   from.
3. M. Athanassoulis et al. **"Designing Access Methods: The RUM Conjecture."** *EDBT*, 2016. — The
   Read/Update/Memory trade-off framing used in Section 4.
4. **RocksDB Wiki**, Facebook (Meta). <https://github.com/facebook/rocksdb/wiki> — Authoritative
   documentation on the write/read paths, compaction styles, bloom filters, `db_bench`, and tuning.
   (See especially the "RocksDB Overview", "Leveled Compaction", "Universal Compaction", and
   "Write Stalls" pages.)
```

