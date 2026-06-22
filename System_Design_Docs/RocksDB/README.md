# RocksDB Architecture (LSM-Tree Storage)

**Author:** Shubham Shah · **Roll No:** 24BCS10316 · **Course:** Advanced DBMS, Scaler School of Technology
**Topic 4: RocksDB** MemTable · SSTables · WAL · L0→Ln levels · Bloom Filters · Compaction · Read/Write paths

---

## 1. Problem Background

The B-tree storage engines studied so far (PostgreSQL, InnoDB) **update data in
place**. On a write they must find the right page, possibly read it first, modify
it, and eventually write it back, producing **random I/O**. On spinning disks
that meant slow seeks; even on SSDs, in-place updates cause **read-modify-write**
at the flash-block level and accelerate wear.

Modern write-heavy systems (time-series, logging, messaging, metrics, KV stores
behind web apps) generate huge streams of small writes where this random-write
cost dominates. The **Log-Structured Merge tree (LSM-tree)**, introduced by
O'Neil et al. (1996) and popularized by Google's **LevelDB**, answers this by
**never updating in place**: all writes are buffered in memory and flushed
**sequentially**, and the structure is reorganized later in the background.

**RocksDB** (Facebook, 2012) is a fork of LevelDB hardened for production: built
as an **embeddable C++ key-value library** (like SQLite, it runs *inside* the
application, not as a server) and tuned for SSDs and large datasets with
multi-threaded compaction, column families, and rich tunables. It is the storage
engine under MySQL's MyRocks, CockroachDB, TiKV, Kafka Streams, and many others.

**The core problem RocksDB optimizes: make writes cheap and sequential, and pay
the reorganization cost later, in the background.**

---

## 2. Architecture Overview

RocksDB is an **LSM-tree**: a small mutable in-memory layer in front of an
**immutable, sorted, multi-level on-disk** structure. Writes go to memory + a log;
data ages downward through levels; background **compaction** keeps it sorted and
bounded.

```
   WRITE                                            READ (newest → oldest)
     │  (1) append                                    │
     ▼                                                ▼  check in order:
  ┌──────────┐   (2) insert     ┌──────────────┐    1. MemTable (RAM)
  │   WAL    │◄────────────────►│   MemTable    │    2. Immutable MemTables
  │ (on disk)│                  │  (RAM, sorted │    3. SSTables L0  (Bloom-gated)
  └──────────┘                  │   skiplist)   │    4. SSTables L1
                                └──────┬────────┘       ...
                            full → becomes immutable    n. SSTables Ln
                                       │ flush (sequential write)
                                       ▼
   ─────────────────────────── ON DISK (SSTables) ───────────────────────────
   L0 :  [SST] [SST] [SST]      ← overlapping key ranges (flushed as-is)
   L1 :  [SST][SST][SST][SST]   ← sorted, NON-overlapping; ~10× L0 size
   L2 :  [.....10× larger.....]
   ...
   Ln :  [..............largest, oldest data..............]

   each SSTable = sorted key/value blocks + index block + Bloom filter
```

**Components.**
- **MemTable**: the active in-memory write buffer, a **sorted skiplist**;
  absorbs all incoming writes.
- **WAL (Write-Ahead Log)**: every write is appended here first so an unflushed
  MemTable survives a crash.
- **Immutable MemTable**: a full MemTable, frozen, awaiting flush.
- **SSTable (Sorted String Table)**: an immutable on-disk file of sorted KV
  pairs, with a block index and a **Bloom filter**.
- **Levels L0…Ln**: L0 holds freshly flushed SSTables (key ranges may overlap);
  L1+ are kept **sorted and non-overlapping**, each level ~10× larger than the one
  above.

---

## 3. Internal Design

### 3.1 Write Path

```
   Put(key, value):
     1. append (key,value) to WAL          ── durability (sequential disk write)
     2. insert into active MemTable        ── sorted skiplist in RAM
     3. return success                     ── that's it: no random I/O, no seeks
```

That is the whole foreground write. No page to locate, no read-before-write, no
in-place mutation, just one sequential log append plus an in-memory insert. This
is why LSM writes are so cheap.

When the MemTable reaches its size limit (`write_buffer_size`):
- it is **frozen** into an *immutable* MemTable, a fresh MemTable takes over, and
- a background thread **flushes** the immutable MemTable to a new **L0 SSTable**
  in one **sequential** write. Its WAL can then be discarded.

