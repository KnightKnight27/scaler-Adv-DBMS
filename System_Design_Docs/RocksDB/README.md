# RocksDB Architecture (LSM-Tree Storage)

**Author:** Manjari Rathore
**Roll Number:** 23BCS10192
**Course:** Advanced DBMS — System Design Discussion

> The three other systems I studied (PostgreSQL, SQLite, MySQL/InnoDB) all store
> data in **B-trees** and update them **in place**. RocksDB does the opposite: it uses a
> **Log-Structured Merge-tree (LSM)** that *never* updates in place — it collects
> writes in memory, flushes them as large sequential files, and merges those
> files in the background. This document explains that design and, above all,
> *why* it is faster for writes. RocksDB comes directly from Google's
> **Bigtable** (Bigtable → LevelDB → RocksDB), so I base the internals on the
> Bigtable paper from my course resources, use the Red-Black tree from **Lab 5**
> as the in-memory MemTable equivalent, and use the B-tree from **Lab 6** as the
> in-place baseline that makes the LSM trade-off clear.
>
> *Honesty note:* I did not run `db_bench` myself, so the amplification figures in
> §5 come from the RocksDB documentation and the LSM literature (cited)
> and from my own analysis using Lab 6. They are clearly labelled as such, not
> presented as my own measurements.

---

## 1. Problem Background

### Why LSM-trees exist

A B-tree is wonderful for reads — one root-to-leaf descent finds any key — but
its writes are its weakness. Inserting or updating a key means modifying **the
leaf page where it belongs**: a **random** write to a specific location on disk,
and if the page is full, a split that triggers *more* random writes. For
**write-heavy** workloads (logging, time-series, metrics, message queues,
write-heavy web services), this flood of small random writes is the bottleneck.
Random I/O is the slowest thing a disk does, and on SSDs it also causes extra
writes and wear inside the device.

The **Log-Structured Merge-tree** (O'Neil, Cheng, Gawlick & O'Neil, 1996) was
designed to fix exactly this: **turn random writes into sequential writes.**
Instead of updating a page in place, you (1) append the change to an in-memory
buffer, and (2) periodically flush that buffer to disk as one big sequential
file, then (3) merge files in the background. Sequential writes are dramatically
faster than random ones on every storage medium, so the write path becomes cheap.

### The lineage — Bigtable → LevelDB → RocksDB

Google's **Bigtable** (Chang et al., OSDI 2006 — a course resource) is the
production system that popularized this design: its `memtable` + immutable
`SSTable` + compaction machinery is *exactly* the LSM. Google open-sourced a
single-node version of that storage engine as **LevelDB**. Facebook forked
LevelDB into **RocksDB** (2012), tuning it for **flash/SSD** and high core
counts, and made it an **embeddable key-value storage engine** — a library you
link into your process (like SQLite), not a server. RocksDB today is the storage
engine *underneath* many databases (MySQL's MyRocks, CockroachDB's older engine,
TiKV, Kafka Streams state stores, etc.). So studying RocksDB is studying the
storage layer of a whole generation of systems.

---

## 2. Architecture Overview

```
   put(k,v) / delete(k) ─┐
                         │ 1. append to WAL (durability)      ┌─────────────────┐
                         ├───────────────────────────────────►│  WAL (log file) │
                         │ 2. insert into active MemTable      └─────────────────┘
                         ▼
   ┌──────────────── MEMORY ───────────────┐
   │  active MemTable   (sorted; skiplist)  │ ◄── in-memory sorted index (cf. Lab 5)
   │       │ fills up (write_buffer_size)   │
   │       ▼                                │
   │  immutable MemTable ── flush ──────────┼──┐
   └────────────────────────────────────────┘  │  3. flush → new SSTable at L0
   ┌──────────────── DISK ──────────────────┐   ▼
   │  L0:  [sst] [sst] [sst]   (ranges may OVERLAP — straight from flushes)        │
   │  L1:  [────sst────][────sst────][────sst────]   non-overlapping sorted run    │
   │  L2:  [──sst──][──sst──][──sst──]…   ~10× larger than L1                       │
   │   ⋮                                                                            │
   │  Ln:  …                              ~10× larger than Ln-1                     │
   │        ▲ background COMPACTION merges level k into level k+1                   │
   │  each SSTable = sorted, IMMUTABLE: data blocks + block index + Bloom filter    │
   └────────────────────────────────────────────────────────────────────────────┘
            reads also consult:  Block Cache (LRU, cf. Lab 3)
```

