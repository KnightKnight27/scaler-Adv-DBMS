# Track A benchmark report

## Goal

Compare MiniDB's batch operators with traditional row-at-a-time operators.
Both modes execute the same three-filter query and must return the same count.

## Method

- Release build on Windows using MSVC 19.44.
- Workloads: 10K, 50K, 100K, and 200K rows.
- Each workload runs 50 times and reports the average.
- Input creation and grouping are done before timing for both modes.
- The timed section contains only operator execution.
- Wall time uses `steady_clock`; CPU time uses the standard C++ `clock`.
- Speedup is `row wall time / batch wall time`; above `1.0x` is better.

## Latest result

| Rows | Batch wall (us) | Row wall (us) | Speedup | Batch CPU (us) | Row CPU (us) |
|---:|---:|---:|---:|---:|---:|
| 10,000 | 886 | 898 | 1.01x | 940 | 920 |
| 50,000 | 4,771 | 4,946 | 1.04x | 4,760 | 4,960 |
| 100,000 | 9,325 | 9,934 | 1.07x | 9,400 | 9,900 |
| 200,000 | 50,914 | 55,682 | 1.09x | 50,920 | 55,680 |

Results vary slightly by machine, so run `benchmark` before the demonstration.

## CPU and memory discussion

Batch mode performs one operator call for up to 512 rows, while row mode makes
one call per row. This reduced both wall time and CPU time in the measured
workloads.

The approximate temporary operator buffer is:

- batch mode: `512 * sizeof(Row)` = 12,288 bytes;
- row mode: `sizeof(Row)` = 24 bytes.

Both modes own the same input rows; the values above describe only the extra
operator buffer. Batch mode therefore uses a small amount of extra memory to
reduce function-call overhead. This is the main Track A trade-off.
