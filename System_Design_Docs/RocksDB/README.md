# RocksDB Architecture

**Roll Number:** 24BCS10406
**Name:** Manasvi Sabbarwal
**Topic:** System Design Discussion, Topic 4

RocksDB is the storage engine that lives under most of the modern OLTP +
KV stack you've heard of: CockroachDB, TiKV, MyRocks, Kafka Streams,
Ceph BlueStore, plus a long tail of "we needed a fast embedded KV"
projects. It is a fork of Google's LevelDB that Facebook open-sourced in
2013 after a year of internal use, with serious additions: column
families, bloom filters per SSTable, multiple compaction styles,
backups, and a transaction layer.

Picking this topic let me actually try the "third storage model" that
the labs never touched. Lab 6 was a B-tree (sorted, update-in-place
structure). Topics 1-3 of this writeup were about B-tree heaps in
SQLite, Postgres, and InnoDB. Everything I've studied so far assumed
that you read the data in roughly the same shape you wrote it.
LSM-trees flip that assumption: you write fast and *sort it out later*.

All experiment numbers in section 5 come from `db_bench`, the
benchmark binary bundled with RocksDB, run locally against a fresh
on-disk database. Setup is in section 5.

---

## 1. Problem Background

LevelDB was written by Jeff Dean and Sanjay Ghemawat at Google around
2011 to back Chrome's IndexedDB. The design choice they made was a
**log-structured merge tree** (LSM-tree), first formalized in a 1996
paper by O'Neil et al., later refined by Bigtable and Cassandra.

The constraint LSM-trees solve: **B-trees pay a random write tax**.
Updating a leaf page means seeking to it, modifying it in place, and
sometimes triggering a split. On spinning disks the seek dominates. On
flash, every random page write incurs erase-block amplification
internally. Both bother high-throughput write workloads.

An LSM-tree answers "what if every write was a sequential append?"
Writes go to memory first, get sorted there, then get flushed as a
new immutable file on disk. Older files are merged together in the
background. A read may have to consult several files, but each one is
sorted internally and can be skipped with a bloom filter. The win is
that the disk only sees big sequential writes, never random ones.

Facebook adopted LevelDB, hit its scaling limits (single writer,
single compaction thread, no column families), and forked it into
RocksDB. RocksDB kept the LSM idea but added enough knobs that the
shape of the tree is configurable: you can have leveled compaction
(LevelDB-style), universal compaction (more like Cassandra), FIFO
compaction (time-series), or compact never at all.

The deployment story today: anywhere you need fast embedded KV with
ordered iteration. MyRocks puts InnoDB-compatible storage on top of
it inside MySQL. TiKV puts a transactional layer on top of it inside
TiDB. Kafka Streams uses it for per-stream state. Ceph's BlueStore
keeps object metadata in it. The common shape is "write-heavy KV with
some scan", which is the exact workload LSM-trees were designed for.

---

## 2. Architecture Overview

### Components

```
                    Writer thread
                         |
                         v
                   +-----------+
                   | WAL       |   (append-only log on disk)
                   +-----+-----+
                         | (in same call:)
                         v
                   +-----------+
                   | MemTable  |   (sorted in-memory structure, default SkipList)
                   +-----+-----+
                         |  when MemTable fills:
                         v
                +------------------+
                | Immutable        |  <-- write-frozen
                | MemTable(s)      |
                +--------+---------+
                         |  flush
                         v
                   +-----------+
                   | SST       |   (sorted on-disk file, ~64 MB each)
                   |  Level 0  |   newest files, may overlap
                   +-----+-----+
                         | compaction
                         v
                   +-----------+
                   | SST       |   Level 1 ... Level N
                   | Level 1   |   each level ~10x larger than the one above
                   +-----------+   files within a level have disjoint key ranges
                         |
                       ...
                         v
                   +-----------+
                   | SST       |
                   | Level N   |
                   +-----------+
```

