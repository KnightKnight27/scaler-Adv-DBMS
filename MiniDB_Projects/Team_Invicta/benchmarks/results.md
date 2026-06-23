# Benchmark — LSM-tree vs B+ tree heap store

Reproduce with `make bench` (default 20,000 rows) or `./bench <N>`. Raw output
is in `results.txt`.

## Experimental setup

- **Workload:** insert `N` rows `(id INTEGER PRIMARY KEY, payload VARCHAR ~30 B)`
  with sequential keys; then 20,000 random point reads that **hit**; then 20,000
  point reads that **miss** (absent keys); then one full scan; then delete half
  the keys and re-measure on-disk size.
- **Stores compared (same `RowStore` interface, same SQL surface):**
  - **B+ tree heap** — slotted-page heap + B+ tree PK index, through a buffer
    pool sized to hold the working set (warm cache, so point reads measure B+
    tree cost, not buffer-pool thrashing).
  - **LSM** — 256 KB MemTable, size-tiered compaction after 4 SSTables, one
    Bloom filter per SSTable. Deliberately small so SSTables accumulate and
    compaction runs (i.e. the real LSM read/write/space trade-off, not an
    all-in-memory table).
- **Machine:** Apple Silicon (arm64), macOS, `clang++ -O2 -std=c++17`. Single
  thread. Times are wall-clock; reads reported as microseconds per operation.

## Results (N = 20,000)

| Metric | B+ tree heap | LSM | Winner |
|--------|-------------:|----:|--------|
| Write throughput (rows/sec) | 21,292 | **304,052** | **LSM ~14×** |
| Point read — hit (µs/read)  | **0.36** | 1.81 | **B+ tree ~5×** |
| Point read — miss (µs/read) | 0.30 | **0.03** | **LSM ~10×** |
| Full scan (sec)             | ~0.00 | 0.02 | B+ tree |
| Disk bytes (after load)     | 1,495,040 | **1,033,947** | LSM |
| Disk bytes (after deleting half) | 1,495,040 | 1,176,475 | LSM |

(Exact numbers vary run to run; the **ratios and directions are stable** — that
is the point of the experiment.)

## Analysis

**Writes — LSM wins (~14×).** Every B+ tree insert updates pages in place: it
walks the index, may split a leaf/internal node, and writes those 4 KB pages
back through the buffer pool. The LSM store simply appends to an in-memory
sorted MemTable and later flushes it **sequentially** as one immutable SSTable.
Turning scattered page updates into sequential appends is exactly why
write-optimized stores use LSM trees. The PK uniqueness check on insert is kept
cheap by the per-SSTable Bloom filters, so it does not erase the advantage.

**Point reads (hit) — B+ tree wins (~5×).** A B+ tree lookup is one logarithmic
descent to a single leaf. An LSM lookup may have to consult the MemTable and
several SSTables newest-to-oldest until it finds the key — **read amplification**.
This is the flip side of the write trade-off and the expected result.

**Point reads (miss) — LSM wins (~10×).** For an absent key the B+ tree still
descends to a leaf to discover the key is not there. Each LSM SSTable is guarded
by a **Bloom filter**: a miss is rejected without touching the SSTable's data at
all, so most absent-key lookups cost essentially a few hashes. This is the
headline benefit of Bloom filters and shows clearly here.

**Storage / space amplification.** After the bulk load the LSM store is smaller
(~1.03 MB vs ~1.50 MB) — it stores tightly packed sorted runs, while the heap
carries per-page slot overhead and free space. After deleting half the keys the
heap file does **not** shrink (deletes are in-place tombstones; pages are never
reclaimed), staying at 1.50 MB. The LSM store first **grows** (each delete writes
a tombstone record) to ~1.18 MB, but **compaction** then merges runs and drops
the tombstoned keys, keeping it below the heap. This is the LSM space story:
temporary write/space amplification from tombstones, reclaimed by compaction.

**Full scan** is comparable; both stream sorted/heap order. The heap's scan is a
straight walk of its pages; the LSM scan merges the MemTable with the SSTables.

## Takeaways

- LSM is the right choice for **write-heavy** and **point-miss-heavy** workloads
  (ingest, logging, key-existence checks) and for **space** under churn.
- The B+ tree heap is better for **point-read-hit-heavy** and range-scan
  workloads where read latency matters most.
- The cost of LSM is **read amplification** (mitigated by Bloom filters and
  compaction) and **background compaction work** — at very large `N` our *full*
  (non-levelled) compaction rewrites the whole dataset, which is why production
  systems use levelled compaction (a documented future improvement).
