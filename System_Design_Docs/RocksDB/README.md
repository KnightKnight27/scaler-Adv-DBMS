# RocksDB Architecture

An analysis of RocksDB as a representative LSM-tree (Log-Structured Merge-tree)
storage engine: why it turns random writes into sequential ones, what that buys and
what it costs, and how the read path is rebuilt around the fact that data is now
scattered across many immutable files instead of living in one updatable B-tree. The
recurring theme is the **amplification triangle** — write, read, and space
amplification are three quantities you cannot all minimize at once, and an LSM engine
is essentially a machine for choosing which one to spend. Throughout, the design is
contrasted with the B-tree engines (PostgreSQL, InnoDB) covered in the sibling docs,
because the LSM approach only makes sense as a deliberate inversion of theirs.

---

## 1. Problem Background

**Why RocksDB was created.** RocksDB was started at Facebook (2012) as a fork of
Google's LevelDB, re-engineered for **fast storage (flash/SSD) and server-class
multi-core hardware**, and to be an **embeddable** key-value engine — a library that
links into an application, like SQLite, rather than a standalone server. LevelDB
proved the LSM design but was single-threaded in compaction, tuned for spinning
disks, and limited in configurability. RocksDB kept the LSM core and added
multi-threaded compaction, configurable compaction strategies, column families,
prefix seeks, and extensive tunability — enough that it now sits underneath many
larger systems (MySQL's MyRocks, CockroachDB's older storage, Kafka Streams, Ceph,
TiKV, and countless metadata stores) as their write-optimized storage layer.