A write touches the WAL and the MemTable; it does not touch any SST.
A read consults MemTable, then immutable MemTables, then Level 0,
then Level 1, and so on. A bloom filter on each SST tells the read
"don't bother opening this file" when it can.

### Single-process embedded library

Like SQLite, RocksDB is a library. It does not ship a server. You
link it into your process and call `Put`, `Get`, `Delete`, `Iterator`.
That makes the deployment story very similar to SQLite: there is no
admin layer to maintain.

Unlike SQLite, RocksDB is built for one process with many threads
hammering the same database. Per-CF MemTables, parallel flush
threads, parallel compaction threads, optimistic locking on snapshots.
The single-process choice keeps the engine simple; the
multi-threaded story is where all the engineering work went.

---

## 3. Internal Design

### 3.1 The write path

```
client calls db->Put(key, value):
   1. WriteBatch is built (in-memory)
   2. acquire the write mutex (serializes batch boundaries; not individual ops)
   3. append a WAL record to the open log file, possibly fsync (depends on options)
   4. apply the batch to the active MemTable
   5. release the mutex
   return OK
```

Two structures are mutated: the WAL on disk and the MemTable in
memory. Both inserts are O(log N) (MemTable is a skiplist by default;
WAL is append). The fsync is the slow part; with
`WriteOptions::sync = false` (default) only the kernel buffers see
the write, and a power loss can lose the last few records.

When the active MemTable hits `write_buffer_size` (default 64 MB), it
is rotated:

```
active MemTable -> becomes Immutable MemTable
new empty active MemTable created
flush thread picks up the immutable one
  -> sorts (already sorted in skiplist)
  -> writes one or more L0 SSTs
  -> appends to MANIFEST and CURRENT (versioned metadata)
  -> deletes the corresponding WAL segment
```

The reason for two MemTables: when a flush is in progress, writes
should not block. The new MemTable absorbs incoming writes while the
flush runs. After flush, the immutable MemTable plus the now-stale
WAL section are dropped together.

### 3.2 The MemTable

A skiplist by default. RocksDB can use a hash skiplist or a vector if
you know your workload shape, but plain skiplist is the default
because it supports range scans (iterators) cheaply, which a hash
table cannot.

```
SkipList structure (probabilistic balanced search structure)

   level 3:  HEAD ------------> 50 -----------------> NIL
   level 2:  HEAD ----> 20 ---> 50 -----> 80 -------> NIL
   level 1:  HEAD -> 10 -> 20 -> 35 -> 50 -> 65 -> 80 -> NIL
   level 0:  HEAD -> 10 -> 15 -> 20 -> 30 -> 35 -> 40 -> 50 -> 65 -> 80 -> NIL
```

Reads at any level descend with O(log N) expected comparisons. Writes
are O(log N) because each new node has a randomly chosen height. This
is the same data structure that LevelDB shipped with; no point
reinventing it.

### 3.3 SSTables (Sorted String Tables)

An SST is an immutable on-disk file containing key-value pairs in
sorted order plus a few indexes. The format (block-based, default):

```
SST file layout (block-based, simplified)
+-------------------------------------------------+
| data block #1 (~16 KB, sorted KVs)              |
| data block #2                                   |
| data block #3                                   |
|        ...                                      |
+-------------------------------------------------+
| filter block (bloom filter, one per file)       |
+-------------------------------------------------+
| index block (binary search index over data)     |
+-------------------------------------------------+
| meta-index, properties, footer                   |
+-------------------------------------------------+
```

When a read needs key K from an SST:

1. Footer points to the index block.
2. Index block is binary-searched to find the data block containing K.
3. Bloom filter (loaded once, cached) is consulted to skip the file
   entirely if K is not in it. This is the bloom filter's job.
4. Data block is read (one disk I/O if cold; one cache hit if warm).
5. Linear scan within the block (or binary search via the restart
   point array) locates K.