**Deletes don't delete.** A delete writes a **tombstone** marker; an update writes
a new version with a newer **sequence number**. Nothing on disk is modified.
Obsolete versions and tombstones are physically removed only later, during
compaction. Every record carries a monotonic **sequence number** that defines
recency and also powers snapshots/MVCC.

### 3.2 SSTable Structure & Bloom Filters

Each SSTable is immutable and internally sorted:

```
   SSTable file:
     ┌─────────────── data blocks (sorted KV, ~4–16 KB each) ───────────────┐
     ├─ index block      (first key of each data block → block offset)       │
     ├─ Bloom filter block  (probabilistic "is key X possibly here?")        │
     └─ footer (metadata)                                                     │
```

A **Bloom filter** is a compact bit array that answers *"is this key in this
SSTable?"* with **"definitely no"** or **"maybe yes"** (never a false negative).
Why it matters: without it, a point lookup that misses would have to read the
index/data blocks of *every* candidate SSTable across many levels. The Bloom
filter lets a read **skip an SSTable entirely** when the key certainly isn't there,
eliminating the dominant cost of LSM reads, disk I/O on files that don't contain
the key. A ~10-bits-per-key filter gives ~1% false-positive rate.

### 3.3 Read Path

A read must find the **newest** version of a key, so it searches layers
newest-first and stops at the first hit:

```
   Get(key):
     1. active MemTable        ── RAM
     2. immutable MemTables    ── RAM
     3. L0 SSTables            ── newest-first; ranges overlap → may check several
     4. L1, L2, … Ln           ── each level sorted & non-overlapping →
                                  binary-search to the ONE SSTable that can hold key
        at each SSTable: consult Bloom filter → skip if "definitely no"
                         else read index block → read data block
     return first version found (could be a tombstone → "not found")
```

The asymmetry with the write path is the LSM's defining trade-off: **one write
touches one place; one read may have to consult many places.** Bloom filters and
compaction exist to keep that read cost bounded.

### 3.4 Compaction and the L0/Ln level design

Because data is never updated in place, the same key can have versions scattered
across MemTables and many SSTables. **Compaction** is the background process that
merges overlapping SSTables, **keeps only the newest version of each key, drops
tombstoned/obsolete records, and pushes data to deeper levels**.

- **Why levels grow ~10×.** Each level Lₙ is non-overlapping and ~10× the size of
  Lₙ₋₁. To look up a key in L1+, RocksDB binary-searches to the *single* SSTable
  whose range covers it, so deeper levels add only O(log) work, not a linear scan.
- **Leveled compaction (default).** Picks an SSTable in Lₙ and merges it with the
  overlapping SSTables in Lₙ₊₁, rewriting Lₙ₊₁. This keeps each level tidy and
  **minimizes space and read amplification**, at the cost of more **write
  amplification** (data is rewritten each time it descends a level).
- **Universal / tiered compaction.** Merges similarly-sized SSTables together,
  rewriting data fewer times → **lower write amplification** but **higher space
  and read amplification** (more overlapping files to check). A direct knob on the
  trade-off triangle below.