**Write path (steps 1–3 above):** every write is appended to the **WAL** (so a
crash loses nothing) and inserted into the in-memory **MemTable**. When the
MemTable fills, it is frozen into an **immutable MemTable**, a fresh one takes
over, and the frozen one is **flushed** to a new **SSTable** at level 0. All disk
writes are large and sequential.

**Read path:** newest-wins. Check the active MemTable, then immutable MemTables,
then L0 SSTables (all of them — they overlap), then one SSTable per level in
L1…Ln. A **Bloom filter** on each SSTable is checked first to skip files that
*can't* contain the key.

This is precisely Bigtable's Figure-5 "tablet representation": writes go to a
commit log + memtable; reads merge the memtable over a stack of SSTables.

---

## 3. Internal Design

### 3.1 MemTable — the in-memory sorted buffer (grounded in Lab 5)

The MemTable is an **in-memory, sorted, mutable index**. It must support fast
inserts and fast *ordered* iteration, because when it is flushed it must produce
a **sorted** SSTable. RocksDB's default MemTable is a **skip list** (chosen
because it allows concurrent lock-free inserts), but the *requirement* is just
"a balanced, ordered, in-memory structure."

That is exactly what I built in **Lab 5** — a **Red-Black tree**:

```cpp
// Lab 5 — a self-balancing, ordered in-memory index (RED/BLACK rotations)
void insert(int val) {
    /* normal BST insert … */
    fixInsert(node);     // rotate + recolor to keep it balanced → O(log n)
}
```

A Red-Black tree keeps keys sorted with O(log n) insert/search and supports an
in-order walk — every property a MemTable needs. So Lab 5 *is* a working
MemTable, just with a different balanced structure than RocksDB's default:

| Requirement of a MemTable | Lab 5 Red-Black tree | RocksDB default (skiplist) |
|---|---|---|
| Keys kept **sorted** | ✅ in-order traversal | ✅ ordered levels |
| Fast insert / lookup | ✅ O(log n) | ✅ O(log n) expected |
| Ordered scan for flush | ✅ inorder walk → sorted SSTable | ✅ bottom-level scan |
| Concurrent writers | ✗ (needs locking) | ✅ lock-free — *why RocksDB prefers it* |

Understanding why RocksDB picked a skiplist over my Red-Black tree *is* the
lesson: both are correct ordered in-memory indexes; the skiplist wins on
**concurrent write throughput**, which is what a write-optimized engine cares
about most.

### 3.2 SSTables — sorted, immutable files (from Bigtable)