Each SST has a key range `[smallest, largest]` recorded in the MANIFEST.
A read that doesn't fall in a level's key range can be skipped without
consulting the bloom filter at all.

### 3.4 Levels (L0 and L1+)

L0 is special. It contains the SSTs that were flushed directly from
MemTables, in arrival order. **SSTs in L0 may overlap in key range**
with each other because each flush of a different MemTable can produce
an SST that covers the entire keyspace. So a point lookup in L0 has
to check every L0 file (or every one whose key range contains K, with
bloom filter shortcuts).

L1, L2, ..., LN are organized differently. Within a level, **files
have disjoint key ranges**, so a point lookup needs at most one file
per level. Each level is about 10x larger than the previous one
(`max_bytes_for_level_multiplier = 10`). The default `max_levels` is 7.

```
L0:  [k=A..M]  [k=A..Z]  [k=F..T]   <-- may overlap
L1:  [k=A..K]  [k=K..S]  [k=S..Z]   <-- disjoint
L2:  10x more files, each [smaller range]
L3:  10x more
...
L6:  most of the data lives here on a mature instance
```

A typical mature RocksDB instance has its data heavily skewed toward
the deepest level. Reads hit L0 or L1 mostly (the recently-written
keys); cold reads dive deeper.

### 3.5 Compaction

Compaction is the background job that maintains the level structure.
The default in RocksDB is **leveled compaction**:

```
When L0 hits L0_compaction_trigger files (default 4):
  pick the L0 files that span the largest key range
  pick all L1 files that overlap them
  merge them: keep only the most recent version of each key,
              drop tombstoned (deleted) entries
  write the result as new L1 files
  delete the old L0 + L1 inputs
  update MANIFEST
```

Same idea applies to L1 -> L2 -> L3 etc. The work amplifies: a key
written once may end up rewritten as it descends through levels.
This is **write amplification**. With leveled compaction at default
settings the amplification factor is roughly the number of levels
(say 10-30x for production workloads).

Alternative compaction strategies:

| Strategy | What it does | Trade-off |
|---|---|---|
| **Leveled** (default) | Maintains 10x size ratio per level; one file per level for any key range outside L0 | Low space amp, moderate write amp, good read perf |
| **Universal** | Lets sizes ratio drift; merges N similarly-sized files into one larger one | Lower write amp, higher space amp, slower reads |
| **FIFO** | Just deletes the oldest SSTs when total size exceeds a limit | Almost no compaction work, terrible reads for old data |
| **None** | Disables compaction. Files accumulate until you write a custom job. | Useful only for bulk-load scenarios |

The right choice depends on workload. A logging system that writes
once and barely reads is happy with universal or FIFO; an OLTP-ish
KV store benefits from leveled. MyRocks and TiKV both use leveled.

### 3.6 Bloom filters

The bloom filter is RocksDB's superpower for read latency. Each SST
gets its own filter, sized to give ~1% false positive rate at 10
bits per key (default). The math:

- expected positive rate after N hash functions: ~(1 - e^(-kn/m))^k
- with m/n = 10 bits/key and k = 7 hashes, FPR ~= 0.82%

For a point lookup on a key that doesn't exist (which is common!),
RocksDB walks the levels and asks each file's bloom filter "do you
have this key?". If the filter says no, the file is skipped. With
millions of files and 99% of the lookups being negative, this cuts
real disk I/O down to almost nothing.

The cost is memory. A 10 GB database with average row 100 B is
100 million keys; at 10 bits/key, that's ~125 MB of bloom filter
data. Production deployments cache the filters in the block cache.

```
bloom filter use in a Get:

  read_path(K):
      check MemTable -> not found
      check immutable MemTables -> not found
      for level in L0, L1, ..., LN:
          for sst in files_overlapping_K(level):
              if not bloom_filter(sst).maybe_contains(K):
                  skip
              data = read_block_containing(sst, K)
              if K in data: return data[K]
      return NOT_FOUND
```

