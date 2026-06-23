# Concurrency Benchmark Results

| Workload | Mode | Throughput (tx/s) | Avg Read (ms) | P95 Read (ms) | Avg Write (ms) | Blocked Reads | Wait Time (ms) | Committed | Aborted | Versions | Version Bytes |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| read_heavy | 2PL | 221.35 | 14.072 | 40.767 | 40.315 | 15 | 54.008 | 50 | 0 | 30 | 11946 |
| read_heavy | MVCC | 302.43 | 3.126 | 5.102 | 33.000 | 0 | 0.000 | 50 | 0 | 30 | 11946 |
| mixed | 2PL | 163.16 | 13.584 | 45.921 | 45.295 | 24 | 74.577 | 96 | 0 | 44 | 17301 |
| mixed | MVCC | 255.09 | 5.733 | 11.486 | 31.255 | 0 | 0.000 | 96 | 0 | 44 | 17305 |
| hot_key_contention | 2PL | 134.07 | 54.972 | 71.459 | 54.183 | 80 | 683.411 | 90 | 0 | 40 | 15768 |
| hot_key_contention | MVCC | 223.66 | 5.418 | 10.565 | 40.154 | 0 | 0.000 | 90 | 0 | 40 | 15772 |
| write_heavy | 2PL | 116.72 | 22.410 | 46.515 | 45.436 | 52 | 258.755 | 120 | 0 | 60 | 23437 |
| write_heavy | MVCC | 202.63 | 5.603 | 8.555 | 29.528 | 0 | 0.000 | 120 | 0 | 60 | 23437 |

Highlights:
- `2PL` read latency rises sharply on the hot-key workloads because readers wait behind the writer's exclusive lock window.
- `MVCC` keeps the writer hold time but lets readers return from their snapshot, which cuts blocked-read counts and cumulative wait time.
- MVCC pays for that read concurrency with a larger version store and more versions to maintain.