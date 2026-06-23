# RocksDB — Why You'd Build a Database That Refuses to Update in Place

**Name:** Parth Sankhla
**Roll Number:** 24BCS10229

Every storage engine I'd looked at before this — SQLite, PostgreSQL, InnoDB — is built on a B-tree, and in the labs I built B-trees myself (a red-black tree in Lab 5, a proper B-tree in Lab 6). They all share an instinct: when data changes, find its place and update it more or less where it lives. RocksDB throws that instinct out. It is built on an **LSM-tree** (Log-Structured Merge-tree), and its whole philosophy is "never go back and modify old data on disk — just keep appending, and clean up later." Understanding *why* anyone would want that is the point of this write-up, and the contrast with my own B-tree labs is what made it land.

---

## 1. Problem Background

The motivation is a hardware fact: on both spinning disks and SSDs, **sequential writes are much cheaper than random writes**. A B-tree, by design, scatters small updates all over the disk — every insert might dirty a different page in a different place. For a write-heavy workload (logging, metrics, message queues, time series, indexes that change constantly) that random-write pattern becomes the bottleneck.

RocksDB — Facebook's fork of Google's LevelDB — was built to make writes cheap by turning them all into sequential appends. It's an embedded key-value store (a library, like SQLite, not a server), and it's often used *underneath* other systems as their storage engine. The trade it makes is the central thing to understand: it sacrifices some read and space efficiency to make writes fast.

---

## 2. Architecture Overview

The design splits cleanly into a write path and a read path, with compaction running in the background to keep both honest.

```
  WRITE PATH                          READ PATH (newest -> oldest)
  ----------                          ----------------------------
  put(k,v)                            get(k)
     |                                   |
     +--> WAL (append, durability)       v
     |                              MemTable        (RAM, newest)
     +--> MemTable (RAM, sorted)         |
              |  full?                    v
              v                     Immutable MemTable
        Immutable MemTable                |
              |  flush (sequential)        v
              v                          L0  ---- bloom filter per SSTable
        +----------- on disk -----------+ |        (skip files that can't
        | L0:  SSTable SSTable ...      | v         hold the key)
        | L1:  SSTable SSTable ...      |  L1
        | L2:  SSTable ...              |  ...
        | ...                          |  Ln       (oldest, largest)
        | Ln:  SSTable ...             |
        +-------------------------------+
                      ^
                      |  compaction merges SSTables
                      |  down the levels, dropping
                      |  overwritten/deleted keys
```

A write goes two places: appended to the **WAL** for durability, and inserted into the in-memory **MemTable** (a sorted structure, usually a skiplist). When the MemTable fills, it becomes immutable and gets flushed to disk as an **SSTable** (a Sorted String Table — an immutable, sorted file) at level L0. Background **compaction** then merges SSTables downward through levels L1...Ln, and as it merges it discards keys that have been overwritten or deleted. A read checks newest-to-oldest — MemTable, then immutable MemTable, then L0, then down the levels — using **bloom filters** to skip SSTables that can't possibly contain the key.

---

## 3. Internal Design

### Write path — append, don't seek

Because the MemTable is in memory and the WAL is an append, a write never has to seek to find and modify an existing record. Overwriting a key doesn't touch the old value at all; it just inserts a newer entry. Deleting a key inserts a special **tombstone** marker. The old data and the obsolete versions stay on disk until compaction gets around to dropping them. This is the inversion of everything my B-tree labs did, where an update modified the tree in place.

### SSTables — immutable, sorted, stacked

Every file on disk is immutable once written. That immutability is what makes the writes sequential and the files safe to read concurrently without locking, but it also means the truth about a key can be spread across several files at different levels, with the newest version winning.

### Compaction — the heart and the cost

Compaction is the background process that reads SSTables, merges them by key, keeps only the newest version of each key (and physically removes keys whose tombstones it passes), and writes fresh SSTables lower down. Without it, reads would slow to a crawl (too many files to check) and space would never be reclaimed. With it, you pay for rewriting data multiple times as it migrates down the levels. The two common strategies trade differently:
- **Leveled compaction** keeps each level non-overlapping and tightly sorted — better reads and less wasted space, but more write amplification.
- **Universal (tiered) compaction** merges less aggressively — lower write amplification, but more files to check on reads and more space used.