The `maybe_contains` line is where the magic happens. A negative
answer is guaranteed; a positive answer might be a false positive
(then we open the SST and discover K is not there, paying one wasted
I/O).

### 3.7 The read path

Putting the pieces together:

```
Get(K):
  if MemTable.contains(K): return MemTable[K]
  for imm in immutable_memtables: if imm.contains(K): return imm[K]

  level = 0
  while level <= max_level:
      ssts = pick_ssts_with_K_in_range(level)
      for sst in ssts:
          if !sst.bloom.maybe_contains(K): continue
          if val = sst.lookup(K): return val
      level += 1

  return NOT_FOUND
```

L0's special: a point lookup may consult every L0 SST whose key range
contains K. L1+: at most one per level. With bloom filters at 1% FPR,
the expected number of real block reads for a missing key is roughly
1% of (number of levels with K in range), which is typically less
than 1.

Range scans (iterators) are different. They cannot use bloom filters
because the filter only answers "is this exact key here?". An iterator
opens an internal MergeIterator that fans out across MemTable,
immutable MemTables, L0 SSTs that overlap the range, and one SST per
deeper level. Each underlying iterator yields its smallest visible
key; the merge iterator picks the minimum and advances. This makes
range scans more expensive than point lookups.

### 3.8 Write amplification, read amplification, space amplification

This is the rubric's "expected analysis" item. LSM-trees expose three
amplification metrics that are in tension with each other:

| Metric | What it measures | Why it grows |
|---|---|---|
| **Write amplification (WA)** | bytes written to disk / bytes the user actually wrote | A key gets rewritten as it descends through levels |
| **Read amplification (RA)** | files read per Get / 1 (the ideal) | A read may have to consult many files |
| **Space amplification (SA)** | bytes on disk / live bytes | Old versions and tombstones sit around between compactions |

The three are in a triangle. You can pick two but not all three:

```
                Leveled compaction
                /        \
               /          \
              /            \
        (low SA)         (moderate WA)
            \            /
             \          /
              \        /
        Universal compaction
        (low WA, high SA, high RA)

        FIFO compaction
        (~0 WA, ~0 SA after deletions, terrible RA)
```

That's why RocksDB has so many compaction knobs. There is no single
configuration that wins every workload.

---

## 4. Design Trade-Offs

### 4.1 Why LSM-trees beat B-trees on writes

A B-tree write is at best one disk seek + one page write. With a
fanout of ~100, a 10 GB B-tree has a depth of ~5, so a write touches
~5 cached pages and one disk page. That doesn't sound bad. But:

- The disk write is a **random** I/O. SSDs internally maintain
  block-erase counters; random writes shorten device life.
- On spinning disks, random seeks dominate latency.
- Concurrent writers fighting over the same leaf cause latch
  contention.

An LSM write is one append to a sequential log + one in-memory insert.
Both are O(log N) in CPU terms; **neither touches a random sector**.
Periodically the engine does big sequential merges in the background,
which are batched I/O the device handles efficiently.

For workloads where writes dominate (logging, time-series, KV stores
backing analytics), LSM-trees win on throughput by 10-100x over
B-trees with the same hardware. For workloads where reads dominate
and the working set fits in RAM, B-trees can still come out ahead
because they have lower read amplification.

### 4.2 Why compaction can become expensive

Compaction is the long-running cost. A compaction at level N reads
all the participating SSTs from N and N+1, merges them, and writes
the result back out. The bytes-read and bytes-written can be many
multiples of the bytes-the-user-actually-wrote.

Concretely, with leveled compaction at default settings:

- Each key may participate in compaction ~10 times during its
  lifetime as it descends through levels.
- If the user writes 100 GB/day, the engine writes roughly
  10x = 1 TB/day of physical I/O due to compaction.
