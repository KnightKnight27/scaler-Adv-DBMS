# RocksDB Architecture (LSM-Tree Storage Engine)

## 1. Problem Background

B-tree storage engines (Postgres, InnoDB) update pages in place, which means
every write requires a random-access read-modify-write somewhere in the tree.
On spinning disks, and even on flash with finite erase cycles, random writes
are expensive relative to sequential writes. RocksDB (built at Facebook,
forked from Google's LevelDB) was designed around the opposite assumption:
**make writes cheap and sequential, and accept that reads may need to check
multiple places.** This is the **Log-Structured Merge-tree (LSM-tree)** model,
and it exists specifically for write-heavy workloads — logging systems,
time-series ingestion, write-intensive key-value stores backing larger
systems (RocksDB itself underlies parts of MySQL via MyRocks, CockroachDB,
TiKV, and Kafka Streams' state stores) — where a B-tree's random-write cost
would dominate.

## 2. Architecture Overview

```
            Write Path                                  Read Path
        ┌───────────────┐                          ┌──────────────────┐
        │  Client Write │                          │  Client Get(key) │
        └──────┬────────┘                          └─────────┬────────┘
               │ 1. append                                   │ check in order:
               ▼                                             ▼
        ┌───────────────┐                          ┌─────────────────┐
        │      WAL      │                          │    MemTable     │
        └──────┬────────┘                          └─────────┬───────┘
               │ 2. insert                                   │ miss
               ▼                                             ▼
        ┌───────────────┐                          ┌────────────────────┐
        │   MemTable    │ ── full ──▶ Immutable    │ Immutable MemTable │
        │ (in-memory    │             MemTable     └─────────┬──────────┘
        │  skiplist)    │                                    │ miss
        └───────────────┘                                    ▼
                                  flush                ┌─────────────────────┐
        ┌───────────────────────────────────┐          │  Bloom Filters      │
        │              L0 SSTables          │◀──────── │ (per SSTable, skip  │
        │   (unsorted, overlapping ranges)  │          │  files that can't   │
        └──────────────┬────────────────────┘          │ contain the key)    │
                       │ compaction                    └─────────┬───────────┘
                       ▼                                         ▼
        ┌───────────────────────────────────┐          ┌───────────────────────┐
        │       L1 SSTables (sorted,        │          │  L0 → L1 → ... → Ln   │
        │       non-overlapping)            │◀──────── │   checked in order,   │
        └──────────────┬────────────────────┘          │   newest data wins    │
                       │ compaction                    └───────────────────────┘
                       ▼
              L2 ... Ln (exponentially larger, sorted, non-overlapping)
```

## 3. Internal Design

### 3.1 Write Path

1. **WAL append**: the write (put/delete) is first appended to a write-ahead
   log file — sequential I/O, durable once fsynced. This is RocksDB's
   crash-recovery mechanism, conceptually identical in purpose to Postgres's
   WAL or InnoDB's redo log.
2. **MemTable insert**: the write is also inserted into the active
   **MemTable**, an in-memory sorted structure (a skiplist by default).
   No disk I/O happens here beyond the WAL append — this is *why writes are
   cheap*: a write is one sequential log append plus one in-memory insert,
   nothing more.
3. **MemTable becomes immutable**: once the active MemTable exceeds a size
   threshold, it is frozen as an **Immutable MemTable** and a new active
   MemTable is created to absorb further writes.
4. **Flush**: a background thread serializes the immutable MemTable to disk as
   a new **SSTable** (Sorted String Table) at level **L0**, then the
   corresponding WAL segment can be discarded (its data is now durably
   represented in the SSTable).

### 3.2 Read Path

A `Get(key)` must check, in order, every place a value could live, because
newer writes are not necessarily merged into older data yet:
1. Active MemTable
2. Immutable MemTable(s) not yet flushed
3. L0 SSTables (which may overlap in key range, since they're flushed
   independently, so multiple L0 files might need checking)
4. L1 through Ln SSTables, where each level's files are sorted and
   non-overlapping, so at most one file per level needs checking

To avoid opening and scanning every SSTable on every read, RocksDB checks a
per-file **Bloom filter** first — a compact probabilistic structure that can
say "definitely not in this file" with zero false negatives, letting the read
skip the disk I/O for files that provably don't contain the key. Only files
the Bloom filter can't rule out are actually opened and binary-searched (using
each SSTable's internal index block).

### 3.3 SSTables and Levels

An SSTable is an immutable, sorted file of key-value pairs plus an index block
(for binary search) and a Bloom filter block. Immutability is central to the
design: once written, an SSTable is never modified — updates and deletes are
represented as *new* entries (a delete is a "tombstone" entry), and reconciling
old vs new values across files happens only during **compaction**, never
during the write itself.

Levels are organized so each level is roughly an order of magnitude larger
than the one above it (`level_multiplier`, often 10x), and `L1..Ln` levels keep
their files sorted and non-overlapping by key range — this non-overlap is what
allows a read in those levels to consult at most one file per level rather than
scanning many.

### 3.4 Compaction

Compaction is the background process that merges SSTables from one level into
the level below, throwing away overwritten and tombstoned data along the way.
- **L0 → L1 compaction**: because L0 files can overlap, this step often
  involves merging multiple L0 files together with overlapping L1 files,
  producing new sorted, non-overlapping L1 files.
- **Ln → Ln+1 compaction**: standard leveled compaction picks files whose key
  ranges overlap between adjacent levels and merges them, again producing
  new, non-overlapping files at the lower level.
- RocksDB also supports a **universal (tiered) compaction** strategy, which
  defers merging longer and does fewer, larger merge passes — trading more
  read amplification and temporary space usage for less total write work,
  versus leveled compaction's opposite trade-off.

**Why compaction is required**: without it, the number of SSTables (and
therefore the number of files a read must check) would grow without bound,
and obsolete/overwritten/deleted data would never be reclaimed, growing disk
usage indefinitely — compaction is doing for an LSM-tree what VACUUM does for
Postgres's heap, but proactively merging files rather than just marking space
reusable in place.

### 3.5 Bloom Filters

A Bloom filter is a fixed-size bit array with k hash functions; checking
membership hashes the key k times and checks whether all corresponding bits
are set. **How Bloom Filters improve read performance**: for keys that don't
exist in a given SSTable (the common case when many files exist across many
levels), the filter answers "not present" without touching disk, at the cost
of a small, tunable false-positive rate (never a false negative) — this turns
what would be an O(levels) disk-seek read into something much closer to O(1)
disk seeks in practice for keys that exist in only one or two files.

## 4. Design Trade-Offs

**Why are LSM trees preferred in write-heavy workloads?**
Writes become sequential WAL appends plus in-memory inserts — no random
disk seeks on the write path at all, which is dramatically faster than a
B-tree's read-modify-write-in-place pattern, especially under heavy write
concurrency or on storage media where random writes are costlier than
sequential ones.

**Why can compaction become expensive?**
Compaction re-reads and re-writes large volumes of existing data purely to
keep levels sorted and reclaim space — this is **write amplification**: a
single logical write can be physically rewritten multiple times as it migrates
down through levels during successive compactions. Under sustained heavy
write load, compaction can consume enough I/O and CPU bandwidth to compete
directly with foreground writes, occasionally causing write stalls if
compaction falls behind and L0 accumulates too many files.

**Read performance trade-offs**: a read may need to check the MemTable, one or
more immutable MemTables, several L0 files, and one file per remaining level —
strictly more places to look than a B-tree's single O(log n) descent. This is
**read amplification**, and it's the structural cost LSM trees accept in
exchange for cheap writes. Bloom filters substantially mitigate this for
non-existent keys but do not eliminate the cost for keys that do exist and
might be scattered across levels.

**Storage efficiency trade-offs**: because old/overwritten versions and
tombstones linger until compaction physically removes them, an LSM tree's
on-disk size temporarily exceeds the logical data size — this is **space
amplification**, and it's tunable via compaction strategy (leveled compaction
keeps space amplification lower; universal/tiered compaction keeps write
amplification lower but lets space amplification rise) — there is no setting
that minimizes all three of write, read, and space amplification
simultaneously; every compaction strategy choice is a trade among them.

## 5. Experiments / Observations

Using RocksDB's `db_bench` tool to compare compaction strategies under a
sustained random-write load (`fillrandom`) followed by point lookups
(`readrandom`) makes the three amplification metrics directly observable:

```bash
# Leveled compaction (default)
./db_bench --benchmarks=fillrandom,readrandom \
  --compaction_style=kCompactionStyleLevel \
  --num=5000000 --statistics

# Universal (tiered) compaction
./db_bench --benchmarks=fillrandom,readrandom \
  --compaction_style=kCompactionStyleUniversal \
  --num=5000000 --statistics
```

Expected pattern in the printed `--statistics` output:
- **Leveled compaction**: higher reported `compact_write_bytes` relative to
  raw bytes written (higher write amplification), but smaller final on-disk
  size (lower space amplification) and fewer files to check per read on
  average (lower read amplification) — this is the typical reason production
  RocksDB-backed systems (e.g. MyRocks) default to leveled compaction for
  general-purpose workloads.
- **Universal compaction**: noticeably lower write amplification under the
  same write load, but the on-disk size grows larger before being reclaimed
  (higher space amplification), and reads have more files to check before a
  full merge completes.

Watching `rocksdb.compaction.times.cputime` and `rocksdb.compaction.times.micros`
statistics under sustained writes also makes write stalls observable directly
as compaction debt accumulates when write throughput exceeds compaction
throughput.

## 6. Key Learnings

- LSM trees are not "a worse B-tree" — they are a deliberate inversion of
  where the cost is paid: B-trees pay cost on every write (random I/O),
  LSM trees defer and batch that cost into background compaction, paying it
  later and more efficiently in bulk sequential I/O.
- Every LSM design parameter (compaction style, level size multiplier, Bloom
  filter bits-per-key) is really a dial between **write, read, and space
  amplification** — there is no free lunch, only different points on that
  trade-off surface.
- Immutability of SSTables is what makes LSM trees easy to reason about
  concurrently — readers never need to lock against an in-place mutation,
  because there isn't one; all mutation happens via compaction producing
  *new* files, conceptually similar to how Postgres never overwrites a tuple
  in place.
- Bloom filters are a cheap, probabilistic way to convert "is this key
  possibly here" into "definitely not here" for the common negative case,
  and they matter most precisely because LSM reads must otherwise check many
  files.


## Architectural Lessons

RocksDB demonstrates how database systems can fundamentally change the location of performance costs rather than eliminating them. Instead of paying the cost during every write operation, RocksDB defers much of that work to background compaction processes.

The LSM-tree architecture shows that optimizing for one workload often requires accepting trade-offs elsewhere. RocksDB achieves extremely high write throughput through sequential writes and immutable SSTables, but this introduces read amplification, space amplification, and compaction overhead.

The most important lesson is that storage engine design is largely an exercise in choosing where work should occur. B-tree systems perform more work during writes to keep reads simple, while LSM-tree systems perform less work during writes and accept additional complexity during reads and compaction. RocksDB is a clear example of how workload characteristics drive architectural decisions.


## References
- RocksDB Wiki (architecture, compaction, Bloom filters) — github.com/facebook/rocksdb/wiki
- "The Log-Structured Merge-Tree (LSM-Tree)" — O'Neil et al., original paper
- RocksDB source: `db/`, `table/`, `util/bloom_impl.h`