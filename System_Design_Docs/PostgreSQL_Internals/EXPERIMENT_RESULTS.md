# PostgreSQL Internals Experiment Results

Generated locally by `experiments/run_experiments.sh`.

## Tool Versions

```text
postgres (PostgreSQL) 17.10 (Homebrew)
psql (PostgreSQL) 17.10 (Homebrew)
adbms_pg_internals_lab
```

## Server Settings

```text
 shared_buffers 
----------------
 128MB
(1 row)

 wal_level 
-----------
 replica
(1 row)

 checkpoint_timeout 
--------------------
 5min
(1 row)

 max_wal_size 
--------------
 1GB
(1 row)

 default_statistics_target 
---------------------------
 100
(1 row)

```

## EXPLAIN ANALYZE With Buffers

```text
GroupAggregate  (cost=527.24..1578.90 rows=1 width=45) (actual rows=1 loops=1)
  Buffers: shared hit=652 read=31
  ->  Hash Join  (cost=527.24..1546.19 rows=6538 width=11) (actual rows=6495 loops=1)
        Hash Cond: (l.account_id = a.account_id)
        Buffers: shared hit=652 read=31
        ->  Bitmap Heap Scan on ledger_entries l  (cost=336.27..1277.99 rows=29417 width=10) (actual rows=29162 loops=1)
              Recheck Cond: (created_at >= '2026-01-01'::date)
              Heap Blocks: exact=574
              Buffers: shared hit=574 read=27
              ->  Bitmap Index Scan on idx_ledger_created  (cost=0.00..328.92 rows=29417 width=0) (actual rows=29162 loops=1)
                    Index Cond: (created_at >= '2026-01-01'::date)
                    Buffers: shared read=27
        ->  Hash  (cost=157.63..157.63 rows=2667 width=9) (actual rows=2667 loops=1)
              Buckets: 4096  Batches: 1  Memory Usage: 147kB
              Buffers: shared hit=78 read=4
              ->  Bitmap Heap Scan on accounts a  (cost=39.62..157.63 rows=2667 width=9) (actual rows=2667 loops=1)
                    Recheck Cond: ((region = 'north'::text) AND (status = 'active'::text))
                    Heap Blocks: exact=78
                    Buffers: shared hit=78 read=4
                    ->  Bitmap Index Scan on idx_accounts_region_status  (cost=0.00..38.95 rows=2667 width=0) (actual rows=2667 loops=1)
                          Index Cond: ((region = 'north'::text) AND (status = 'active'::text))
                          Buffers: shared read=4
Planning:
  Buffers: shared hit=310 read=6
Planning Time: 0.935 ms
Execution Time: 7.626 ms
```

## Planner Statistics Sample

```text
   tablename    |  attname   | n_distinct |                                     most_common_vals                                      
----------------+------------+------------+-------------------------------------------------------------------------------------------
 accounts       | region     |          4 | {east,north,south,west}
 accounts       | status     |          2 | {active,blocked}
 ledger_entries | created_at |        540 | {2025-09-08,2025-07-05,2026-04-26,2026-05-21,2025-05-03,2025-06-09,2026-02-07,2025-05-07}
(3 rows)

```

## B-Tree Metadata

```text
 magic  | version | root | level | fastroot | fastlevel | last_cleanup_num_delpages | last_cleanup_num_tuples | allequalimage 
--------+---------+------+-------+----------+-----------+---------------------------+-------------------------+---------------
 340322 |       4 |    3 |     1 |        3 |         1 |                         0 |                      -1 | t
(1 row)

```

## MVCC Tuple Metadata

```text
     phase     | ctid  | xmin | xmax | id |     note      
---------------+-------+------+------+----+---------------
 before update | (0,1) |  789 |    0 |  1 | first version
(1 row)

    phase     | ctid  | xmin | xmax | id |      note      
--------------+-------+------+------+----+----------------
 after update | (0,2) |  790 |    0 |  1 | second version
(1 row)

 lp | lp_flags | t_xmin | t_xmax | t_ctid 
----+----------+--------+--------+--------
  1 |        1 |    789 |    790 | (0,2)
  2 |        1 |    790 |      0 | (0,2)
(2 rows)

```

## WAL Bytes From Update

```text
 lsn_after | wal_bytes_generated 
-----------+---------------------
 0/2AF48D0 |               33808
(1 row)

```