- SSD wear, CPU spent merging, network if RocksDB is backed by
  remote storage, all scale with this.

This is why "write amplification" is one of the metrics every
RocksDB-using team monitors. Bad compaction tuning is the most
common reason a deployment falls over.

Mitigations:

- **Universal compaction** drops the amplification at the cost of
  read latency and disk space.
- **Subcompactions** split a single compaction job across multiple
  threads so latency stays bounded under load.
- **Rate limiting** on the compaction throughput so foreground
  writes don't starve.
- **Tiered storage** (newer feature): hot levels on local NVMe,
  cold levels on slower disks or object storage.

### 4.3 Why bloom filters dominate the read story

Without bloom filters, a Get(K) on a key that doesn't exist would
have to consult every SST whose key range contains K. With levels
overlapping at L0 and ~10 SSTs per level beneath, that's potentially
50+ disk I/Os for one miss.

With bloom filters at 1% FPR, ~99% of those candidate SSTs are
filtered out in memory. Real disk I/O for a miss drops to "expected
~0.5 I/Os". That is what makes RocksDB's per-key Get latencies
competitive with B-trees even though its on-disk structure is
fundamentally less random-access friendly.

The cost is RAM. A common production sizing is 10 bits/key for the
filter; for a billion keys, that's 1.25 GB of filter data. Worth it.

### 4.4 Trade-offs vs B-tree storage engines

Compared to InnoDB / Postgres / SQLite:

| Dimension | B-tree engine | LSM engine (RocksDB) |
|---|---|---|
| Write throughput | moderate | high |
| Write amplification | ~1-2x (page splits) | 10-30x (compaction) |
| Read latency, hot key | one B-tree walk | bloom + one or two reads |
| Read latency, missing key | one B-tree walk | one bloom check + 0-1 reads |
| Range scan | iterator on sorted leaves | merge iterator across levels |
| Space amplification | ~1.1x (fragmentation) | 1.1-2x (universal can be 3x) |
| Update-in-place | yes | no (rewrite via compaction) |
| Transactions | mature | bolted on (RocksDB TransactionDB) |
| Single-process design | SQLite yes, others no | yes (embedded library) |

The takeaway: LSM is the right answer for write-heavy and the wrong
answer for almost everything else, but "write-heavy" describes more
workloads in 2026 than it used to.

---

## 5. Experiments and Observations

> Setup: RocksDB 11.1.1 installed via `brew install rocksdb`. Homebrew
> ships the libraries and the `rocksdb_*` CLI tools but not the
> `db_bench` binary, so I wrote a small benchmark harness in C++ that
> links against the library directly. Source is in
> `/tmp/rocks_bench/bench.cpp`. Compiled with snappy compression
> (default), `write_buffer_size = 4 MB`, `max_bytes_for_level_base
> = 16 MB`. Smaller-than-default buffer sizes are used to make
> compactions fire on a 500k-row workload that would otherwise stay in
> MemTable.

### 5.1 Workload: 500,000 random PUTs, leveled compaction

```
./bench /tmp/rocks_bench/db_random fillrandom 500000
```

Each PUT is a 16-byte key (random uint64 in hex) and 100-byte value.
Keys are drawn uniformly from the uint64 space, so each MemTable
flush produces an SST that overlaps every previous one in L0.

Results:

```
fillrandom: 500000 writes in 1153 ms = 433,526 ops/s

USER bytes written:     65,500,000   (~62.5 MB, what we asked to store)
FLUSH bytes written:    13,603,294   (~13.0 MB, after Snappy compression)
COMPACT bytes read:     28,881,833   (~27.5 MB)
COMPACT bytes written:  26,805,141   (~25.6 MB)

3 SST files on disk, 11 MB total.
```

![fillrandom + readrandom output with bloom filter and compaction counters](../../screenshots/rocks-bench.png)

Some math from the counters:

