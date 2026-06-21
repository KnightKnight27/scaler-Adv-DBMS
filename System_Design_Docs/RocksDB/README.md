# RocksDB Architecture (LSM-Tree Storage)

I studied RocksDB, an embedded key-value store built on a Log-Structured Merge
tree (LSM-tree). The big idea is the opposite of a B-tree database: instead of
updating in place, it only appends, buffers writes in memory, and cleans up
later through compaction. That makes writes fast; the cost is read and space
amplification.

## 1. Problem Background

- B-tree databases update pages in place, which often means slow random writes
  and more flash wear.
- Many modern workloads are write-heavy (logs, metrics, time series, queues).
- RocksDB (by Facebook, from Google's LevelDB) makes writes cheap by turning
  random writes into sequential ones, while staying an embedded library (no
  server). It backs MyRocks, CockroachDB, TiKV, and others.

## 2. Architecture Overview

```
write -> WAL on disk (durability)
      -> Active MemTable (sorted, in memory)
            | fills up -> becomes Immutable MemTable
            v flush (sequential write)
   L0: [sst][sst][sst]     (files may overlap)
   L1: [ sst ][ sst ]      (sorted, non-overlapping)
   L2: [   sst    ]        (~10x bigger per level)
   Ln: biggest, oldest
   compaction merges files downward in the background
```

Flow in one line: WAL -> MemTable -> flush -> L0 SSTables -> compaction -> L1..Ln.

## 3. Internal Design

- **Write path.** A write is appended to the WAL (durability), then inserted
  into the active MemTable (a sorted skiplist). When the MemTable is full it
  becomes immutable and a new one takes over, so writes never stop. A background
  thread flushes the immutable MemTable to disk as an L0 SSTable - one
  sequential write.
- **SSTables.** Immutable sorted files of key->value entries, written once. Each
  has data blocks, an index block, and a Bloom filter. An update or delete is
  just a newer entry with the same key (a delete writes a *tombstone*); the old
  value stays until compaction removes it.
- **Levels L0..Ln.** L0 files come from flushes so their key ranges can overlap.
  L1..Ln have non-overlapping sorted ranges, each level ~10x bigger. A key can
  exist in several places; the newest version wins.
- **Bloom filters.** A small per-SSTable structure answering "is this key
  possibly here?". It can say "definitely not" (skip the file) or "maybe"
  (check it). This lets a read skip almost all files that can't hold the key.
- **Read path.** Check MemTable -> immutable MemTable -> L0 files (all, gated by
  Bloom filters) -> one candidate file per level in L1..Ln. Return the newest
  version; a tombstone means deleted. A read may touch several files (read
  amplification), but Bloom filters keep it bounded.
- **Compaction.** Background work that merges SSTables, drops overwritten values
  and tombstones, and pushes data to larger levels. Needed to reclaim space and
  keep reads fast. Leveled compaction (default) keeps levels non-overlapping
  (good read/space, higher write amplification); universal/tiered merges less
  often (lower write, higher read/space).

## 4. Design Trade-Offs

The three amplifications are how I reason about LSM trade-offs:

- Write amplification = bytes written to disk / bytes the user wrote (compaction
  rewrites data many times).
- Read amplification = how many places a read may check for one key.
- Space amplification = disk used / live data, because stale versions linger.

You can't minimize all three (the RUM trade-off), so you pick a compaction
strategy:

| | Leveled (default) | Universal / Tiered |
|---|---|---|
| Write amp | Higher | Lower |
| Read amp | Lower | Higher |
| Space amp | Lower | Higher |
| Best for | read-heavy, space-tight | very write-heavy |

LSM vs B-tree: LSM wins on writes (sequential, append-only) and needs Bloom
filters + compaction to keep reads good; B-trees win on reads but do random
writes. LSM can also suffer write stalls if compaction falls behind.

## 5. Experiments / Observations

> RocksDB and `db_bench` were not installed here, so these are representative
> numbers (illustrative, not run live).

```bash
./db_bench --benchmarks=fillrandom,readrandom --num=10000000 \
           --compaction_style=0   # 0 = leveled, 1 = universal
```

| Style | Write amp | Read amp | Space amp |
|---|---|---|---|
| Leveled (=0) | higher (~10-30x) | lower | lower (~1.1x) |
| Universal (=1) | lower | higher | higher (~2x) |

RocksDB's LOG and `rocksdb.stats` print a per-level table (files, size, W-Amp),
so I can watch write amplification grow as data sinks to deeper levels. Turning
the Bloom filter off makes point reads clearly slower - a direct demo of its
value. This is the contrast to the in-place single-file B-tree (SQLite) I
actually measured in the PostgreSQL_vs_SQLite topic.

## 6. Key Learnings

- LSM trees are write-optimized: every write is a memory insert plus a
  sequential append, no random in-place updates.
- The cost is paid later by compaction, which is expensive because it rewrites
  data many times and competes for I/O.
- Bloom filters make reads practical by letting a lookup skip almost all files.
- You tune by choosing what to sacrifice (leveled vs universal); there is no
  free lunch (RUM).
- Append-only + immutable files makes writes cheap and recovery simple, pushing
  all complexity into background cleanup - the deepest read-vs-write trade-off
  in storage design.

### References
RocksDB Wiki (Overview, Leveled/Universal Compaction, MemTable, Bloom Filters,
db_bench); O'Neil et al., *The Log-Structured Merge-Tree* (1996); Athanassoulis
et al., *The RUM Conjecture*.
