# Concurrency Benchmark Results

| Workload | Mode | Throughput (tx/s) | Avg Read (ms) | P95 Read (ms) | Avg Write (ms) | Blocked Reads | Wait Time (ms) | Committed | Aborted | Versions | Version Bytes |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| read_heavy | 2PL | 225.78 | 13.629 | 35.247 | 40.632 | 15 | 41.199 | 50 | 0 | 30 | 11946 |
| read_heavy | MVCC | 375.71 | 4.783 | 6.641 | 26.542 | 0 | 0.000 | 50 | 0 | 30 | 11946 |
| mixed | 2PL | 180.76 | 12.792 | 44.141 | 41.219 | 24 | 59.810 | 96 | 0 | 44 | 17301 |
| mixed | MVCC | 285.45 | 5.070 | 8.819 | 27.944 | 0 | 0.000 | 96 | 0 | 44 | 17305 |
| hot_key_contention | 2PL | 129.44 | 57.028 | 71.990 | 55.358 | 80 | 752.811 | 90 | 0 | 40 | 15768 |
| hot_key_contention | MVCC | 210.90 | 6.019 | 10.247 | 42.582 | 0 | 0.000 | 90 | 0 | 40 | 15772 |
| write_heavy | 2PL | 118.99 | 22.115 | 45.379 | 44.322 | 52 | 252.819 | 120 | 0 | 60 | 23437 |
| write_heavy | MVCC | 207.19 | 5.022 | 8.266 | 28.886 | 0 | 0.000 | 120 | 0 | 60 | 23437 |

Highlights:
- `2PL` read latency rises sharply on the hot-key workloads because readers wait behind the writer's exclusive lock window.
- `MVCC` keeps the writer hold time but lets readers return from their snapshot, which cuts blocked-read counts and cumulative wait time.
- MVCC pays for that read concurrency with a larger version store and more versions to maintain.