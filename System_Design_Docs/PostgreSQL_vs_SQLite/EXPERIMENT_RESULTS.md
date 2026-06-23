# PostgreSQL vs SQLite Experiment Results

Generated locally by `experiments/run_experiments.sh`.

## Tool Versions

```text
postgres (PostgreSQL) 17.10 (Homebrew)
psql (PostgreSQL) 17.10 (Homebrew)
sqlite3 3.51.0 2025-06-12 13:14:41 f0ca7bba1c5e232e5d279fad6338121ab55af0c8c68c84cdfb18ba5114dcaapl (64-bit)
```

## Workload

- 5,000 customers across five cities.
- 50,000 orders with deterministic customer/date/status distribution.
- Indexes: `customers(city)`, `orders(customer_id, order_date)`, and `orders(order_date)`.
- Query: city-level revenue for Bengaluru orders from 2026-01-01 onward.

## PostgreSQL Query Result

```text
   city    | order_count |  revenue  
-----------+-------------+-----------
 Bengaluru |        3238 | 161794.67
(1 row)

```

## PostgreSQL EXPLAIN ANALYZE

```text
GroupAggregate  (cost=259.49..853.06 rows=1 width=47) (actual rows=1 loops=1)
  Buffers: shared hit=363 read=17
  ->  Hash Join  (cost=259.49..836.76 rows=3256 width=13) (actual rows=3238 loops=1)
        Hash Cond: (o.customer_id = c.customer_id)
        Buffers: shared hit=363 read=17
        ->  Bitmap Heap Scan on orders o  (cost=186.46..720.96 rows=16280 width=10) (actual rows=16196 loops=1)
              Recheck Cond: (order_date >= '2026-01-01'::date)
              Heap Blocks: exact=331
              Buffers: shared hit=331 read=15
              ->  Bitmap Index Scan on idx_orders_date  (cost=0.00..182.39 rows=16280 width=0) (actual rows=16196 loops=1)
                    Index Cond: (order_date >= '2026-01-01'::date)
                    Buffers: shared read=15
        ->  Hash  (cost=60.53..60.53 rows=1000 width=11) (actual rows=1000 loops=1)
              Buckets: 1024  Batches: 1  Memory Usage: 55kB
              Buffers: shared hit=32 read=2
              ->  Bitmap Heap Scan on customers c  (cost=16.03..60.53 rows=1000 width=11) (actual rows=1000 loops=1)
                    Recheck Cond: (city = 'Bengaluru'::text)
                    Heap Blocks: exact=32
                    Buffers: shared hit=32 read=2
                    ->  Bitmap Index Scan on idx_customers_city  (cost=0.00..15.78 rows=1000 width=0) (actual rows=1000 loops=1)
                          Index Cond: (city = 'Bengaluru'::text)
                          Buffers: shared read=2
Planning:
  Buffers: shared hit=288 read=6
Planning Time: 0.711 ms
Execution Time: 3.938 ms
```

## SQLite Query Result

```text
city       order_count  revenue  
---------  -----------  ---------
Bengaluru  3238         161794.67
```

## SQLite EXPLAIN QUERY PLAN

```text
QUERY PLAN
|--SEARCH c USING COVERING INDEX idx_customers_city (city=?)
`--SEARCH o USING INDEX idx_orders_customer_date (customer_id=? AND order_date>?)
```

## SQLite File Metrics

```text
metric          value
--------------  -----
page_size       4096 
page_count      1043 
journal_mode    wal  
freelist_count  0    
```