When a MemTable flushes, it becomes an **SSTable (Sorted String Table)**: an
immutable, sorted file mapping keys → values. From the Bigtable paper, an SSTable
is "a sequence of blocks (typically 64 KB each); a block index (stored at the end
of the SSTable) is used to locate blocks; the index is loaded into memory when the
SSTable is opened." A lookup is one binary search of the in-memory index, then one
block read (or zero, if the block is cached / the SSTable is mmap'd).

The defining property is **immutability** — once written, an SSTable is *never
modified*:

- **No in-place random writes, ever.** Updates and deletes are written as *new*
  entries in *newer* SSTables; the old value just becomes unreachable.
- **Reads need no locks.** An immutable file can be read and cached freely by
  many threads — no latching like a B-tree page.
- **Compaction is just "read some SSTables, write a new one, delete the old"** —
  safe because nothing mutates in place.

Because newer data lives in newer SSTables, **a delete is itself a write**: it
inserts a **tombstone** marker that hides older versions of the key. The key is
only physically reclaimed when compaction reaches the bottom level (Bigtable:
"SSTables produced by non-major compactions can contain special deletion entries
that suppress deleted data in older SSTables").

### 3.3 Levels L0…Ln (leveled organization)

```
L0   [a–z][c–m][b–q]      ← straight from MemTable flushes; ranges OVERLAP
L1   [a–f][g–m][n–t][u–z] ← non-overlapping; one sorted run; ~10× size of L0
L2   …                    ← non-overlapping; ~10× size of L1
```

- **L0** SSTables come directly from flushes, so their key ranges can **overlap**
  — a read must check *every* L0 file. RocksDB triggers compaction once L0 has a
  few files.
- **L1 and below** are kept as **non-overlapping** sorted runs: within a level,
  the key ranges are disjoint, so a read checks **at most one SSTable per level**.
- Each level is ~**10×** the size of the one above (`max_bytes_for_level_multiplier`).
  With a 10× fan-out, even terabytes of data fit in ~6–7 levels, so a point read
  touches only a handful of files.

### 3.4 Compaction — the engine of the LSM (from Bigtable)

Compaction is the background process that merges SSTables. Without it, L0 would
fill with overlapping files and reads would slow to a crawl, and dead/overwritten
keys would never be reclaimed. Bigtable names three flavours, which generalize
into RocksDB's compaction strategies:

| Bigtable term | What it does | RocksDB analogue |
|---|---|---|
| **minor compaction** | flush full MemTable → a new SSTable | MemTable flush → L0 |
| **merging compaction** | merge a few SSTables + MemTable → bound the file count | **leveled** / universal compaction |
| **major compaction** | rewrite all SSTables into one, **dropping tombstones** | full / bottommost compaction |

RocksDB's **default leveled compaction**: pick an SSTable in level *k*, find the
overlapping SSTables in level *k+1*, merge-sort them, and write fresh
non-overlapping SSTables into *k+1* (deleting the inputs). Each piece of data thus
migrates downward, rewritten at each level — which is the source of **write
amplification** (§5). The alternative, **universal/tiered compaction**, merges
similarly-sized runs and rewrites data fewer times (lower write amp) at the cost
of more overlapping files (higher read & space amp). **Choosing a compaction
strategy means choosing which amplification trade-off you are willing to make.**

### 3.5 Bloom filters — skipping SSTables on reads (from Bigtable)

A point read may have to probe several SSTables across the levels. Reading a
64 KB block from each, only to find the key isn't there, is wasteful. A **Bloom
filter** — a small probabilistic bitmap stored with each SSTable — answers *"could
this SSTable contain key K?"* with either **"definitely not"** or **"maybe."**

```
   get("user:4217")
     MemTable?            no
     L0 sst #1  Bloom →   "definitely not"  → skip the file (no disk read)
     L0 sst #2  Bloom →   "maybe"           → read its block, check
     L1 sst     Bloom →   "definitely not"  → skip
     L2 sst     Bloom →   "maybe"           → read its block → FOUND (newest wins)
```

Bigtable states the payoff directly: Bloom filters "drastically reduce the number
of disk seeks required for read operations… lookups for non-existent rows or
columns mostly do not need to touch disk." A false positive only costs one
unnecessary block read; a true negative saves a whole seek. This is the single
most important read-path optimization in an LSM, and it is *why* an
otherwise read-amplifying design can still serve point lookups acceptably.

### 3.6 Memory management & caching (grounded in Lab 3)

RocksDB holds two kinds of memory, mirroring Bigtable's two caches:

- **MemTable(s)** — the write buffer (§3.1).
- **Block Cache** — an **LRU** cache of *uncompressed SSTable blocks* read from
  disk (Bigtable's "Block Cache… caches SSTable blocks read from GFS"). This is
  exactly the eviction problem from my **Lab 3**. RocksDB's default block cache is
  LRU, and it *also* ships a **clock-based** cache — which is the very algorithm I
  implemented in Lab 3 (a circular hand giving pages a second chance). So my
  clock-sweep lab is a real, shippable RocksDB cache policy, not just a toy.

### 3.7 Durability & recovery — WAL

Durability works the same way as in the other engines I studied: every write is
appended to the **Write-Ahead Log** *before* it is acknowledged, so a crash
cannot lose an acknowledged write even though the MemTable lives only in memory.
On restart, RocksDB **replays the WAL** to rebuild the MemTable, exactly as
Bigtable recovers a tablet by replaying its commit log's redo records. Once a
MemTable is safely flushed to an SSTable, the WAL segment that fed it can be
discarded. (The WAL/redo idea is the same one I traced through ARIES in my
PostgreSQL and InnoDB submissions — durability is universal; only the data
structure being protected changes.)

---

## 4. Design Trade-Offs

**Advantages**
- **Write-optimized.** Random writes become sequential MemTable flushes — ideal
  for ingest-heavy, append-heavy, and SSD workloads.
- **Excellent compression.** Immutable, sorted SSTables compress well block-by-
  block (similar keys sit together), so LSM stores are often far smaller on disk
  than a B-tree of the same data.
- **No in-place mutation** → simpler concurrency (immutable files need no read
  locks) and no torn-page problem on the data files.
- **Tunable.** The compaction strategy lets you dial the read/write/space balance
  per workload.

**Limitations**
- **Read amplification.** A point read may probe the MemTable + several SSTables;
  Bloom filters mitigate this but don't eliminate it. A *range* scan must merge
  across all levels.
- **Write amplification from compaction.** Data is rewritten as it migrates down
  the levels — background I/O that competes with foreground work.
- **Space amplification.** Dead/overwritten versions and tombstones occupy space
  until compaction reclaims them.
- **Compaction is operationally tricky** — poorly tuned compaction causes write
  stalls (if L0 fills faster than it drains) and latency spikes.

**The fundamental trade-off — the RUM conjecture**

> The **RUM conjecture** (Athanassoulis et al.) says you can optimize at most two of
> **R**ead, **U**pdate, and **M**emory (space) — making one better usually makes
> another worse. An LSM trades away **read** and **space** performance to win on
> **write** performance. A B-tree makes the opposite trade. There is no free option —
> only "which amplification can your workload live with?"

---

## 5. Experiments / Observations

> **These figures are derived from the RocksDB/LevelDB documentation and the LSM
> literature (cited in §References), plus my own analysis using Lab 6 — not
> measurements I took.** I describe the `db_bench` experiment the assignment
> recommends and reason about its expected results.

### 5.1 The three amplifications, defined

| Amplification | Definition | Driven by |
|---|---|---|
| **Write (WA)** | bytes written to disk ÷ bytes of user data | compaction rewriting data down the levels |
| **Read (RA)** | SSTables/blocks read ÷ 1, per point lookup | number of levels/files a key might be in |
| **Space (SA)** | bytes on disk ÷ bytes of live data | dead versions + tombstones awaiting compaction |

### 5.2 A worked write-amplification analysis (leveled compaction)

This is my own derivation, not a measurement. In leveled compaction, moving data
from level *k* to *k+1* merge-rewrites it against the overlapping data already in
*k+1*. With a level multiplier **T = 10**, a byte is rewritten on the order of
**T** times to fully populate each level, across **L** levels:

```
   WA  ≈  T × L          (rough upper bound for leveled compaction)
   e.g. T = 10, L = 4  →  WA on the order of 10–40×
```

The RocksDB docs and the LSM literature commonly cite leveled write
amplification in the **~10–30×** range — consistent with this rough estimate. **Universal/tiered** compaction rewrites data far fewer times, so its WA
is much lower (often single digits) — but it leaves more overlapping files, so
its **RA and SA are higher**. That is the RUM trade-off made numeric.

### 5.3 Why LSM beats a B-tree on writes — concrete, via Lab 6

Take a single key insert under a write-heavy stream:

- **B-tree (my Lab 6):** locate the target leaf and write *that* page back — a
  **random** write. If the leaf is full, `splitChild()` runs and writes **two
  more** pages (and updates the parent). So one logical insert ⇒ **1–3+ random
  page writes**, scattered across the file. Under heavy writes this is a storm of
  random I/O.
- **LSM (RocksDB):** the insert is an O(log n) MemTable insert in **memory** plus
  one tiny **sequential** WAL append. Thousands of such inserts accumulate and are
  flushed **together** as **one large sequential** SSTable write. The expensive
  rewriting (compaction) happens later, in the background, in big sequential
  passes.

So the LSM converts *many small random writes* (the B-tree's per-insert cost)
into *batched sequential writes + deferred background merging*. **That conversion
is the entire reason LSM-trees are write-optimized** — and seeing my own Lab 6
B-tree do 1–3 random writes per insert is what made it click.

### 5.4 The `db_bench` experiment I would run

The recommended exercise is to run RocksDB's `db_bench` and observe amplification
under different compaction strategies. The experiment I would set up:

```
# write-heavy ingest
db_bench --benchmarks=fillrandom --num=10000000 --compaction_style=level
db_bench --benchmarks=fillrandom --num=10000000 --compaction_style=universal
# then point and range reads on the resulting store
db_bench --benchmarks=readrandom,seekrandom --use_existing_db=1
```

**Expected outcome (hypothesis from the design):**

| Strategy | Write amp | Read amp | Space amp | Best for |
|---|---|---|---|---|
| **Leveled** (default) | **high** (~10–30×) | **low** | **low** (~1.1×) | read-heavy, space-constrained |
| **Universal/tiered** | **low** | **high** | **high** | write-heavy ingest |

I would also expect **Bloom filters on vs off** to make the biggest difference on
`readrandom` of **non-existent** keys — with filters, most such reads should
avoid disk entirely (per Bigtable), and turning them off should sharply increase
disk seeks. That single comparison would be the cleanest demonstration of §3.5.

### 5.5 Connection to my own benchmark

My Lab 2 benchmark measured B-tree engines (PostgreSQL, InnoDB, SQLite) on
**analytical reads**, where B-trees shine and an LSM's read amplification would be
a handicap. RocksDB targets the *opposite* corner — **write-heavy** workloads —
which my Lab 2 didn't stress. The honest cross-reading: Lab 2 shows the B-tree
world's strength (reads), and this LSM study shows the regime (writes) where that
same B-tree world struggles — the two halves of the RUM trade-off.

---

## 6. Key Learnings

1. **LSM = "turn random writes into sequential writes."** Strip away the
   terminology and that one sentence is the whole design. MemTable, SSTable,
   levels, and compaction are all machinery in service of batching writes and
   deferring the expensive rewriting to the background.

2. **My Lab 5 Red-Black tree is a MemTable.** The MemTable is just an ordered
   in-memory index; the balanced tree I already built satisfies every
   requirement. RocksDB prefers a skiplist *only* because it allows lock-free
   concurrent writes — which told me what a write-optimized engine values most.

3. **Immutability is the quiet superpower.** Because SSTables never change, reads
   need no locks, files cache and share freely, and compaction is just
   read-merge-write-delete. So much complexity in B-tree engines (page latching,
   torn pages) simply doesn't exist here.

4. **Bloom filters are what make LSM reads viable.** Reading the Bigtable paper
   made it concrete: a small bitmap lets a read *skip* SSTables that can't hold
   the key, turning "probe every level" into "probe one or two." Without them, the
   read-amplification cost of the design might be unacceptable.

5. **The RUM conjecture names the trade-off precisely.** You optimize at most two
   of read/update/memory. LSM buys write performance with read and space
   amplification; my Lab 6 B-tree makes the opposite trade. The compaction
   strategy is the dial between them.

6. **The same primitives keep reappearing.** A WAL for durability (as in ARIES,
   PostgreSQL, InnoDB), an LRU/clock block cache (my Lab 3), and a balanced
   ordered index (my Lab 5/6) — RocksDB recombines the exact building blocks from
   my other labs into a write-optimized shape. Database engines are remixes of a
   small set of ideas arranged for different workloads.

---

## References

- F. Chang, J. Dean, S. Ghemawat, et al., **"Bigtable: A Distributed Storage
  System for Structured Data"**, *OSDI* 2006 — memtable, immutable SSTables (64 KB
  blocks + block index), minor/merging/major compaction, Bloom filters, block
  cache. RocksDB's direct ancestor (Bigtable → LevelDB → RocksDB). *(course
  resource: `Resources/bigtable-osdi06.pdf`)*
- P. O'Neil, E. Cheng, D. Gawlick, E. O'Neil, **"The Log-Structured Merge-Tree
  (LSM-Tree)"**, *Acta Informatica*, 1996 — the original LSM design.
- M. Athanassoulis et al., **"Designing Access Methods: The RUM Conjecture"**,
  *EDBT* 2016 — the Read/Update/Memory amplification trade-off.
- Alex Petrov, ***Database Internals*** (O'Reilly, 2019) — LSM-trees, SSTables,
  compaction, Bloom filters (Part I). *(course resource:
  `Resources/Database Internals.pdf`)*
- **RocksDB Wiki** — *Leveled & Universal Compaction, MemTable, Block Cache,
  Bloom Filters, `db_bench`* — https://github.com/facebook/rocksdb/wiki
- My own lab work: **Lab 2** (B-tree engines on analytical reads — the opposite
  workload corner from RocksDB), **Lab 3** (clock/LRU cache = RocksDB's block
  cache), **Lab 5** (Red-Black tree = an in-memory MemTable), **Lab 6** (B-tree —
  the in-place baseline whose 1–3 random writes per insert show *why* LSM batches
  sequentially).