**Why LSM-tree databases exist.** The LSM idea (Patrick O'Neil et al., 1996) starts
from one observation: **sequential writes are dramatically faster than random
writes** on essentially every storage medium, and the gap is enormous on disk and
still large on SSD (where random writes also accelerate wear). A traditional update-
in-place B-tree, when ingesting a stream of inserts with scattered keys, performs
*random* writes all over the file. An LSM tree instead buffers writes in memory and
flushes them to disk **sequentially** as sorted, immutable files, never updating a
file in place. It trades a more complex read path and background rewriting for write
throughput.

**Problems with traditional B-tree databases (under write-heavy load).**

- *Random write I/O.* Inserting keys in non-sequential order dirties scattered pages;
  each must eventually be written back to its own location — many small random writes.
- *In-place update cost.* Every update may require reading a page, modifying it, and
  writing it back (read-modify-write), plus WAL — so the effective bytes written per
  logical byte is already greater than one.
- *Page splits and fragmentation.* High insert rates cause B-tree page splits,
  fragmentation, and locking/latching contention on hot pages.
- *Write amplification on SSD.* Small in-place updates interact badly with the SSD's
  own erase-block garbage collection, compounding wear and write amplification.

**Write-heavy workload requirements.** Logging, metrics/time-series, event streams,
message queues, IoT ingestion, and "write a lot, read recent" patterns need to absorb
a high, sustained insert rate with predictable latency, and care less about the cost
of an occasional point lookup. For these, an engine that converts the write stream
into large sequential flushes — accepting that reads and background work get harder —
is the right shape. RocksDB is that engine.

---

## 2. Architecture Overview

RocksDB keeps recent writes in memory (the **MemTable**), protected by a **write-
ahead log (WAL)** for durability, and periodically flushes them to immutable on-disk
**SSTables** organized into levels (**L0 … Ln**). Background **compaction** merges and
re-sorts SSTables to keep reads bounded and reclaim space.

```
 +-------------------------------------------------------------+
 |                     APPLICATION (embeds RocksDB)            |
 +------------------------------+------------------------------+
                                | Put / Get / Delete / Merge
                                v
 +-------------------------------------------------------------+
 |                          IN MEMORY                          |
 |   +-----------------+       +-----------------------------+  |
 |   |   MemTable      | --->  | Immutable MemTable(s)       |  |
 |   | (active, sorted)|  full | (read-only, awaiting flush) |  |
 |   +-----------------+       +--------------+--------------+  |
 |                                            | flush          |
 +--------------------------------------------|----------------+
            ^ durability                      v
            |                       +-------------------------+
   +-----------------+              |        ON DISK          |
   |   WAL (log)     |              |   L0:  [SST][SST][SST]  | (may overlap)
   | sequential file |              |   L1:  [ SST | SST | …] | (sorted, disjoint)
   +-----------------+              |   L2:  [ SST | … ]      | (~10x larger)
                                    |   ...                   |
                                    |   Ln:  [ SST | … ]      | (largest)
                                    +-----------+-------------+
                                                ^
                                                | compaction merges levels,
                                                | drops overwritten/deleted keys
```

**Write path.**

```
Client Put(k,v)
   |
   v
[ WAL append ]  -- sequential write to log (durability)
   |
   v
[ MemTable insert ] -- in-memory sorted structure; write returns
   |
   v  (when MemTable reaches write_buffer_size)
[ MemTable becomes IMMUTABLE; a new active MemTable is created ]
   |
   v
[ Flush: immutable MemTable written sequentially as a new L0 SSTable ]
   |
   v
[ Compaction (background): merge L0->L1->...->Ln, discard stale versions ]
```

**Read path.**

```
Client Get(k)
   |
   v  search newest -> oldest (first match wins)
[ active MemTable ]      --hit? return
   |
   v
[ immutable MemTable(s) ] --hit? return
   |
   v
[ L0 SSTables ]  -- may overlap, so check each (newest first)
   |
   v
[ L1 SSTable ]   -- disjoint ranges: at most ONE file can contain k
   |             -- (bloom filter consulted before reading each SST)
   v
[ L2 ... Ln ]    -- one candidate file per level
   |
   v
 not found anywhere -> key does not exist
```

The asymmetry is the whole story: a write touches **two** places (WAL + MemTable) and
returns immediately; a read may have to consult the MemTable, every L0 file, and one
file per deeper level — which is exactly why Bloom filters and the leveled, disjoint
layout exist (below).

---

## 3. Internal Design

### MemTable

**Purpose.** The MemTable is the in-memory write buffer and the most-recent layer of
the database. Every `Put`/`Delete`/`Merge` lands here (after the WAL append), and
every `Get` checks it first. Buffering writes in memory is what lets RocksDB convert
many small logical writes into one large sequential flush.

**Data structure.** The default MemTable is a **skip list**, chosen because it keeps
keys **sorted** (needed so the eventual flush produces a sorted SSTable and so range
scans work) while supporting **concurrent inserts** with good performance and no
rebalancing. Alternatives exist (hash-linked list, vector) for specialized workloads,
but the skip list is the general default because sorted-order + concurrency is exactly
what the flush and scan paths need.

**In-memory writes.** A write is "done" once it is in the WAL and the MemTable — no
disk seek into a data structure, no page split, no read-modify-write. Deletes are not
destructive here either: a delete inserts a **tombstone** (a marker that the key is
deleted), because the real data may still live in older SSTables and must be shadowed,
not erased.

### Immutable MemTable

**Flush process.** When the active MemTable fills (`write_buffer_size`), RocksDB
**seals** it into an *immutable* MemTable and immediately allocates a fresh active
MemTable so writes never stop. The immutable MemTable is read-only: it still serves
reads but accepts no new writes.

**Transition to SSTables.** A background flush thread serializes the immutable
MemTable — already sorted — into a new **L0 SSTable** with a single large sequential
write, then discards the corresponding WAL segment (it is no longer needed for
recovery once the data is durably in an SSTable). Multiple immutable MemTables can
queue up if flushing lags ingestion; if too many accumulate, RocksDB applies
**write stalls/throttling** to protect itself — an observable backpressure signal.

### SSTables

**Sorted storage.** An SSTable (Sorted String Table) is an **immutable** on-disk file
holding key-value pairs **sorted by key**. Immutability is the linchpin: because files
are never modified after creation, writes are always sequential, files can be read
without locking, and they can be cached/checksummed safely. Updates and deletes are
represented as *new* records in *newer* files that shadow older ones.

**File format.** A RocksDB SSTable is structured as:

```
 +------------------------------------------+
 | Data Block 0  (sorted k/v, prefix-compressed)
 | Data Block 1
 | ...                                      |
 | Data Block N                             |
 +------------------------------------------+
 | Filter Block   (Bloom filter for this file)
 | Index Block    (key -> data block offset)
 | Metadata / Footer (checksums, properties) |
 +------------------------------------------+
```

Keys are grouped into ~configurable-size **data blocks**; an **index block** maps key
ranges to block offsets so a lookup reads only the one relevant block; a **filter
block** holds the file's Bloom filter; the **footer** anchors it all with checksums.
Block-level compression (Snappy, LZ4, Zstd) and prefix compression of keys keep files
compact.

**Key organization.** Each entry carries an internal key = `user_key + sequence_number
+ type`. The monotonically increasing **sequence number** is how RocksDB orders
versions of the same user key (newer sequence = newer version) and how snapshots pick
the right version. Within a file keys are sorted by user key, then by *descending*
sequence number, so the newest version of a key is encountered first.

### WAL (Write-Ahead Log)

**Durability guarantees.** Before a write is applied to the MemTable, it is appended
to the WAL — a sequential on-disk log. Because the MemTable is volatile, the WAL is
what makes a committed write survive a crash. Sync behavior is tunable: `sync=true`
fsyncs each write (fully durable, slower), while the default buffers and lets the OS
flush (faster, a small window of risk) — the same durability/latency knob seen in
other engines.

**Crash recovery.** On restart, RocksDB **replays the WAL** to rebuild the MemTable
contents that had not yet been flushed to SSTables, restoring the exact pre-crash
state. SSTables themselves need no recovery — being immutable and checksummed, they
are either fully present or detected as corrupt. Once a MemTable is flushed, its WAL
segment is obsolete and removed, bounding recovery time.

### LSM Tree Levels

RocksDB (in its default *leveled* compaction) organizes SSTables into levels with a
crucial structural difference between L0 and everything below:

- **L0** holds SSTables flushed directly from MemTables. These files **can have
  overlapping key ranges** with each other, because each is just a dump of a MemTable.
  A lookup may therefore have to check *every* L0 file.
- **L1 and below** are kept **non-overlapping (disjoint)**: within a single level, key
  ranges partition the keyspace, so **at most one file per level can contain a given
  key**. This is what makes deep-level lookups cheap (one candidate file per level).
- **Higher levels are larger**, each roughly ~10× the previous (`max_bytes_for_
  level_multiplier`). Most data lives in the deepest, largest level; the upper levels
  hold the most recent changes.

**Data movement.** New data enters at the top (MemTable → L0) and is pushed downward
by compaction: L0→L1, L1→L2, …, gradually migrating from small, recent, overlapping
files into large, old, disjoint ones. A key's *journey* is from hot/in-memory to
cold/deep-on-disk.

### Compaction

**Why compaction exists.** Without it, an LSM tree would accumulate an ever-growing
pile of overlapping SSTables, so (a) reads would degrade — more files to search; (b)
space would balloon — obsolete versions and tombstones would never be reclaimed; and
(c) L1+ could not maintain its disjoint property. Compaction is the background process
that **merges SSTables, discards superseded versions and tombstoned keys, and re-sorts
data into the next level**. It is the price the LSM design pays to keep reads and space
bounded — the structural counterpart to its cheap writes (analogous to how VACUUM is
the counterpart to PostgreSQL's MVCC).

**Minor vs major compaction.**

- *Minor compaction* is the **flush** of an (immutable) MemTable into a new L0 SSTable
  — turning in-memory data into the first on-disk level.
- *Major compaction* merges SSTables across levels (e.g., L0→L1, or fully down to the
  last level). A *full* major compaction merges everything into the bottom level,
  producing the most compact possible form (and is sometimes triggered manually).

**Merge process.** Because inputs are all sorted, compaction is a **k-way merge**:

```
   input SSTs (sorted)         output SST (sorted, deduped)
   A: a1 c1 e1   ----\
   B: a2 c0 d1   -----> merge by key, keep NEWEST seq per user key,
   C: b1 e0      ----/   drop versions older than oldest snapshot,
                          drop a key entirely if newest record is a tombstone
                         => a(new) b c(new) d e(new)
```

For each user key the merge keeps the newest version visible to any live snapshot and
drops the rest; a tombstone that has reached the bottom level (where nothing older can
exist) is dropped entirely, finally reclaiming the deleted key's space.

**Compaction strategies.** *Leveled* (default) keeps each level disjoint and size-
tiered by ~10×, favoring read and space efficiency at higher write-amplification cost.
*Universal* (tiered) compacts similarly-sized files together, lowering write
amplification but raising space and read amplification — a direct, configurable move
along the amplification triangle.

### Bloom Filters

**Membership tests.** A Bloom filter is a compact probabilistic bit-array that answers
"is key *k* possibly in this SSTable?" RocksDB stores one per SSTable (in the filter
block) and consults it **before** reading the file's data blocks.

**False positives, never false negatives.** A Bloom filter can say "possibly present"
when the key is actually absent (false positive, at a tunable rate set by bits-per-key,
e.g. ~1% at 10 bits/key) but **never** says "absent" for a key that is present. That
one-sided guarantee is exactly what a read needs: a "definitely not here" lets RocksDB
**skip the file entirely**, avoiding its disk I/O.

**Read optimization.** Without Bloom filters, a point lookup for a *non-existent* (or
deep) key would have to probe one file per level all the way down — many disk reads.
With them, almost all of those files are skipped at the cost of a tiny in-memory bit
check, turning the worst case (negative lookups) from "read every level" into "check a
few bitmaps." This is the single most important reason LSM read performance is
tolerable for point queries.

### Read Path

**Lookup process.** A `Get(k)` searches layers **newest → oldest** and returns at the
**first match** (because newer versions shadow older ones): active MemTable →
immutable MemTable(s) → L0 files (each, newest first, since they overlap) → one
candidate file in L1, then L2, …, Ln. At each SSTable, the Bloom filter is checked
first; only on a "maybe" does RocksDB read the index block, then the one relevant data
block (often served from the **block cache**). If a tombstone is found first, the key
is reported as deleted.

**Searching across levels.** The disjoint property of L1+ bounds the work: at most one
file per deep level is a candidate, and Bloom filters eliminate most of those. The
genuinely expensive part is L0 (must check every file) and *range scans*, which cannot
use Bloom filters and must merge iterators across the MemTable and every level
simultaneously — which is why keeping the L0 file count and the number of levels in
check (via compaction) matters so much for read latency.

### Write Path

**Insert flow.** `Put(k,v)` → append to WAL → insert into the active MemTable → return.
No disk seek into a tree, no in-place modification, no page split — this is why writes
are fast and uniform in cost regardless of key locality.

**Flushes.** When the MemTable fills, it is sealed (immutable) and a new one takes
over; the immutable MemTable is flushed sequentially to an L0 SSTable, after which its
WAL is retired. If flushing can't keep up, immutable MemTables queue and eventually
trigger write stalls.

**Compactions.** Asynchronously, compaction merges L0 into L1 and cascades downward,
maintaining disjoint levels and reclaiming space. Compaction is where the *deferred*
cost of those cheap writes is actually paid — in background CPU and I/O — which leads
directly to the trade-off analysis.

---

## 4. Design Trade-Offs

LSM engines are defined by three amplification factors. The fundamental result (the
"RUM conjecture") is that you can optimize for at most two of read, update, and memory
at the expense of the third; RocksDB exposes knobs to choose *where* on this surface to
sit.

| Factor                | Definition                                              | Where it comes from in RocksDB                          |
|-----------------------|--------------------------------------------------------|---------------------------------------------------------|
| **Write amplification** | Bytes written to disk ÷ bytes written by the app      | Each key is rewritten every time compaction moves it to a deeper level (plus the WAL and the initial flush) |
| **Read amplification**  | Disk reads (or files probed) ÷ logical read           | A lookup may probe the MemTable, all L0 files, and one file per level; range scans merge across all of them |
| **Space amplification** | Bytes on disk ÷ bytes of live data                    | Obsolete versions and tombstones persist until compaction reclaims them; multiple copies exist mid-compaction |

**Why each occurs.**

- *Write amplification* is the direct cost of the leveled structure: to keep deep
  levels sorted and disjoint, a key written once may be physically rewritten ~5–10
  times over its lifetime as it migrates down. The very mechanism that makes reads and
  space bounded (compaction) is what rewrites the data.
- *Read amplification* exists because data for one key can be in many places at once
  (newest in MemTable, older shadowed copies down the levels). The engine must look in
  newest-first order until it finds the live version.
- *Space amplification* exists because nothing is updated in place — old versions and
  tombstones occupy space until a compaction that covers them runs.

**Why they are accepted (and how they're managed).**

| Amplification | Mitigation in RocksDB                                              | Why the trade is worth it                                  |
|---------------|-------------------------------------------------------------------|-----------------------------------------------------------|
| Write         | Universal/tiered compaction lowers it; larger MemTables flush less often | The payoff is sequential, high-throughput writes — the whole point |
| Read          | Bloom filters skip files; block cache; keep L0 small; disjoint L1+ | Most lookups become a few bitmap checks + one block read   |
| Space         | Compaction reclaims; compression (LZ4/Zstd) shrinks files          | SSD capacity is cheaper than SSD random-write throughput   |

The key insight: an LSM engine **spends write amplification and accepts read/space
amplification to buy sequential write throughput and write latency**, then claws back
read and space costs with Bloom filters, caching, and compaction. The B-tree engines
make the opposite default choice.

---

## 5. Experiments / Observations

Architectural observations (no fabricated numbers):

**Compaction behavior.** Under sustained ingestion you observe a sawtooth: write
throughput is high until flushes/compactions fall behind, at which point RocksDB
**throttles** and write latency spikes (write stalls). The visible levers are L0 file
count (too many → stalls) and pending compaction bytes. Compaction I/O competes with
foreground reads, so a write-heavy burst degrades read latency *indirectly* — a direct
consequence of deferring write cost to the background. Statistics like
`rocksdb.compaction.bytes.written` versus user bytes make write amplification
*measurable* rather than theoretical.

**Read/write workload effects.** Write-only and write-mostly workloads exercise the
LSM's strength: writes stay fast and uniform regardless of key distribution. Point
reads on *recent* keys are cheap (served from MemTable/block cache). Point reads on
*random old* keys are the worst case — they may descend many levels — which is exactly
where Bloom filters earn their keep. Range scans are inherently more expensive than in
a B-tree because the iterator must merge across the MemTable and every level instead of
walking one sorted structure.

**Bloom filter impact.** The clearest qualitative observation is on **negative
lookups** (keys that don't exist). Without Bloom filters, a missing-key `Get` must
check a candidate file at every level before concluding "absent." With filters, almost
all of those file reads are skipped via an in-memory bit test, collapsing the cost.
RocksDB exposes `rocksdb.bloom.filter.useful` (filters that avoided a read) versus
`bloom.filter.full.positive`, letting you confirm the filter is doing work. The cost is
memory (the filter blocks) and a small false-positive rate that wastes the occasional
unnecessary block read — a memory-for-I/O trade.

**General reasoning observations.** Official RocksDB and academic benchmarks
consistently show the *shape* (not specific numbers worth quoting): LSM engines lead on
write throughput and ingest, B-trees lead on read latency and especially range-scan
predictability, and leveled vs universal compaction trades write amplification against
space/read amplification. The reproducible lesson is the *direction* of each trade, not
a magic figure.

---

## 6. Comparison with PostgreSQL and InnoDB

PostgreSQL and InnoDB are **B-tree, update-in-place** engines; RocksDB is an **LSM,
append-and-merge** engine. They sit on opposite sides of the amplification triangle.

| Dimension              | B-Tree (PostgreSQL / InnoDB)                          | LSM-Tree (RocksDB)                                    |
|------------------------|------------------------------------------------------|------------------------------------------------------|
| Write pattern          | Update-in-place; random page writes + WAL            | Append to WAL + MemTable; sequential SSTable flushes |
| Read pattern           | One tree traversal to the page (predictable)         | Search newest→oldest across MemTable + levels        |
| Write amplification    | Lower per-write in steady state (but random I/O)     | Higher (compaction rewrites keys repeatedly)         |
| Read amplification     | Low (one structure)                                  | Higher (many files; mitigated by Bloom filters)      |
| Space amplification    | Moderate (bloat → VACUUM; or undo/purge)             | Variable (stale versions until compaction; compresses well) |
| Range scans            | Excellent (ordered leaves / clustered index)         | Workable but costlier (merge across levels)          |
| Background maintenance | VACUUM (PG) / purge (InnoDB)                          | Compaction                                            |
| Operational complexity | Mature tooling; tuning autovacuum / buffer pool      | Many compaction/level/cache knobs; stalls to manage  |

**B-Tree vs LSM-Tree (the essence).** A B-tree optimizes the *read* and keeps writes
acceptable by caching + WAL; an LSM optimizes the *write* and keeps reads acceptable by
Bloom filters + compaction. Both have a background "garbage" process (VACUUM/purge vs
compaction) because both defer cleanup — they just defer different cleanup.

**Read performance.** B-trees win on random point reads and especially range scans,
because the data is one ordered structure reached in a single traversal. LSM point
reads are competitive *with* Bloom filters and block cache, but range scans pay the
multi-level merge.

**Write performance.** LSM wins decisively on sustained, random-key insert throughput
because it never does random writes; B-trees suffer page splits and random I/O under
the same load.

**Storage efficiency.** LSM files compress very well (immutable, sorted, block-
compressed) and the bottom level is compact after compaction; B-trees carry page
fragmentation and per-engine bloat (PG dead tuples, InnoDB undo). But mid-compaction
and with stale versions, LSM space can temporarily balloon.

**Operational complexity.** Both need care. B-tree engines are mature with well-trodden
tuning. LSM engines expose more structural knobs (MemTable size, level multiplier,
compaction style, Bloom bits) and a characteristic failure mode — write stalls — that
operators must understand.

**When each is preferred.**

| Prefer **B-tree** (PostgreSQL / InnoDB) when…             | Prefer **LSM** (RocksDB) when…                          |
|-----------------------------------------------------------|--------------------------------------------------------|
| Read-heavy or balanced workloads                          | Write-heavy / high-ingest workloads                    |
| Frequent range scans and ordered access                   | Mostly point writes; recent-data reads                 |
| Need rich SQL, transactions, joins out of the box         | Need an embeddable, tunable KV engine under your system|
| Predictable low read latency matters most                 | Sequential write throughput and SSD endurance matter   |

---

## 7. Key Learnings

- **Why LSM trees are write-optimized.** They never update data in place. Writes go to
  an in-memory sorted buffer (after a sequential WAL append) and are later flushed in
  large sequential batches. This eliminates random write I/O — the single most
  expensive operation for a B-tree under heavy inserts — and turns write cost into
  fast, uniform, append-only work regardless of key distribution.
- **Why compaction is necessary.** Cheap writes are paid for later. Because every
  update and delete creates a *new* record that shadows old ones across immutable
  files, the database would otherwise drown in overlapping files and stale versions,
  destroying read performance and wasting space. Compaction merges files, discards
  superseded versions and tombstones, and maintains the disjoint deep levels that keep
  reads bounded. It is the structural counterpart to LSM's write speed — its VACUUM.
- **Why Bloom filters help.** Reads are the LSM's weak point because a key's live
  version could be in any of many files. A per-SSTable Bloom filter gives a one-sided
  "definitely not here" answer that lets RocksDB skip a file's I/O entirely. Since they
  never produce false negatives, they are safe to trust for skipping, and they convert
  the painful negative/deep lookup from "read every level" into "check a few bitmaps."
- **Engineering lessons.** The deepest lesson is the **amplification triangle**: write,
  read, and space amplification trade against one another, and no engine escapes all
  three. A storage engine is a *choice* of which amplification to pay. B-trees pay write
  (random I/O) to keep reads and space cheap; LSM trees pay read and space amplification
  to make writes sequential and cheap, then recover much of the read/space cost with
  Bloom filters, caching, compression, and compaction. RocksDB's value is not a single
  optimum but the *tunability* to move along that surface — leveled vs universal
  compaction, MemTable size, Bloom bits-per-key — to fit a workload. Choosing a storage
  engine well means knowing your read/write/space ratio and picking the engine whose
  *accepted* cost is the one you can afford.

---

## References

1. P. O'Neil, E. Cheng, D. Gawlick, E. O'Neil — "The Log-Structured Merge-Tree
   (LSM-Tree)", *Acta Informatica*, 1996 (the foundational LSM paper).
2. Facebook / Meta — *RocksDB Wiki and Documentation* (architecture, MemTable, flushes,
   compaction styles, Bloom filters, WAL, tuning). https://github.com/facebook/rocksdb/wiki
3. S. Dong et al. — "Optimizing Space Amplification in RocksDB", CIDR, 2017.
4. Google — *LevelDB documentation and source* (the LSM design RocksDB forked).
5. M. Athanassoulis et al. — "Designing Access Methods: The RUM Conjecture", EDBT, 2016
   (read/update/memory amplification trade-off).
6. N. Dayan, M. Athanassoulis, S. Idreos — "Monkey: Optimal Navigable Key-Value Store"
   / "Dostoevsky" (LSM tuning, Bloom-filter memory allocation, compaction trade-offs).
7. RocksDB statistics reference (`rocksdb.compaction.*`, `rocksdb.bloom.filter.*`,
   block cache counters) — for the observability points discussed in §5.
