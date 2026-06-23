# Benchmark Results

Environment: C++17 (g++ -O2), single-file heap per table, 4 KB pages, buffer
pool of 16 frames.

## 1. Index scan vs sequential scan (point lookups)

Reproduce with `make bench` (runs `bench_index`). The table has a column `dup`
holding the same value as the primary key `id`. Looking up by `id` uses the B+
tree (IndexScan); looking up by `dup` is not indexed, so it forces a full scan.
Both match exactly one row, so it is a fair comparison. 300 random lookups each.

| Rows | Index / lookup | Scan / lookup | Speedup |
|------|----------------|---------------|---------|
| 500  | 4.4 us         | 2,973 us      | 674x    |
| 2000 | 3.5 us         | 11,798 us     | 3,407x  |
| 8000 | 10.0 us        | 47,078 us     | 4,700x  |

The index lookup stays nearly flat (O(log n) tree descent) while the sequential
scan grows linearly with the row count (O(n), it must read and decode every
row). The bigger the table, the more the index wins, which is exactly why the
optimizer chooses an IndexScan for an equality on the primary key. The absolute
scan time is dominated by decoding every row; the point is the flat-vs-linear
shape of the two curves.

## 2. MVCC vs 2PL under write contention

Reproduce with `make bench` (runs `bench_mvcc`). A writer is part-way through an
uncommitted update to all 10 keys; 100 readers then read all 10 keys (1000
reads total).

| Scheme | Reads served | Reads blocked |
|--------|--------------|---------------|
| 2PL    | 0            | 1000 (100%)   |
| MVCC   | 1000         | 0 (0%)        |

Under 2PL a reader needs a shared lock, which conflicts with the writer's
exclusive lock, so every read blocks. Under MVCC the reader reads against its
snapshot and sees the last committed version, so it never blocks. This is the
headline benefit of the Track B extension: readers do not block writers.

## 3. Deadlock detection and crash recovery

These are demonstrated (not timed) by `make demos`:

- `deadlock_demo`: two transactions form a wait-for cycle (T1 waits for T2, T2
  waits for T1); the cycle is detected and the youngest transaction is aborted,
  after which the other proceeds.
- `recovery_demo`: a committed transaction's rows survive a simulated crash
  while an uncommitted transaction's changes are rolled back (redo of winners,
  undo of losers).
