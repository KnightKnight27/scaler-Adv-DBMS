# RocksDB (Log-Structured Merge Tree)

- **Role Number:** 24BCS10345
- **Name:** Ansh Mahajan

RocksDB is an embeddable key-value store (a fork of Google's LevelDB, tuned by
Facebook for SSDs and high write throughput). Unlike the B-tree engines in the
other documents, RocksDB is built on a **Log-Structured Merge tree (LSM)**. The
core idea: **never do random writes to the main data structure** — buffer writes
in memory, flush them to disk as sorted immutable files, and merge those files
in the background. This turns random writes into sequential ones, at the cost of
making reads and background work harder.

## 1. Architecture and the write path

```
            WRITE(key, value)
                  │
        ┌─────────▼──────────┐      append (sequential)
        │  WAL (log file)     │◄──── crash recovery source
        └─────────┬──────────┘
                  │ insert
        ┌─────────▼──────────┐
        │  active MemTable    │   (sorted in-memory, e.g. skiplist)
        └─────────┬──────────┘
          full → becomes immutable, flushed
                  │
        ┌─────────▼──────────────────────────────────┐
        │ L0  SSTable  SSTable  SSTable   (may overlap)│
        │ L1  ─────── sorted runs, non-overlapping ────│
        │ L2  ──────────────── ~10x larger ────────────│
        │ ...                                          │
        └──────────────────────────────────────────────┘
```

- A write is appended to the **WAL** (for durability) and inserted into the
  in-memory **MemTable** (a sorted structure such as a skiplist). The write is
  done — no random disk I/O on the hot path.
- When the MemTable fills, it becomes **immutable** and is flushed to disk as a
  **Sorted String Table (SSTable)** at level **L0**.
- **Compaction** later merges SSTables into larger, non-overlapping sorted runs
  at deeper levels (L1, L2, … each ~10× the previous).

## 2. SSTable structure

An SSTable is an immutable file of key-sorted entries plus an index:

```
SSTable
 ├─ data blocks      (sorted key/value entries)
 ├─ index block      (first key of each data block → offset)
 ├─ bloom filter     (per-SSTable / per-block; "is key possibly here?")
 └─ footer           (offsets, magic)
```

Because entries are sorted, a lookup within one SSTable is a binary search over
the index plus one block read.

## 3. The read path (and why it's harder)

A key can exist in several places, so a read checks them **newest-first**:

```
GET(key):
  1. active MemTable      (in memory)
  2. immutable MemTables   (in memory)
  3. L0 SSTables           (newest first; L0 files can overlap)
  4. L1, L2, ... in order  (one file per level via the index)
  stop at the first match (or a tombstone)
```

A read may therefore touch many files. Two structures keep this affordable:

- **Bloom filters** — a probabilistic "key is *definitely not* in this SSTable"
  test. Most SSTables are skipped without a disk read; false positives only
  cost an occasional wasted lookup. This is the single most important read
  optimization in an LSM.
- **Block cache** — caches hot data/index blocks in memory.

## 4. Deletes: tombstones

You cannot delete from an immutable SSTable, so a delete writes a **tombstone**
— a marker that shadows older values for that key. The space is only actually
reclaimed when compaction merges the tombstone with the older versions and drops
them. (The same is true for overwritten values: old copies linger until
compaction.)

## 5. Compaction strategies

Compaction is the heart — and the cost — of an LSM:

- **Leveled compaction** (default): each level beyond L0 is a set of
  non-overlapping SSTables; merging keeps levels sorted. Low **space** and
  **read** amplification, higher **write** amplification.
- **Universal / tiered compaction**: merges similarly-sized runs. Lower write
  amplification, higher space and read amplification.

This is the explicit knob for trading the three amplifications against each
other.

## 6. The amplification triangle

LSM design is usually framed as a three-way trade-off, and RocksDB lets you pick
your corner via compaction config:

| Amplification | Meaning | LSM tendency |
| --- | --- | --- |
| **Write** | bytes written to disk per logical write | **High** — data is rewritten by compaction repeatedly |
| **Read** | files/blocks read per logical read | Moderate–high — many levels, mitigated by Bloom filters |
| **Space** | disk used vs live data | Moderate — tombstones + old versions until compacted |

Compare a B-tree (InnoDB/SQLite/PostgreSQL): **low read** amplification but
**random** writes and in-place updates. The headline contrast:

> **B-tree:** read-optimized, pays on random writes.
> **LSM:** write-optimized (sequential), pays on reads and background compaction.

## 7. Observations / takeaways

- The genius of the LSM is converting random writes into **sequential** writes +
  **deferred** sorting — ideal for write-heavy workloads and friendly to SSDs.
- **Bloom filters are what make LSM reads viable**; without them, a point lookup
  could read every level.
- Compaction is unavoidable background work: it is the price paid later for the
  cheap writes taken now, and it competes with foreground traffic for I/O.
- RocksDB is a *library*, like SQLite — embedded, no server — but optimized for
  a completely different (write-heavy KV) workload. It powers storage layers in
  many systems (MySQL's MyRocks, Kafka Streams, CockroachDB's earlier versions,
  etc.).

## References

- P. O'Neil et al., "The Log-Structured Merge-Tree (LSM-Tree)", *Acta
  Informatica*, 1996.
- *RocksDB Wiki* (github.com/facebook/rocksdb) — architecture, compaction,
  Bloom filters.
- M. Kleppmann, *Designing Data-Intensive Applications*, Ch. 3 (SSTables and
  LSM-Trees).
