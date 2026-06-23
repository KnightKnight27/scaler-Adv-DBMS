# Benchmark Results

Run on the development machine (Apple Silicon, AppleClang 17, Release build,
local SSD with a warm OS page cache). Run via:

```
cmake --build build -j4
./build/benchmarks/bench_seqscan_vs_index
./build/benchmarks/bench_buffer_pool
./build/benchmarks/bench_replication_lag
```

## 1. IndexScan vs. SeqScan (optimizer's core decision)

`bench_seqscan_vs_index.cpp` inserts N rows into `bench(id INT PRIMARY KEY,
payload VARCHAR)` and times 200 point lookups two ways: an equality
predicate on `id` (PK -> IndexScan) vs. the logically equivalent lookup on
`payload`, which has no index and forces a full SeqScan.

| rows (N) | IndexScan (us/lookup) | SeqScan (us/lookup) | Speedup |
|---------:|-----------------------:|----------------------:|--------:|
|    1,000 |                 129.64 |                 118.31 |    0.9x |
|    5,000 |                 191.67 |                 592.91 |    3.1x |
|   20,000 |                 241.53 |                2474.07 |   10.2x |

**Analysis.** IndexScan latency grows ~O(log N) as expected (242us at
20,000 rows vs. 130us at 1,000 - a 20x increase in N costs less than 2x in
latency), and SeqScan grows ~linearly. The crossover point matters more
than the headline number, though: **at N=1,000, IndexScan is consistently
~10% *slower* than SeqScan** (0.9x "speedup"), reproduced across multiple
runs. This is a direct consequence of `BTREE_LEAF_MAX = BTREE_INTERNAL_MAX
= 4` (deliberately tiny, so a handful of inserts already produces a
multi-level tree for demo purposes - see Section 4 of the README). At
1,000 rows that fanout produces a ~5-level tree, and each level costs a
full `BufferPool::FetchPage`/`UnpinPage` round trip (hash-map lookup +
mutex) regardless of whether the page is already cached - overhead a
SeqScan over a few dozen heap pages doesn't pay per-row. The crossover is
between 1,000 and 5,000 rows on this machine; past it, IndexScan wins by a
growing margin (3.1x at 5k, 10.2x at 20k) because SeqScan cost is genuinely
linear while IndexScan's `log_4(N)` barely grows.

This means the optimizer's blanket "always prefer IndexScan for an
equality predicate on the PK" rule
([optimizer.cpp](../src/query/optimizer.cpp)) is *not* cost-optimal at
small table sizes with this fanout - a real, measured optimizer
imprecision, not a benchmark artifact. A more accurate optimizer would
need either a larger fanout (trading away the easy-to-demo multi-level
splits at small N) or a small-N cost correction. Documented here rather
than silently "fixed" by cherry-picking table sizes that make IndexScan
win, since the whole point of measuring is to find out where the
optimizer's assumptions break.

## 2. Buffer pool size vs. repeated full-scan latency

`bench_buffer_pool.cpp` runs 20 full scans of a ~50-page table (4,000
rows) with pool sizes of 4, 64, and 1024 frames.

| Pool frames | Total time (20 scans) |
|------------:|-----------------------:|
|           4 |                57.93 ms |
|          64 |                33.86 ms |
|        1024 |                34.82 ms |

**Analysis.** The gap is real but modest here because `DiskManager`'s
reads/writes go through `fstream` to a file that's almost entirely warm
in the OS page cache on this hardware - so even a buffer-pool "miss"
rarely costs an actual physical disk seek. The architectural effect
(pin/unpin + LRU eviction, frames recycled under pressure) is exercised
correctly regardless - in fact, a real pin-leak bug in the B+Tree's
split path (see commit `fix(index): leaf-split path leaked a buffer pool
pin per split`) was only caught because a 20,000-row insert workload
exhausted a 256-frame pool and `FetchPage()` started returning `nullptr`.
On slower/networked storage the 4-frame case would show a much larger gap.

## 3. Replication lag and throughput (Track D)

`bench_replication_lag.cpp` runs 500 `INSERT`s on the primary, broadcasts
each over TCP, and measures how long after the primary's last write the
replica has applied everything.

```
Primary applied 500 writes in 67.82ms (7372.1 writes/sec)
Replica caught up 1.26ms after the primary's last write (replication lag)
```

**Analysis.** Replication lag is sub-2ms on localhost - the replica's
background apply thread parses and executes each broadcast statement
about as fast as it arrives. Lag would grow with network latency between
primary and replica (this is logical/statement-based replication over
TCP, not physical WAL shipping - see the Replication section of the main
README for that design tradeoff).

## Limitations of this benchmark suite

- All three benchmarks run on a single machine with a warm OS cache;
  absolute numbers are illustrative, not a substitute for profiling on
  target hardware.
- `HeapFile::InsertTuple` re-walks the page chain from the first page on
  every insert looking for free space, which is O(pages) per insert - the
  IndexScan benchmark's insert phase (not timed above) is the slowest part
  of the run for exactly this reason. This is a known scalability
  limitation, documented in the main README.