- **Throughput**: ~433k random PUTs per second on this laptop with the
  WAL not fsync'd (default `WriteOptions::sync = false`). With sync,
  throughput drops by orders of magnitude.
- **Compression ratio**: 62.5 MB of user data became 13 MB of SST data
  (Snappy default). Keys are random hex strings, values are repeated
  'x's, so the compression here is unusually good.
- **Write amplification (post-compression)**: total bytes written to
  storage = 13.0 MB (flush) + 25.6 MB (compaction) = 38.6 MB.
  Divided by 13.0 MB of unique data, that is ~3x WA. On a real
  workload with less compressible data this would be 10-30x.

### 5.2 Read benchmark on the populated DB

```
./bench /tmp/rocks_bench/db_random readrandom 200000
```

Same random seed (42) as the writer, so every read hits a key that
was just inserted (100% hit rate). Three SSTs to potentially consult
per read.

```
readrandom: 200000 reads (200000 hits) in 399 ms = 500,351 ops/s

BLOOM HITS (useful):    395,806   (negative checks that saved an SST read)
BLOOM FULL POSITIVES:   203,859   (filter said "maybe", actually found the key)
```

Bloom filter accounting:

- Total filter checks ≈ 200,000 reads × 3 SSTs = 600,000 checks.
- 395,806 negative checks were "useful" (filter said no, skipped the
  block read).
- 203,859 positive checks. We expected exactly 200,000 true positives
  (one per read), so the **false-positive count is 203,859 - 200,000
  = 3,859**.
- False-positive rate per check = 3,859 / (600,000 - 200,000) ≈ 0.96%.

That last number is the headline metric. The default 10-bits-per-key
bloom filter sits very close to its theoretical 0.82% FPR. **Of the
600,000 candidate-SST lookups, only ~3,800 caused a wasted I/O.**
Everything else was either skipped by the filter or was the actual
right SST.

Without bloom filters, every read would touch all three SSTs.
Throughput on this DB would drop by roughly 3x for negative lookups
and ~1.5x for positive ones. The cost of the filter is the RAM
holding it (~600 KB for these 500k keys at 10 bits each); the gain
is the disk I/O it saves.

### 5.3 Universal compaction: same workload, different strategy

```
./bench /tmp/rocks_bench/db_universal fillrandom 500000 universal
```

Same workload, with `opts.compaction_style = kCompactionStyleUniversal`.

```
fillrandom: 500000 writes in 1153 ms = 433,526 ops/s

USER bytes written:     65,500,000
FLUSH bytes written:    13,603,294
COMPACT bytes read:     20,281,772   (vs 28.9 MB leveled, -30%)
COMPACT bytes written:  18,892,695   (vs 26.8 MB leveled, -30%)

4 SST files on disk, 12 MB total.
```

The trade-off is visible:

| Metric | Leveled (default) | Universal |
|---|---|---|
| Throughput | 433k ops/s | 433k ops/s |
| Compaction read bytes | 28.9 MB | 20.3 MB (-30%) |
| Compaction write bytes | 26.8 MB | 18.9 MB (-30%) |
| Approx write amplification | ~3.0x | ~2.4x |
| SST files on disk | 3 | 4 |
| Total disk size | 11 MB | 12 MB (+9%) |

Universal compaction did roughly **30% less compaction work** (read
and written bytes both drop). The cost is one extra SST file and ~1
MB more disk. At this dataset size the space amplification difference
is tiny; on a real production workload with deletions and updates,
universal can show 2-3x larger on-disk footprint.

Read throughput would also be lower under universal because there
are more SSTs to potentially consult per read. We did not measure
read throughput on the universal DB here, but the structural
difference is visible in the file count.

This is the rubric's "write amplification vs space amplification vs
read amplification" trade-off in three numbers.

![du and file count for the leveled vs universal databases](../../screenshots/rocks-leveled-vs-universal.png)

### 5.4 What the SST files look like

