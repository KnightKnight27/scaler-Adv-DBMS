# RocksDB Architecture

## 1. Problem Background

RocksDB is a Facebook fork of Google's LevelDB, redesigned for SSDs and write-heavy server workloads. Its central idea — the **Log-Structured Merge tree (LSM)** — flips the B-tree trade-off: instead of updating pages in place (cheap reads, expensive random writes), an LSM turns every write into a sequential append and pushes the merging work to background compaction. This wins on SSDs, where sequential writes are far cheaper than random ones and write endurance matters.

RocksDB is used as the storage engine inside MySQL (MyRocks), CockroachDB, TiKV, Kafka Streams, and many others.

---

## 2. Architecture Overview

```
   PUT(k,v) ─▶ WAL append ─▶ MemTable (skiplist, in-memory)
                                   │ when full → flush
                                   ▼
                            Immutable MemTable
                                   │
                                   ▼ flush
                            ┌────────────────┐
                            │     L0  (SSTs) │  may overlap key ranges
                            ├────────────────┤
                            │     L1         │  non-overlapping
                            ├────────────────┤
                            │     L2  (~10×) │
                            ├────────────────┤
                            │     ...        │
                            └────────────────┘
   GET(k) ─▶ MemTable → Immutable → L0 → L1 → ... (Bloom filters skip levels)
```

---

## 3. Internal Design

### Write Path

1. Append the record to the **WAL** (sequential file write).
2. Insert into the active **MemTable** (a concurrent skiplist).
3. When the MemTable reaches a size threshold, it becomes **immutable** and a new one is created. Writes continue uninterrupted.
4. A background thread **flushes** the immutable MemTable to disk as a sorted L0 **SSTable**, then the WAL for it can be discarded.

The hot path is just an append + a skiplist insert — no random I/O, no page splits.

### SSTables (Sorted String Tables)

An immutable file containing key-value pairs sorted by key, organized into blocks:
- **Data blocks** — the actual sorted KV pairs (compressed).
- **Index block** — `(first key of block → block offset)`.
- **Bloom filter block** — per-SST, lets a GET skip the file if the key is definitely absent.
- **Footer** — pointers to the above.

### Levels

- **L0** is special: files come straight from flushes, so their key ranges may overlap. A GET may need to consult every L0 file.
- **L1 and below**: files within a level have **disjoint** key ranges. A GET checks at most one file per level (binary search on file boundaries).
- Each level is ~10× larger than the one above.

### Compaction

Compaction merges SSTables to keep L0 small and to push tombstones down so they eventually delete data and disappear.

- **Leveled compaction** (default): pick a file in Ln, find overlapping files in Ln+1, k-way merge them, write new files into Ln+1. Reads are cheap (one file per level); writes are amplified (each key rewritten ~once per level it descends through).
- **Universal compaction**: merge files of similar size in L0 only. Lower write amplification, but reads may have to consult many files and space amplification is higher.

### Bloom Filters

Per-SST probabilistic structure that answers "is key K definitely not in this file?" with high accuracy and tiny memory (~10 bits/key for ~1% false-positive rate). Without them, a GET in the worst case opens every SST. With them, most SSTs are skipped at constant cost.

### Read Path

For `GET(k)`:
1. Check MemTable and Immutable MemTable.
2. Walk levels L0 → Ln. For each candidate SST, consult its Bloom filter; if "maybe present", binary-search the index block, load the data block, look up the key.
3. Return the **newest** value (or absence — a tombstone counts).

### WAL and Durability

The WAL guarantees crash recovery: on restart, replay any WAL whose MemTable was not flushed. `WriteOptions::sync=true` does `fsync` per write (durable but slow); the default groups commits and fsyncs in batches.

---

## 4. Design Trade-Offs

LSM trees are defined by three amplifications:

| | Definition | LSM behavior |
|---|---|---|
| Write amp | Bytes written to disk per byte of user data | High — each key rewritten at each compaction |
| Read amp | Files / blocks read per logical key | Higher than B-tree without Bloom filters; close to B-tree with them |
| Space amp | Bytes on disk per byte of live data | Stale versions + tombstones until compaction reclaims them |

Compaction strategy is the knob:
- **Leveled** — low read amp, low space amp, high write amp.
- **Universal / Tiered** — low write amp, higher read and space amp.

Other trade-offs:
- **Sequential writes everywhere** → SSD-friendly, cheap commits, good for write-heavy logs / time-series / KV.
- **No in-place updates** → updates and deletes are just newer records (with a tombstone for delete). Until compaction reaches them, the old value still costs space.
- **Tombstone reads** can be slow on heavy delete patterns (range scans skip many dead keys until compaction catches up).
- **Compaction stalls**: if writes outrun the compactor, L0 grows, GETs slow down, and the writer is throttled.

---

## 5. Experiments / Observations

Using `db_bench` (RocksDB's built-in tool) on the same dataset:

| Workload | Leveled | Universal | Notes |
|---|---|---|---|
| `fillrandom` (write-heavy) | slower | **faster** | universal does less rewriting |
| `readrandom` (point GET) | **faster** | slower | leveled hits ≤1 SST per level |
| Final DB size | **smaller** | larger | universal keeps old versions longer |

Observations:
- Disabling Bloom filters on a read-heavy workload makes `readrandom` several times slower — the system has to open and binary-search far more SSTs.
- Forcing a manual `compact_range` shrinks the on-disk size sharply if the workload had many updates/deletes — confirming that space is held by stale versions until compaction.
- Watching `LOG`, you can see L0 filling up; if compaction can't keep up, write throughput drops as RocksDB applies write stalls.

---

## 6. Key Learnings

- LSM trees trade the B-tree's write cost for read and space cost, then claw back the read cost with sorted runs and Bloom filters. The shape of the LSM is essentially the shape of that trade-off.
- Compaction is not a background detail — it sets the write/read/space amplification numbers and therefore the cost model of the entire system.
- The same engine, with different compaction strategies, becomes a different database (write-heavy log store vs read-tuned KV).
- The reason RocksDB is used inside so many systems is not a single feature — it is a small, pluggable LSM that gives the system above it a sane place to put bytes.