- **L0 is special.** SSTables flushed from MemTables land in L0 with **overlapping
  key ranges** (they're just frozen MemTables), so a read may have to check all of
  them. That's why too many L0 files trigger compaction and can stall writes.

### 3.5 Durability & Recovery (WAL)

- Each write is appended to the **WAL** before/with the MemTable insert. The sync
  policy is tunable: fsync-per-write (full durability, slower) vs buffered
  (faster, risks losing the last few writes on power loss).
- **Crash recovery:** on restart RocksDB **replays the WAL** to rebuild the
  MemTable contents that hadn't been flushed to SSTables. Already-flushed data is
  safe in immutable SSTables. Once a MemTable is flushed, its WAL is obsolete.
- SSTables being immutable makes recovery simple: there are no half-updated pages
  to repair (no torn-page problem, no UNDO pass), only the tail of the log to
  replay.

### 3.6 The Three Amplifications

LSM performance is understood through three competing costs:

| Amplification | Meaning | Pushed up by |
|---------------|---------|--------------|
| **Write** | bytes written to disk ÷ bytes of user data | leveled compaction (rewrites on each descent) |
| **Read**  | SSTables/blocks consulted per lookup | many L0 files, tiered compaction, no Bloom filter |
| **Space** | disk used ÷ live data size | tiered compaction, uncompacted obsolete versions/tombstones |

**You cannot minimize all three at once**. This is the central LSM tension.
Leveled compaction trades write amplification to cut read+space; tiered does the
reverse. RocksDB exposes the knobs to choose per workload.

---

## 4. Design Trade-Offs

| Decision | Advantage | Cost / Limitation |
|----------|-----------|-------------------|
| **Out-of-place, log-structured writes** | Writes are sequential & fast; SSD-friendly; high write throughput | Reads must merge across layers; needs compaction to stay efficient |
| **Immutable SSTables** | Lock-free reads; trivial caching/replication; simple, torn-page-free recovery | Updates/deletes leave garbage until compaction (tombstones, dead versions) |
| **Leveled compaction (default)** | Low read & space amplification; predictable lookups | High **write amplification**; compaction consumes CPU/IO bandwidth |
| **Tiered/universal compaction** | Low write amplification | Higher read & space amplification |
| **Bloom filters** | Skip SSTables that can't hold the key → cheap negative lookups | Extra RAM (~10 bits/key); help point lookups, **not range scans** |
| **Embedded library (no server)** | Zero IPC, low latency, embeddable anywhere | No built-in multi-user networking/SQL; it's a KV engine, the app adds the rest |

**LSM-tree vs B-tree, the architecture-level comparison:**

| | B-tree (InnoDB/Postgres) | LSM-tree (RocksDB) |
|---|---|---|
| Writes | in-place → random I/O | append/flush → sequential I/O |
| Write amplification | lower per write | higher (compaction rewrites) |
| Reads | one tree traversal | merge across layers (Bloom-assisted) |
| Read latency | predictable | variable (depends on level depth / L0 count) |
| Space | fragmentation/bloat | tombstones + obsolete versions until compaction |
| Best for | read-heavy / mixed, range-scan-heavy | **write-heavy**, high-ingest workloads |

---

## 5. Experiments / Observations

**Recommended exercise: `db_bench` under different compaction strategies.**
RocksDB ships a benchmark tool, `db_bench`. A representative run:

```bash
# write-heavy fill, then measure
./db_bench --benchmarks=fillrandom,readrandom \
           --num=10000000 --value_size=100 \
           --compaction_style=0          # 0 = leveled (default)

# repeat with universal/tiered compaction
./db_bench --benchmarks=fillrandom,readrandom \
           --num=10000000 --value_size=100 \
           --compaction_style=1          # 1 = universal (tiered)
```

What to observe and connect to §3:

- **Write amplification**: compare bytes written to disk vs user bytes (RocksDB
  reports `compact.write` stats / `rocksdb.stats`). Expect **leveled > universal**.
- **Read amplification**: average SSTables touched per `readrandom`. Expect
  **universal > leveled**, especially as L0/overlap grows.
- **Space amplification**: on-disk size vs live data. Expect **universal >
  leveled**; force a full compaction and watch disk usage drop as tombstones and
  dead versions are purged.
- **Bloom filter effect**: run `readmissing` (lookups for absent keys) with and
  without `--bloom_bits=10`. With the filter, missing-key lookups do far less I/O,
  direct proof of §3.2.
- **Write stalls**: push ingest hard and watch L0 file count; when it exceeds the
  trigger, RocksDB throttles/stalls writes until compaction catches up, the
  background cost made visible.

Inspecting internals at runtime: `GetProperty("rocksdb.stats")`,
`rocksdb.num-files-at-level<N>`, and the `LOG` file show per-level file counts and
compaction activity directly.

---

## 6. Key Learnings

- **LSM = "defer the work."** The foreground write does almost nothing (log +
  in-memory insert); the real organizing work (sorting, merging, deduplicating) is
  deferred to background **compaction**. That deferral is exactly why writes are
  fast, and why compaction CPU/IO is the price.
- **Compaction is the heartbeat.** It reclaims space, bounds read cost, and keeps
  levels sorted. Tune it wrong and you get write stalls (too slow) or wasted
  bandwidth (too aggressive). It can become expensive because it **rewrites data
  every time it moves down a level**.
- **The three amplifications can't all win.** Write vs read vs space is a triangle;
  leveled and tiered compaction are just two corners. Choosing a compaction style
  *is* choosing which cost to pay.
- **Bloom filters convert "search everywhere" into "skip almost everything."**
  They're what make point lookups on a multi-level LSM practical, but they do
  nothing for range scans, a real limitation.
- **Surprising takeaway:** RocksDB **never modifies data in place**: even a
  delete is an *insert* (of a tombstone). The entire engine is "append + merge
  later," the mirror image of the B-tree's "find + overwrite." It made vivid that
  a storage engine's whole personality follows from one decision: *update in
  place, or never*.
