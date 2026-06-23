# RocksDB Architecture (LSM-Tree Storage)

> Advanced DBMS — System Design Discussion
> Topic 4: RocksDB

The first three topics were all B-tree style databases. RocksDB is different — it
uses an **LSM-tree (Log-Structured Merge tree)**. I chose to study it because it
made me realize that B-trees are not the only way to store data, and that a
totally different structure can be much better for **write-heavy** workloads.
RocksDB is a key-value store (made by Facebook, based on Google's LevelDB) and is
used inside many bigger systems.

---

## 1. Problem Background

B-tree databases update data more-or-less *in place*. On disk that means lots of
**random writes** (jump here, change a page; jump there, change another). Random
writes are slow, especially on spinning disks, and they wear out SSDs faster.

The LSM-tree idea is: **never do random writes for data. Only ever append.**
Collect changes in memory, then write them out to disk in big **sorted, immutable
chunks**. This turns random writes into fast **sequential writes**, which is great
when your workload is mostly writes (logging, metrics, messaging, etc.).

RocksDB is an embedded LSM key-value store built for exactly these **write-heavy**
workloads while still giving fast reads.

---

## 2. Architecture Overview

```
   WRITE path                                 READ path (newest -> oldest)
   ---------                                   --------------------------
   put(key,val)                                get(key)
      |                                            |
      v                                       1. check MemTable (RAM)
  +--------+   (also appended to)                  | not found
  |  WAL   |---------------------------+      2. check Immutable MemTables
  +--------+                           |           | not found
      |                                |      3. check SSTables L0, L1 ... Ln
      v                                |           ^ uses Bloom filters to skip files
  +-------------+   when full          |
  |  MemTable   | ------------------>  becomes Immutable MemTable
  |  (in RAM,   |                          |
  |   sorted)   |                          v  flush to disk
  +-------------+                     +-----------------+
                                      |   SSTable (L0)  |  sorted, immutable file
                                      +-----------------+
                                              |  compaction merges files down
                                              v
                                      L1 -> L2 -> ... -> Ln   (bigger, older levels)
```

Main components:
- **MemTable** — in-memory sorted structure where new writes go.
- **Immutable MemTable** — a full MemTable waiting to be written to disk.
- **WAL** — write-ahead log so data in the MemTable isn't lost on a crash.
- **SSTables** — Sorted String Tables: immutable sorted files on disk.
- **Levels L0..Ln** — SSTables are organized in levels; lower = newer/smaller.
- **Bloom filters** — quick "is this key probably here?" check per SSTable.
- **Compaction** — background merging of SSTables.

---

## 3. Internal Design

### 3.1 Write path

Writing is designed to be fast:

1. The key-value pair is **appended to the WAL** (on disk) so it's durable.
2. It's inserted into the **MemTable** in memory (kept sorted, often a skip list).
3. That's it — the write returns. No random disk writes to data files at all.

When the MemTable gets full:
4. It becomes an **Immutable MemTable** (read-only now), and a fresh MemTable
   takes new writes.
5. A background thread **flushes** the immutable MemTable to disk as a new
   **SSTable in level L0**. The SSTable is sorted and **never modified again**.

So every write is just: append to WAL + insert into RAM. Very cheap. This is why
LSM trees are fast for writes.

Note: a **delete** doesn't erase anything immediately — it writes a special marker
called a **tombstone**. The real removal happens later during compaction. Same for
updates: a new value is just written on top; the old one is removed later.

### 3.2 SSTables and levels

An **SSTable** is an immutable file of **sorted** key-value pairs, plus an index
and a Bloom filter. Because it's sorted, you can binary-search within it.

SSTables are arranged in **levels**:
- **L0** holds the freshly flushed files. L0 files can have **overlapping** key
  ranges (because each is just a dumped MemTable).
- **L1, L2, ... Ln**: from L1 down, the files within a level have
  **non-overlapping** key ranges, and each level is much bigger than the one
  above (often ~10×). Lower levels hold older data.

So the same key could exist in several places (MemTable, L0, L1, ...) with the
**newest version highest up**.

### 3.3 Read path

To read a key, RocksDB looks from **newest to oldest** and stops at the first hit:

1. Check the active **MemTable** (RAM).
2. Check the **Immutable MemTable(s)**.
3. Check **SSTables**, starting at L0, then L1, ... Ln.

Because a key might be in many SSTables, reads could be slow — this is where
**Bloom filters** help.

### 3.4 Bloom filters

A **Bloom filter** is a small, fast, memory structure that can answer *"is this
key definitely NOT in this SSTable, or maybe in it?"*

- If the Bloom filter says **"no"**, the key is *definitely not* in that file →
  skip reading it (saves a disk read). This is the common, useful case.
- If it says **"maybe"**, RocksDB actually checks the file. Bloom filters can have
  **false positives** (say "maybe" when it's not there) but **never false
  negatives**.

So Bloom filters let RocksDB skip most SSTables that don't have the key, which
makes reads much faster. Without them, a read might have to touch every level.

### 3.5 Compaction (and why it's needed)

Because data is only ever appended, over time you get **many SSTables**, lots of
**duplicate/old versions** of keys, and **tombstones** for deleted keys. This
wastes space and slows reads (more files to check).

**Compaction** is the background process that **merges SSTables together**: it
reads several SSTables, merges their sorted keys, **keeps only the newest version**
of each key, **drops tombstoned (deleted) keys**, and writes out new, clean
SSTables — usually pushing data down to the next level.

```
Before:  L0: [a1 b1]  [a2 c1]   (a appears twice, different versions)
              merge + keep newest + drop deletes
After:   L1: [a2 b1 c1]         (clean, sorted, non-overlapping)
```

Compaction is what keeps reads fast and reclaims space — but it costs CPU and disk
I/O, and it **re-writes data that was already written**, which causes *write
amplification* (see below).

### 3.6 Durability

RocksDB is durable through the **WAL**: every write is appended to the WAL before
(or as) it goes into the MemTable. If RocksDB crashes, the MemTable (which was only
in RAM) is lost, but on restart RocksDB **replays the WAL** to rebuild it. SSTables
are already safely on disk and immutable, so they're fine.

---

## 4. Design Trade-Offs

The classic LSM trade-off is described with three "amplifications":

- **Write amplification** = how many times data gets written to disk in total
  compared to the data the user wrote. LSM re-writes data during compaction
  (L0→L1→L2...), so one logical write can be physically written several times.
  **Higher** in LSM because of compaction.
- **Read amplification** = how many places you might have to look to find a key
  (MemTable + several SSTables). **Higher** than a B-tree (which is basically one
  tree), but Bloom filters reduce it a lot.
- **Space amplification** = how much extra disk space is used by old/duplicate
  versions and tombstones before compaction cleans them up.

**Advantages**
- **Excellent write throughput** — writes are sequential appends, no random I/O.
- Good **space efficiency** after compaction; compresses well because SSTables are
  sorted blocks.
- Great for **write-heavy** workloads (logs, time-series, queues, metrics).

**Limitations**
- **Reads can be slower** than a B-tree (must check several levels) — Bloom filters
  and caching help but it's still more complex.
- **Compaction is expensive** and runs in the background; it can cause I/O spikes
  and occasional latency hiccups.
- **Write amplification** is real and can wear SSDs / use I/O bandwidth.

**Compaction strategies trade these off:**
- **Leveled compaction** → lower space & read amplification, but **higher write**
  amplification.
- **Universal / size-tiered compaction** → lower write amplification, but **higher
  space & read** amplification.
You basically can't minimize all three at once — you pick which one hurts least
for your workload. That was the key insight for me.

**vs B-tree databases (Postgres/InnoDB):** B-trees update in place → great reads,
more random writes. LSM appends + compacts → great writes, more complex reads.
So LSM is the better choice when writes dominate.

---

## 5. Experiments / Observations

RocksDB ships with a benchmark tool called **`db_bench`**, which the assignment
suggested. The idea is to run different workloads and watch the amplifications.

```bash
# fill the DB with random writes
./db_bench --benchmarks=fillrandom --num=1000000 --statistics

# then read
./db_bench --benchmarks=readrandom --num=1000000 --use_existing_db=1 --statistics
```

What I looked for / observed (conceptually, from the stats output and the LOG file):
- **Writes were very fast** for `fillrandom` because they just hit the WAL +
  MemTable — confirming the "LSM is write-optimized" claim.
- In the RocksDB **LOG file** I could see **flush** events (MemTable → L0 SSTable)
  and **compaction** events (merging files down to L1, L2...). Watching these
  happen made the architecture feel real.
- The statistics report things like `rocksdb.bytes.written` vs the amount of data
  I actually inserted — the ratio is roughly the **write amplification**, and it
  was clearly **greater than 1** because of compaction.
- Turning on Bloom filters made `readrandom` faster because many SSTable reads got
  skipped (`rocksdb.bloom.filter.useful` counter goes up — those are reads that
  Bloom filters let us avoid).
- Comparing compaction styles (leveled vs universal) showed the trade-off:
  leveled used less disk space but reported more bytes written (write amp);
  universal wrote less but used more space.

(I kept the numbers small on my laptop, so these are about *seeing the behavior
and direction* rather than production benchmark figures.)

---

## 6. Key Learnings

- **LSM trees are write-optimized** because they turn random writes into
  sequential appends: write to WAL + an in-memory MemTable, then flush sorted
  immutable SSTables. No in-place random writes.
- **Data flows** one direction: MemTable (RAM) → Immutable MemTable → SSTable (L0)
  → compacted down to L1, L2, ... Ln. Reads go the opposite way, newest first.
- **Compaction is required** to merge files, throw away old versions and
  tombstones, keep reads fast, and reclaim space — but it's the main *cost* of
  LSM (CPU, I/O, write amplification). That's why it "becomes expensive."
- **Bloom filters improve reads** by letting RocksDB skip SSTables that
  *definitely* don't contain the key, so a read doesn't have to touch every level.
- The **three amplifications (write / read / space)** are a trade-off triangle —
  you can't win all three, and your choice of compaction strategy decides which
  one you sacrifice.
- Biggest lesson: the *data structure itself* (LSM vs B-tree) is an engineering
  trade-off. There's no universally best storage engine — B-trees favor reads,
  LSM trees favor writes, and you choose based on your workload.
