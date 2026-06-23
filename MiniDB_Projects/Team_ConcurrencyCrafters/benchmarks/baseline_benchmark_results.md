# Baseline Benchmark Results

| Benchmark | Duration (s) | Rows/Events |
| --- | ---: | ---: |
| insert_100_records | 1.714953 | 100 |
| point_lookup_btree | 0.000178 | 1 |
| table_scan | 0.001083 | 100 |
| nested_loop_join | 0.002067 | 100 |
| 2pl_concurrent_workload | 0.051412 | t1:committed,t2:aborted |

Notes:
- Insert benchmark loads 100 users and matching orders to seed the rest of the workload.
- Point lookups use the automatically created primary-key B+ Tree.
- The concurrent workload measures strict 2PL lock acquisition and release under two threads.