# RocksDB — An LSM-Tree Storage Engine

RocksDB is an embeddable key-value store built around a **Log-Structured Merge tree (LSM)**.
The whole design is organized around one bet: that turning slow random writes into fast
sequential writes is worth paying for later, in the form of background work called
**compaction**. If you understand *that* bargain, everything else — memtables, SSTables, levels,
bloom filters — falls into place.

---

## 1. Problem Background

RocksDB was forked from Google's LevelDB by Facebook (~2012) and tuned for fast storage (SSDs,
flash) and server workloads. It's a **library**, not a server — you link it into your process,
and it backs things like MySQL's MyRocks engine, Kafka Streams state, CockroachDB (historically),
and countless metadata stores.

The motivating problem: B-tree storage engines update pages **in place**, which on disk means
**random writes**. Random writes are expensive — especially the read-modify-write of a page, and
especially under write-heavy load. LSM trees attack this by *never updating in place*. Every
write is an append; reorganization happens later, in bulk, sequentially.

---

## 2. Architecture Overview

```
  WRITE path                                READ path
  ---------                                 ---------
  put(k,v)                                  get(k)
     |                                         |
     +--> WAL (append, for durability)         1. active memtable
     |                                         2. immutable memtable(s)
     +--> active MemTable (in-RAM skiplist)    3. L0 SSTables (newest first)
              |  (when full)                   4. L1 ... Ln SSTables
              v                                   |
        immutable MemTable                     bloom filter per SSTable
              |  (flush)                        skips files that can't
              v                                 contain the key
        L0 SSTable on disk
              |
              |  compaction merges down
              v
        L1 -> L2 -> ... -> Ln   (each level ~10x larger)
```

**Writes** go to two places: the WAL (so a crash can't lose them) and an in-memory **memtable**.
**Reads** check memory first, then progressively older/larger on-disk levels, using bloom filters
to avoid touching files that can't help.

---

## 3. Internal Design

### MemTable and the write path

A write appends to the **WAL** and inserts into the **active memtable**, an in-memory sorted
structure (default: a skiplist). Because it's in RAM and append-only, writes are *fast* — no disk
seek, no page rewrite. When the memtable hits its size limit it's frozen into an **immutable
memtable** (still readable, no longer written), a fresh active memtable takes over, and a
background thread **flushes** the immutable one to disk as an SSTable. The matching WAL segment
can then be discarded.

### SSTables and levels

An **SSTable** (Sorted String Table) is an immutable, sorted file of key-value pairs with an
index block and bloom filter at the end. Once written, it is never modified — only eventually
replaced by compaction.

Files are organized into **levels**:

- **L0** holds files flushed straight from memtables, so its files can have **overlapping** key
  ranges (and overlap each other in time). A read may have to check *every* L0 file.
- **L1 and below** are kept **non-overlapping** within a level, and each level is roughly **10×
  larger** than the one above. Because a level is internally sorted and non-overlapping, a read
  needs at most **one** file per level — found via binary search on the level's file boundaries.

A key insight: newer data is always nearer the top (memtable → L0 → L1...). The first version of
a key you encounter walking top-down is the live one; older versions and tombstones (delete
markers) lurk below until compaction removes them.

### Bloom filters

Each SSTable carries a **bloom filter** — a compact probabilistic structure that answers "is key
K *possibly* in this file?" with **no false negatives**. If the filter says no, the key is
*definitely* not there and RocksDB skips the file entirely without reading it from disk. This is
what keeps point lookups cheap despite data being spread across many files: instead of probing
O(number of levels) files, a `get` usually touches only the one file that actually holds the key
(plus near-instant in-memory filter checks for the rest).

### Compaction

Compaction is the background process that merges SSTables, drops overwritten values and
tombstones, and pushes data down the level hierarchy keeping each level sorted and
non-overlapping. It's the price you pay for cheap writes — and the main tuning dial:

- **Leveled compaction** (default): maintains the strict ~10×-per-level, non-overlapping
  structure. Gives **low read amplification** and **low space amplification**, but **higher write
  amplification** (data gets rewritten as it migrates down levels).
- **Universal / tiered compaction**: merges similarly-sized files more lazily. **Lower write
  amplification**, but **higher space and read amplification** (more overlapping data lingers).

### Crash recovery

Recovery is simple precisely because of the WAL: on restart, replay the WAL into a fresh memtable
to recover any writes that hadn't been flushed to an SSTable yet. Everything already in SSTables
is immutable and safe by construction.

---

## 4. Design Trade-Offs — the three amplifications

LSM engines are best understood through three competing costs (the **RUM conjecture** — you can
optimize any two, but not all three at once):

- **Write amplification** — bytes written to disk ÷ bytes the user wrote. Compaction rewrites the
  same data several times as it sinks through the levels. Leveled compaction has the most.
- **Read amplification** — work per lookup. A key might live in the memtable, or any level, so a
  read may consult several places (bloom filters cut this down dramatically).
- **Space amplification** — disk used ÷ live data size. Obsolete versions and tombstones occupy
  space until compaction reclaims them. Tiered compaction has the most.

The fundamental tension: **compact aggressively** → less space and faster reads, but more write
amplification. **Compact lazily** → cheap writes, but you read through more files and waste more
disk. There's no free lunch; you pick the corner of the triangle that matches your workload.

Versus a B-tree (like InnoDB): the B-tree optimizes for reads and in-place updates (low read amp,
low space amp) at the cost of random-write performance; the LSM tree optimizes for writes
(sequential, append-only) at the cost of background compaction and read amplification. **That's
the whole story in one sentence.**

---

## 5. Experiments / Observations

Using the bundled `db_bench`:

```
./db_bench --benchmarks=fillrandom,readrandom \
           --num=10000000 --compaction_style=0   # 0 = level, 1 = universal
```

Things you can observe, and what they teach:

- **`fillrandom` is fast, far faster than a B-tree would manage for random keys** — because every
  write is just a memtable insert + WAL append. This *is* the LSM advantage, made visible.
- **Watch compaction in `LOG` / `rocksdb.stats`.** You'll see total bytes written to disk exceed
  bytes you inserted — that ratio **is** write amplification. Switch `--compaction_style` from
  leveled to universal and watch write amp drop while disk usage (space amp) climbs.
- **Bloom filters on reads.** Compare `readrandom` for keys that exist vs keys that don't. Missing
  keys stay cheap because bloom filters short-circuit the disk reads — turn filters off and that
  case gets dramatically slower as every level is probed.
- **L0 file count and read latency.** Push writes faster than compaction can keep up and L0
  accumulates overlapping files; point-read latency rises because each L0 file must be checked.
  Compaction catching up brings it back down — the write/compaction balance in real time.

---

## 6. Key Learnings

- **LSM trees are write-optimized because they refuse to update in place.** Every write is a
  sequential append (memtable + WAL); the expensive reorganization is deferred and batched into
  compaction. That's exactly why they shine in write-heavy workloads where a B-tree's random
  writes would dominate.
- **Compaction is the bill for cheap writes.** It's necessary to reclaim space, drop tombstones,
  and keep reads fast — and it can get expensive precisely because it rewrites the same data
  multiple times (write amplification) and competes with foreground traffic for I/O.
- **Bloom filters are what make LSM reads tolerable.** Without them a lookup would scan many
  files; with them, "key not here" is answered from memory and most disk reads vanish.
- The recurring lesson is the **amplification triangle**: write, read, and space amplification
  trade off against each other, and choosing a compaction strategy is really just choosing which
  one you're willing to pay. Design is, once again, a choice about *where* the cost goes — not
  whether there is one.
