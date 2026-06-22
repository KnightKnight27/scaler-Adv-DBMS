# RocksDB — LSM-Tree Architecture

This is my write-up for Topic 4. RocksDB is the one system in the
list that *doesn't* use B-trees as its primary structure — it uses an
LSM tree (log-structured merge tree). That makes it the most useful
contrast with everything else in this course, because almost
everything in the labs (B-trees in Lab 4, slotted pages in Lab 4's
xxd, even the buffer manager in Lab 3) assumed a B-tree world.

I haven't used RocksDB in any lab, so this is research-and-thinking,
not measurements.

## 1. Problem Background

RocksDB was open-sourced by Facebook in 2012 as a fork of Google's
LevelDB. Both were designed for the same class of problem: very
**write-heavy** workloads on SSDs, where a B-tree's update-in-place
behaviour causes too many random writes.

The key insight behind LSM trees (Patrick O'Neil et al., 1996) is:
**disk is fast at sequential writes and slow at random writes**, even
on SSDs (which still have write amplification at the flash layer). A
B-tree update touches a random leaf page and writes 8 or 16 KB back
out — that's a random write. If you can turn those random writes into
sequential ones, you can get huge write throughput improvements.

LSM trees do this by:

1. Buffering recent writes in memory.
2. Periodically flushing those buffers to disk as **immutable, sorted
   files**.
3. Periodically **compacting** older files together to keep the read
   side from getting too slow.

That's the whole idea. RocksDB is just the practical, embeddable
implementation of it, used as the storage engine inside many other
systems — MyRocks (MySQL with RocksDB underneath), CockroachDB, TiDB,
Kafka Streams, etc.

## 2. Architecture Overview

```
   writes
     |
     v
   WAL (log file)             <-- durability
     |
     v
   MemTable    (in-memory skip list, mutable)
     |
     | when full
     v
   immutable MemTable   ---+
                           | flush (sequential write)
                           v
   L0: SSTable, SSTable, SSTable, ...   (overlapping key ranges)
                           |
                           | compaction (merge sort + dedupe)
                           v
   L1: SSTable | SSTable | SSTable      (non-overlapping; bigger)
   L2: SSTable | SSTable | ...          (~10x L1)
   ...
   Ln
```

The four moving pieces:

- **WAL** — every write hits a log file first, so a crash doesn't lose
  in-memory data.
- **MemTable** — a sorted in-memory structure (a skip list by default
  in RocksDB) holding recent writes. Fast inserts, fast point lookups.
- **Immutable MemTable** — once the active MemTable is full, it
  becomes immutable and a new MemTable starts taking writes. The
  immutable one gets flushed in the background to disk as an SSTable.
- **SSTables** — Sorted String Tables. Immutable, sorted files on disk.
  Once written, they're never modified. New data goes into new
  SSTables; deletions are recorded as "tombstones."

The big question is reads, because the same key can appear in many
SSTables across levels. That's where the level structure + bloom
filters come in (below).

## 3. Internal Design

### 3.1 Write path

```
write(key=K, value=V):
    append (K, V) to WAL  (sequential write, fsync on commit)
    insert (K, V) into MemTable  (in-memory skip list)
    return success
```

That's it. The on-disk part of the write is a single sequential
append to the WAL. No B-tree descent, no random write, no read of
the existing value.

When the MemTable fills up (default 64 MB):

```
mark MemTable as immutable
start a new empty MemTable
schedule a flush job:
    iterate the immutable MemTable in key order
    write it out as a new SSTable in L0
    delete the corresponding WAL entries
```

L0 is special — it's the only level where SSTables can have
**overlapping key ranges** (because multiple MemTable flushes hit L0
in arrival order).

### 3.2 Read path

A point lookup has to ask "where could this key live?" The answer:
MemTable first, then immutable MemTable, then every L0 SSTable, then
the relevant L1 SSTable, L2 SSTable, ..., until found or the search
hits the bottom.

The naive cost would be terrible. Two things make it fast:

- **Each SSTable has a bloom filter** in its index. The bloom filter
  answers "is K definitely not here?" with very high accuracy. If
  yes, we skip that whole SSTable.
- **L1+ levels are sorted with non-overlapping key ranges**, so we
  only need to check at most one SSTable per level. A binary search
  on the level's metadata finds which SSTable could contain `K`.

So a typical point lookup checks: MemTable (in-memory), then 4–10
SSTables across levels, each one filtered by a bloom check.

Range scans are different — they merge iterators from MemTable + all
relevant SSTables in key order. That's where being *sorted* matters:
merging sorted streams is cheap.

### 3.3 Compaction

The L0 SSTables (overlapping) and the L1+ SSTables grow without
compaction. Reads slow down because the number of files to check
keeps growing. Compaction merges files together — read N input
SSTables, merge-sort them, write out new SSTables to the next level,
delete the inputs.

```
L0:  [A..K]  [D..M]  [G..P]   <-- overlapping
                  |
                  | compaction picks files from L0 + the L1 files
                  | whose ranges overlap, merges them
                  v
L1:  [A..C][D..F][G..J][K..M][N..P]   <-- non-overlapping
```

During the merge, RocksDB also:

- **Drops tombstones** for keys that don't exist below this level.
- **Discards old versions** if multiple versions of the same key are
  present (only the latest stays).
- **Frees up disk space** previously held by overwritten or deleted
  data — this is when "deletes" actually become space savings.

Compaction is the heaviest cost in an LSM. It runs in the background
and competes for I/O with foreground writes/reads. Two main
strategies in RocksDB:

- **Level compaction** (default) — keep level sizes growing roughly
  10x. When a level exceeds its target, pick a victim file and merge
  it into the next level.
- **Universal compaction** — keep everything as a few SSTables in
  L0 and merge larger sets at once. Higher write throughput, more
  space amplification, longer pauses.

### 3.4 Three amplifications

LSMs are usually discussed in terms of three "amplification" numbers:

- **Write amplification (WAF)** — total bytes written to disk per
  byte of logical user write. In RocksDB this comes from the WAL,
  the L0 flush, and every compaction that re-writes the data into
  the next level. Typical: 10–30x.
- **Read amplification (RAF)** — disk reads per logical read. Bloom
  filters keep this small for point lookups (~3–5x typically). Range
  scans amplify more because they hit many SSTables.
- **Space amplification (SAF)** — total disk space used / logical
  data size. The non-compacted overlap and the tombstones cause this
  to be greater than 1. Universal compaction makes it worse.

The compaction strategy is essentially "pick which of W/R/S
amplification you minimize at the cost of the others." Different
workloads want different points on that curve.

### 3.5 Bloom filters

A bloom filter is a small probabilistic data structure that answers
"is K in the set?" with no false negatives (if it says no, K is
definitely not there) and configurable false positive rate (typically
1%). RocksDB stores one bloom filter per SSTable in the SSTable's
index block; on a point lookup, it consults the bloom filter before
doing any actual block I/O.

This is the magic that makes LSM point reads acceptable. Without
bloom filters, every read would have to actually open and binary-
search every potentially-relevant SSTable. With them, you skip most
of them entirely.

### 3.6 Sub-compactions, parallelism, rate limiting

RocksDB exposes a lot of tuning knobs for compaction parallelism
(how many compaction threads), rate limiting (how much bandwidth
compaction can use so it doesn't starve foreground reads), and
sub-compactions (splitting one compaction job across multiple
threads). All of this exists because **compaction is the dominant
cost** and you have to tune it to fit your hardware.

### 3.7 Comparison to a B-tree

| | B-tree (PG/InnoDB) | LSM (RocksDB) |
|---|---|---|
| In-place updates? | Yes | No (writes always sequential) |
| Random writes? | Yes (page splits, tuple updates) | Almost none |
| Read path | One B-tree descent | MemTable + N SSTables + bloom filters |
| Write amplification | Low (1–2x) | High (10–30x) |
| Range scan | Sequential at leaf level | Merge sort across SSTables |
| Background work | VACUUM / page compaction | Compaction |
| Best workload | Read-heavy, balanced | Write-heavy |

The TL;DR: B-trees pay the cost on writes (random I/O) to make reads
cheap. LSMs pay the cost on reads + compaction to make writes cheap.

## 4. Design Trade-Offs

### Sequential writes vs read complexity

You get amazing write throughput because every disk write is
sequential. But now reads have to potentially consult many SSTables
across many levels, mediated by bloom filters. For write-heavy
workloads (logs, metrics, time series, ingestion pipelines) this is a
huge win. For random read-heavy workloads, it's worse than a B-tree
unless you tune carefully.

### Compaction is mandatory

Without compaction, an LSM eventually stops working — reads slow down,
disk fills up with overwritten data, bloom filters stop helping. So
compaction is a permanent background workload that has to be sized
correctly. There's no equivalent in a B-tree (PG's VACUUM is the
closest, but it's much lighter).

### Tunable amplifications

This is genuinely cool — you can dial RocksDB's compaction to favour
write throughput (universal compaction, larger MemTables) or to
favour read latency (level compaction, more aggressive merges). There
isn't really an equivalent dial in B-tree storage engines.

### No clustered index, no secondary indexes (in core RocksDB)

RocksDB is a key-value store. There's no built-in "secondary index"
concept. If you want one, you build it yourself (often as a separate
column family with derived keys). MyRocks layers MySQL's SQL on top
and emulates clustered + secondary indexes via key encoding tricks.

### Snapshot isolation via sequence numbers

Each write gets a monotonically increasing sequence number. A
"snapshot" in RocksDB is just a sequence number value; reads against
that snapshot ignore any entry whose sequence number is greater. This
is conceptually similar to PG's `xmin`/snapshot rule, but expressed
at the key-value layer.

## 5. Experiments / Observations

I don't have RocksDB benchmarks to cite from a lab, but the experiments
the topic spec recommends are:

- Run `db_bench` (RocksDB's built-in benchmark) with different
  compaction strategies — level vs universal — and observe write
  amplification, read amplification, and space amplification.
- Run a write-heavy workload and watch how compaction threads spike
  when L0 fills up.
- Run a read-heavy workload with bloom filters on, then disabled,
  and see how point-lookup latency changes.

The qualitative pattern is well documented:

- Write throughput is way higher than a B-tree engine on the same
  hardware (5–10x is common for random writes).
- Point-lookup latency is comparable to a B-tree if bloom filters are
  on and the working set fits in the block cache; worse otherwise.
- Range-scan latency is the LSM's weakest area unless the working set
  is mostly in one or two levels.

Reading other people's benchmarks (Facebook's papers, CockroachDB's
posts, etc.), the consistent message is: **LSMs are not faster than
B-trees in general — they're faster on the specific workloads they
were designed for**. The wrong workload (random read-heavy with a
working set bigger than the block cache) can run worse on RocksDB
than on InnoDB.

## 6. Key Learnings

- **The fundamental trade-off in storage engines is when you pay for
  ordering.** B-trees pay on every write (maintain sorted order
  on disk). LSMs pay during compaction (sort batches of writes
  together, lazily). Same total work, distributed differently.

- **Sequential writes are still much cheaper than random writes on
  modern hardware.** Even on NVMe SSDs, where "random" reads are
  cheap, random *writes* still hit firmware-level wear levelling and
  write amplification at the flash layer. An LSM avoids that by
  design.

- **Bloom filters are doing enormous work.** Without them an LSM is
  basically unusable for point reads. With them, reads stay tolerable
  even when there are 10+ levels.

- **Compaction is the LSM equivalent of VACUUM, but it's central, not
  an afterthought.** PG can run for a while without VACUUM. RocksDB
  cannot run for any meaningful time without compaction — the system
  is *defined* by its background compaction.

- **Why people pick RocksDB at all:** when the workload is write-heavy
  enough that a B-tree storage engine can't keep up, or when you need
  embeddability + key-value semantics rather than full SQL. CockroachDB
  picked RocksDB (now their own LSM, Pebble) because every Cockroach
  node needs an embedded key-value store, not a full SQL engine —
  Cockroach builds the SQL layer above.

- **The lab series in this course doesn't touch LSMs at all.** All
  the trees we built were B-trees. After writing this up, I have a
  much better sense of *why* an LSM looks completely different —
  it's optimised for a different bottleneck. If I were extending the
  course's labs, an LSM memtable + a single L0 SSTable flush in C++
  would be the natural next exercise.

## References

- O'Neil et al., "The Log-Structured Merge-Tree" (1996) — the
  original paper.
- RocksDB official docs: <https://github.com/facebook/rocksdb/wiki>
- "Optimizing Space Amplification in RocksDB" (Dong et al., 2017) —
  Facebook's write-up of universal compaction.
- LevelDB design doc:
  <https://github.com/google/leveldb/blob/main/doc/impl.md>
- This repo: no lab corresponds directly to LSMs, but Lab 3 (buffer
  caching) and Lab 6 (MVCC via sequence/transaction IDs) are
  conceptually analogous to RocksDB's block cache and snapshot
  reads, respectively.
