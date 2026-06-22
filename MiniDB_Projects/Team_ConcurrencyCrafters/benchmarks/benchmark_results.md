# Baseline Benchmark Results

| Benchmark | Duration (s) | Rows/Events |
| --- | ---: | ---: |
| insert_100_records | 0.870064 | 100 |
| point_lookup_btree | 0.000127 | 1 |
| table_scan | 0.000711 | 100 |
| nested_loop_join | 0.001788 | 100 |
| 2pl_concurrent_workload | 0.031702 | t1:committed,t2:aborted |

Notes:
- Insert benchmark loads 100 users and matching orders to seed the rest of the workload.
- Point lookups use the automatically created primary-key B+ Tree.
- The concurrent workload measures strict 2PL lock acquisition and release under two threads.