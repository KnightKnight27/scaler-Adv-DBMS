# LSM vs B+Tree Benchmark Report

## Command

```bash
uv run python -m benchmarks.bench_lsm_vs_btree 50000 7
```

## Workload

- Dataset: 50,000 integer keys.
- Logical payload: about 2.5 MB of row bytes.
- Writes: sequential inserts into each engine.
- Reads: random point lookups over up to 5,000 existing keys.
- Misses: 5,000 absent-key point lookups.
- Reported timings: median of 7 trials.
- Engines compared:
  - B+Tree baseline: heap file rows plus in-memory B+ tree primary-key index.
  - LSM-tree: MemTable, L0 SSTables, L1 compaction, Bloom filters, sparse indexes.

## Results
_Median of 7 trials. Timing is wall-clock and load-dependent._

| Metric | B+Tree (heap) | LSM-tree |
|---|---|---|
| Write throughput (ops/s) | 15,273 | 57,068 |
| Point read hit (us) | 17.66 | 120.12 |
| Point read miss (us) | 0.55 | 7.15 |
| Space amplification | 1.09x | 1.33x |
| Write amplification | 1.00x | 2.93x |
| Compactions | 0 | 2 |
| Bloom-filter skips (5k misses) | 0 | 23,262 |

Timing range across 7 trials (min - max):
- Write ops/s: B+Tree 11,809 - 16,787; LSM 44,725 - 67,016
- Read-hit us: B+Tree 12.86 - 24.71; LSM 101.79 - 140.93
- Read-miss us: B+Tree 0.47 - 0.95; LSM 6.94 - 10.82

## Analysis

The LSM-tree write path is about 3.7x faster on this workload because writes go to an in-memory MemTable and later flush as sorted sequential SSTables. The heap+B+ tree baseline inserts each row into the heap and updates the primary-key index immediately.

The B+ tree baseline has lower point-read latency; LSM hits are about 6.8x slower here because reads may check the MemTable plus multiple SSTables. Bloom filters reduce wasted work for negative lookups: 23,262 SSTable reads were skipped during the miss probes.

The LSM-tree also pays write and space amplification. Compaction rewrites data, producing 2.93x write amplification and 1.33x space amplification in this run. This is the expected Track C trade-off: higher write throughput in exchange for more read work and background rewrite cost.
