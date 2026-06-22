# Benchmark Report — LSM-tree vs B+ Tree Storage (Extension Track C)

## 1. Goal

The extension track (Track C) replaces/compares heap+B+Tree storage with an
**LSM-tree** design. This report measures the three quantities the brief asks
for, for an identical workload on both engines:

- **Write throughput** — how fast can we insert keys?
- **Read latency** — how long does a point lookup take?
- **Space & read amplification** — the LSM-specific costs of deferring work to
  compaction.

## 2. Experimental Setup

- **Code:** `src/bench.cpp` (build with `make bench`, run `./build/bench [N]`).
- **Workload:** `N = 200,000` integer keys, inserted in a **fixed shuffled
  order** (seed = 42) so both engines see exactly the same sequence. Values are
  16-byte strings. Reads are 50,000 random point lookups.
- **Engines compared:**
  - `BPlusTree` (our `index/bplus_tree`), order 64, in memory.
  - `LSMTree` (our `lsm/lsm_tree`): memtable limit 10,000 entries; SSTables
    persisted to disk; compaction trigger every 4 SSTables.
- **Machine:** Apple M-series laptop, compiled with `clang++ -O2 -std=c++17`.
  (Absolute numbers vary by machine; the **ratios and trends** are the point.)
- **Methodology:** wall-clock timing with `std::chrono::high_resolution_clock`;
  each phase timed separately.

> Numbers below are from one representative run. Re-running gives values within a
> small margin; trends are stable.

## 3. Results

### 3.1 Write throughput
| Engine | Throughput | Time for 200k |
|--------|-----------:|--------------:|
| B+ Tree | ~3.2 M inserts/sec | 62 ms |
| LSM | ~1.9 M puts/sec | 103 ms |

### 3.2 Read latency (50k random point lookups)
| Engine | Latency / lookup |
|--------|-----------------:|
| B+ Tree | ~270 ns |
| LSM (after compaction, 1 SSTable) | ~135 ns |

### 3.3 Space amplification (each key written twice → 200k logical keys)
| State | Physical entries | SSTables | Amplification |
|-------|-----------------:|---------:|--------------:|
| Before compaction | 400,000 | 40 | ~2.0× |
| After compaction | 200,000 | 1 | ~1.0× |

### 3.4 Read amplification (lookup cost vs number of SSTables)
| State | Latency / lookup |
|-------|-----------------:|
| 40 SSTables | ~563 ns |
| 1 SSTable (after compaction) | ~86 ns |

## 4. Analysis

**Write throughput.** Both engines are in-RAM here, so both are fast. The LSM
pays a little for periodically serializing the memtable to SSTable files (disk
I/O during flushes), which is why its raw number trails the purely-in-memory B+
Tree in this micro-benchmark. **The LSM's real advantage shows on disk-bound,
write-heavy workloads**: it only ever does *sequential* writes (append a new
SSTable), whereas a disk-resident B+ Tree must do *random* writes to update
interior pages and split nodes. Our benchmark deliberately keeps the B+ Tree in
memory to give it the best case; even so the LSM stays in the same order of
magnitude.

**Read latency / read amplification.** This is the LSM's fundamental cost. A
lookup must check the memtable and then **every SSTable** until the key is found.
With 40 SSTables a lookup is ~6.5× slower (~563 ns) than with a single compacted
SSTable (~86 ns). The B+ Tree has no such penalty — one O(log n) descent always.
This is the classic **write-optimized vs read-optimized** trade-off: the LSM
defers work to keep writes cheap, and pays it back on reads until compaction runs.

**Space amplification.** Because SSTables are immutable, an updated key leaves its
old version behind in an older SSTable until compaction. After writing every key
twice we hold ~2× the logical data (400k physical entries for 200k keys).
Compaction merges everything, keeps only the newest value per key, drops
tombstones, and brings space back to ~1.0×.

**The role of compaction.** Compaction is the LSM's "garbage collector". It is the
single mechanism that controls **both** read amplification (fewer SSTables to
search) **and** space amplification (old versions reclaimed). The trade-off is
that compaction itself consumes CPU and I/O — so real systems tune *how often* and
*how aggressively* to compact (size-tiered vs leveled strategies).

## 5. Conclusion

| | B+ Tree | LSM-tree |
|---|---|---|
| Write pattern | in-place, random | append-only, sequential |
| Write throughput | high (in RAM) | high; **best on disk/write-heavy** |
| Point-read latency | consistently low | low when compacted, **degrades with #SSTables** |
| Space overhead | ~1× | up to N× until compaction (~1× after) |
| Best for | read-heavy, point/range queries | write-heavy / ingest-heavy workloads |

MiniDB's primary engine (heap + B+ Tree) is a sound default for mixed read/write
SQL workloads. The LSM engine is the better choice when writes dominate and reads
can tolerate compaction-managed amplification — which is exactly why log-structured
stores power write-heavy systems like time-series, logging, and KV databases.