### Bloom filters — rescuing the read path

Reads are LSM's weak spot because a key might be in any level. A **bloom filter** is a small probabilistic structure per SSTable that answers "is this key *definitely not* here?" cheaply. If the filter says no, RocksDB skips that file entirely without reading it. False positives are possible (it may occasionally say "maybe" when the key is absent), but false negatives never are, so it's safe. Without bloom filters, the multi-level read path would be painfully slow.

### The three amplifications

The vocabulary that finally made LSM trade-offs precise for me:
- **Write amplification** — bytes actually written to disk ÷ bytes the user wrote. Compaction rewrites data repeatedly, so this is > 1.
- **Read amplification** — how many places a read might have to check (MemTable + several SSTables across levels).
- **Space amplification** — extra disk used by obsolete versions and tombstones not yet compacted away.

You can't minimize all three at once; tuning compaction is choosing which one to sacrifice.

---

## 4. Design Trade-Offs

The cleanest way I can frame it is **B-tree vs LSM**, since I'd built the B-tree side myself:

| | B-tree (Lab 5/6, InnoDB, Postgres) | LSM-tree (RocksDB) |
|---|---|---|
| Writes | update in place, often random | append-only, sequential |
| Write speed | slower under heavy churn | fast — the whole point |
| Reads | one path down the tree, predictable | may check many levels (bloom filters help) |
| Space | fairly tight | obsolete versions linger until compaction |
| Background work | little | constant compaction |

So LSM is a deliberate bet: take on **read amplification, space amplification, and a permanent compaction workload** in exchange for **cheap, sequential writes**. That bet pays off enormously for write-heavy workloads and loses to a B-tree for read-heavy, update-light ones. Compaction is the recurring theme — it's simultaneously what makes the design viable and the price you pay forever, and tuning it (leveled vs universal, how aggressive) is most of the operational work.

---

## 5. Experiments / Observations

I don't have my own benchmark numbers for RocksDB the way I have Lab 2 data for Postgres, so rather than quote figures I didn't measure, I'll describe the experiment that exposes the trade-offs and reason about the *shape* of the result.

**Experiment — `db_bench` under different compaction strategies.** RocksDB ships with `db_bench`, which can run write-heavy (`fillrandom`) and read-heavy (`readrandom`) workloads and report the amplification factors:

```sh
# write-heavy, leveled compaction
db_bench --benchmarks=fillrandom --num=10000000 --compaction_style=0 --statistics

# same workload, universal compaction
db_bench --benchmarks=fillrandom --num=10000000 --compaction_style=1 --statistics
```

What I'd expect to observe, and why:
- **Leveled** compaction should show **higher write amplification** (data is rewritten more often to keep levels non-overlapping) but **lower space amplification** and better read latency.
- **Universal** compaction should show **lower write amplification** but **higher space and read amplification** (more overlapping files to sift through).
- Running `fillrandom` (writes) then `readrandom` (reads) makes the asymmetry obvious: the write phase is fast and steady because everything is an append, while read latency depends heavily on how many levels and SSTables a key search has to touch — and disabling bloom filters should visibly hurt that read phase.

The honest caveat is that these are reasoned expectations from the architecture plus RocksDB's documented behavior, not measurements I took myself; the value of the experiment is that each knob (compaction style, bloom filters) maps to one of the three amplifications in a way you can actually watch move.

---

## 6. Key Learnings

- **Sequential-vs-random writes is the whole motivation.** LSM exists because random writes are expensive and appends are cheap. Once that clicked, every other piece (immutable SSTables, compaction, tombstones) made sense as a way to keep writes sequential.
- **It's the exact inverse of my B-tree labs.** My trees updated data in place; RocksDB refuses to, and pushes all the "cleanup" into background compaction. Seeing the opposite philosophy taught me the B-tree's assumptions better than building it did.
- **Compaction is both the engine and the tax.** Nothing about LSM works without it, and it's the source of write amplification and most of the tuning. There's no version of LSM that skips this cost.
- **The three amplifications are the real language.** Write/read/space amplification turned vague "LSM is write-optimized" hand-waving into a precise statement: you trade read and space amplification for low write amplification.
- **Bloom filters are what make reads survivable.** A tiny probabilistic "definitely-not-here" check is the difference between a usable read path and an unusable one across many levels.
