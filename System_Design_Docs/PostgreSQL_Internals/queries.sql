\echo '===== 1. EXPLAIN ANALYZE: 3-table join (no helpful secondary indexes yet) ====='
EXPLAIN (ANALYZE, BUFFERS, COSTS)
SELECT c.city, count(*) AS orders, sum(o.total) AS revenue
FROM customers c
JOIN orders o      ON o.customer_id = c.id
JOIN order_items i ON i.order_id    = o.id
WHERE c.city = 'Pune'
GROUP BY c.city;

\echo '===== 2. Same query AFTER adding a secondary index on orders.customer_id ====='
CREATE INDEX idx_orders_cust ON orders(customer_id);
CREATE INDEX idx_items_order ON order_items(order_id);
ANALYZE;
EXPLAIN (ANALYZE, BUFFERS, COSTS)
SELECT c.city, count(*) AS orders, sum(o.total) AS revenue
FROM customers c
JOIN orders o      ON o.customer_id = c.id
JOIN order_items i ON i.order_id    = o.id
WHERE c.city = 'Pune'
GROUP BY c.city;

\echo '===== 3. Planner statistics the optimizer relied on (pg_stats) ====='
SELECT attname, n_distinct, most_common_vals, most_common_freqs, null_frac
FROM pg_stats WHERE tablename='customers' AND attname IN ('city','id');

\echo '===== 4. Estimate vs actual on a selective predicate ====='
EXPLAIN ANALYZE SELECT * FROM customers WHERE city='Pune';

\echo '===== 5. MVCC: physical row header columns (xmin, xmax, ctid) ====='
SELECT ctid, xmin, xmax, id, total FROM orders WHERE id IN (1,2,3) ORDER BY id;

\echo '===== 6. MVCC update creates a NEW version (ctid changes, old xmax set) ====='
BEGIN;
SELECT ctid, xmin, xmax, total FROM orders WHERE id=1;
UPDATE orders SET total = total + 100 WHERE id=1;
SELECT ctid, xmin, xmax, total FROM orders WHERE id=1;  -- new ctid, new xmin
COMMIT;

\echo '===== 7. Dead tuples after the update, before VACUUM ====='
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname='orders';
SELECT * FROM pgstattuple('orders');

\echo '===== 8. Buffer manager: what is cached in shared_buffers right now ====='
SELECT c.relname, count(*) AS buffers, pg_size_pretty(count(*)*8192) AS cached
FROM pg_buffercache b JOIN pg_class c ON b.relfilenode = pg_relation_filenode(c.oid)
WHERE c.relname IN ('orders','customers','order_items','idx_orders_cust')
GROUP BY c.relname ORDER BY buffers DESC;

\echo '===== 9. WAL: current write position, generate some, measure bytes ====='
SELECT pg_current_wal_lsn() AS lsn_before;
UPDATE orders SET total = total + 1 WHERE id % 1000 = 0;
SELECT pg_current_wal_lsn() AS lsn_after;
SELECT pg_size_pretty(pg_wal_lsn_diff(pg_current_wal_lsn(), '0/0')) AS total_wal_written;

\echo '===== 10. B-tree index page inspection (pageinspect) ====='
SELECT * FROM bt_metap('orders_pkey');
SELECT type, live_items, dead_items, avg_item_size, page_size, free_size
FROM bt_page_stats('orders_pkey', 1);

\echo '===== 11. VACUUM reclaims dead tuples ====='
VACUUM (VERBOSE) orders;
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname='orders';