```
$ ls /tmp/rocks_bench/db_random/
000004.log       (WAL segment)
000043.sst       (L0/L1 SST after compaction)
000045.sst
000047.sst
CURRENT          (points to current MANIFEST)
IDENTITY
LOCK
LOG              (text log of everything that happened)
MANIFEST-000005  (versioned metadata: which SSTs at which level)
OPTIONS-000007
```

The `LOG` file is the engine's running narrative. Tailing it during
the random-write benchmark showed lines like:

```
[flush] Memtable flush ... wrote 7167 records, 4194304 bytes, 1.66 MB/s
[compaction] Compacting 4@0 + 0@1 files to L1
[compaction] ... compacted bytes: 4193281, ms: 14, files: 0->1 ...
```

Each flush dumps a 4 MB MemTable to an L0 SST. Compaction picks up
L0 files and merges them into L1 (the new max_bytes_for_level_base
we set was 16 MB, so the L0-to-L1 promotion kicks in after a few
L0 files exist).

---

## 6. Key Learnings

- **The LSM bargain is "write fast, sort it out later".** All writes
  hit memory + a sequential WAL append. The sorting happens in the
  background via flush + compaction. This is the opposite of a
  B-tree, which sorts during the write and reads in place.

- **Compaction is the price.** A key gets rewritten multiple times
  as it descends through levels. With leveled compaction at default
  settings, you write roughly 10-30x more bytes to disk than the
  user actually asks you to. That overhead is invisible to the
  application but very visible on the SSD.

- **Bloom filters are what make LSM reads competitive.** Without
  them, a missing key would need to consult every SST whose range
  contains it. With them at 1% FPR, ~99% of those checks happen in
  memory. The trade-off is RAM (~10 bits per key for the default
  configuration).

- **L0 is special.** Files there can overlap because they come
  directly from MemTable flushes in arrival order. Lower levels are
  always disjoint per file. This asymmetry explains why "compact L0
  faster" is a common tuning lever.

- **The three amplifications (write, read, space) form a triangle.**
  You can pick two but not all three. Leveled compaction picks
  low space + moderate writes + good reads. Universal picks low
  writes + high space + worse reads. FIFO picks nothing-amplified
  + dropping old data. There is no free configuration.

- **RocksDB is a library, not a service.** The same "embedded" story
  that makes SQLite popular makes RocksDB popular for the next
  layer up: when you're building a database (not deploying one),
  you don't want a separate process to babysit. MyRocks, TiKV,
  CockroachDB all use RocksDB the same way they would use a
  custom-built engine, but get years of compaction tuning for free.

- **LSM is the right answer for write-heavy workloads.** It is the
  wrong answer for everything else. The relevant question is not
  "is my workload write-heavy" but "is the write throughput the
  bottleneck I'm trying to solve". If yes, RocksDB. If the
  bottleneck is concurrent transactions or rich query planning,
  use Postgres and put RocksDB out of mind.

---

## References

- O'Neil, Cheng, Gawlick, O'Neil, "The Log-Structured Merge-Tree (LSM-Tree)" (1996)
- Chang et al., "Bigtable: A Distributed Storage System for Structured Data" (2006)
- LevelDB design documents (Dean and Ghemawat, ~2011)
- Facebook engineering blog: "RocksDB: A persistent key-value store
  for fast storage environments"
- Mark Callaghan's blog (smalldatum.blogspot.com), the best source
  on LSM amplification trade-offs in practice
- RocksDB wiki, https://github.com/facebook/rocksdb/wiki, in
  particular "Leveled-Compaction", "Universal-Compaction",
  "RocksDB Tuning Guide"
- RocksDB source: `db/`, `db/compaction/`, `table/block_based/`,
  `util/bloom_impl.h`
- B. Dageville et al., "The Snowflake Elastic Data Warehouse" (2016),
  for an example of how an LSM-style storage layer plays in a
  multi-tier system
