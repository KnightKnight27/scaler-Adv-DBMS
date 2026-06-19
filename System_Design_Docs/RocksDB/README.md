# RocksDB Architecture — LSM-Tree Storage

**Piyush Bansal — 24BCS10079**

---

## 1. Problem Background

PostgreSQL and InnoDB both use **B-Trees**, which are great for reads but do
**random writes** to disk (update a page wherever the key lives). On write-heavy
workloads — logging, time-series, message queues — random writes are slow.

RocksDB solves this with a completely different structure: the **LSM-Tree**
(Log-Structured Merge Tree). The core idea:
> **Never do random writes. Buffer writes in memory, then flush them to disk in
> big sequential batches.**

This makes RocksDB extremely fast at writes, at the cost of more work on reads and
background cleanup. It's a key-value store used inside many systems (Kafka Streams,
CockroachDB, MyRocks, etc.).

---

## 2. Architecture Overview

```
   WRITE path                          READ path
   ──────────                          ─────────
   write ─► WAL (durability)           look in:
        └─► MemTable (in RAM)            1. MemTable        (newest)
                │ full                   2. Immutable MemTable
                ▼                        3. SSTables L0..Ln  (older)
        Immutable MemTable                  (Bloom filter skips files
                │ flush                       that can't have the key)
                ▼
        ┌──────────────────────────────┐
        │  L0  SSTables                 │
        │  L1  SSTables   (sorted)      │  ← compaction merges
        │  ...                          │     levels downward
        │  Ln  SSTables   (largest)     │
        └──────────────────────────────┘
```

- **WAL** — every write is logged first, for durability.
- **MemTable** — an in-memory sorted structure; writes go here (fast, no disk seek).
- **Immutable MemTable** — when the MemTable fills up, it's frozen and a new one
  takes over.
- **SSTable** (Sorted String Table) — the frozen MemTable is flushed to disk as a
  sorted, immutable file.
- **Levels L0…Ln** — SSTables are organized in levels; **compaction** merges them
  downward over time.

---

## 3. Internal Design

### 3.1 Write path (why writes are so fast)

1. Append the write to the **WAL** (sequential = cheap, gives durability).
2. Insert into the **MemTable** in RAM (sorted, fast).
3. That's it — the write returns. No disk seek, no B-Tree page update.

When the MemTable is full, it becomes **immutable** and is flushed to disk as a new
**SSTable** in level L0. The flush is one big sequential write.

Because writes never modify existing files (SSTables are immutable), there are
**no random writes** — this is the whole point of LSM-trees.

### 3.2 Read path (why reads are harder)

A key might be in the MemTable, or in any SSTable, at any level. So a read may have
to check multiple places, newest first, until it finds the key:

```
MemTable → Immutable MemTable → L0 SSTables → L1 → ... → Ln
```

Two things make this fast:
- **Bloom filters** — each SSTable has a Bloom filter, a tiny probabilistic
  structure that answers "is this key *definitely not* here?" If the filter says no,
  RocksDB **skips reading that SSTable entirely.** This avoids most useless disk reads.
- **SSTables are sorted** — within a file, the key is found by binary search, and an
  index block points to the right block.

### 3.3 Compaction (the background cleanup)

Over time you accumulate many overlapping SSTables, and the same key may have several
versions (plus deletions, stored as "tombstones"). **Compaction** is a background job
that merges SSTables, keeps the newest version of each key, drops tombstoned keys, and
writes fresh sorted SSTables into the next level down.

Compaction is *required* because:
- it stops the number of SSTables (and read cost) from growing forever, and
- it reclaims space from deleted/overwritten keys.

But compaction itself reads and rewrites data → this is **write amplification** (the
same data gets rewritten several times as it moves down the levels).

---

## 4. Design Trade-Offs

LSM-trees are usually discussed in terms of three "amplifications":

| Amplification | What it means | LSM-tree behavior |
|---------------|---------------|-------------------|
| **Write** | Bytes written to disk ÷ bytes the user wrote | **High** — compaction rewrites data repeatedly |
| **Read** | Disk reads needed per lookup | **Higher than B-Tree** — may check many SSTables (Bloom filters reduce this) |
| **Space** | Disk used ÷ live data | Some waste from old versions until compaction runs |

**Trade-off summary:**
- ✅ Excellent **write throughput** (sequential writes, no random I/O).
- ✅ Good space efficiency after compaction.
- ❌ Reads can be slower than a B-Tree (mitigated by Bloom filters).
- ❌ Compaction consumes background CPU/IO and causes write amplification.

**B-Tree vs LSM-Tree (the headline):**
- B-Tree → read-optimized, random writes. Good for read-heavy / mixed workloads.
- LSM-Tree → write-optimized, sequential writes. Good for write-heavy workloads.

---

## 5. Experiments / Observations

RocksDB ships with `db_bench`, its benchmark tool:

```bash
# write-heavy load
./db_bench --benchmarks=fillrandom --num=1000000

# then read
./db_bench --benchmarks=readrandom --num=1000000 --use_existing_db=1
```

What to observe under different compaction settings:
- **Write amplification** — total bytes written to disk vs bytes inserted; goes *up*
  with more aggressive (leveled) compaction.
- **Read amplification** — leveled compaction keeps fewer overlapping files → faster
  reads; universal compaction does less work but reads get slower.
- **Space amplification** — how much extra disk is used by un-compacted old versions.

The key observation: you **cannot minimize all three at once** — tuning compaction is
choosing which amplification to trade away (the "RUM conjecture": Read, Update, Memory
— pick your trade-off).

---

## 6. Key Learnings

- **Why LSM-trees are preferred for write-heavy workloads:** writes are buffered in
  memory and flushed sequentially, avoiding the random disk writes that slow down
  B-Trees.
- **Why compaction is expensive:** it constantly re-reads and re-writes SSTables to
  keep reads fast and reclaim space — that's the write-amplification cost.
- **Why Bloom filters matter:** without them, a read might touch every SSTable; the
  filter lets RocksDB skip files that definitely don't contain the key, turning a
  potentially slow read into a fast one.
- The big lesson: B-Tree and LSM-Tree are **opposite trade-offs** of the same goal —
  one optimizes reads, the other optimizes writes. The right choice depends entirely
  on the workload.

---

### References
- RocksDB documentation / wiki — *LSM-tree, Compaction, Bloom filters, db_bench*
- Compared against B-Tree storage from my PostgreSQL and InnoDB write-ups
