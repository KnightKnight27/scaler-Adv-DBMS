-- PostgreSQL Internals — demo queries
-- Run: psql -d dbms_demo -f queries.sql > results.txt

-- enable extension for page inspection (needs superuser)
CREATE EXTENSION IF NOT EXISTS pageinspect;
CREATE EXTENSION IF NOT EXISTS pgstattuple;

-- ============================================================
-- Q1. parallel 3-table hash join (orders × users × products)
-- pick a low-selectivity filter on users.country so the planner
-- chooses hash join over nested loops
-- ============================================================
\echo === Q1: hash join, grouped revenue per category for IN users ===
EXPLAIN (ANALYZE, BUFFERS, VERBOSE)
SELECT p.category, SUM(o.qty * p.price_cents) AS revenue
FROM orders o
JOIN users    u ON u.id = o.user_id
JOIN products p ON p.id = o.product_id
WHERE u.country = 'IN'
GROUP BY p.category
ORDER BY revenue DESC;

-- ============================================================
-- Q2a. index-driven lookup — user_id is 1/20000 selective
-- ============================================================
\echo === Q2a: index lookup for one user_id ===
EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM orders WHERE user_id = 12345;

-- ============================================================
-- Q2b. unselective predicate — planner should pick seq scan
-- (qty=3 matches ~30% of rows; an index on qty would not help)
-- ============================================================
\echo === Q2b: full scan for non-selective predicate ===
EXPLAIN (ANALYZE, BUFFERS) SELECT count(*) FROM orders WHERE qty = 3;

-- ============================================================
-- Q3. MVCC version chain — observe xmin/xmax/ctid before & after UPDATE
-- ============================================================
\echo === Q3a: row before update ===
SELECT ctid, xmin, xmax, id, qty FROM orders WHERE id = 1;
BEGIN;
UPDATE orders SET qty = qty + 100 WHERE id = 1;
\echo === Q3b: row inside the same txn after update ===
SELECT ctid, xmin, xmax, id, qty FROM orders WHERE id = 1;
COMMIT;
\echo === Q3c: row after commit (xmin = the txn that wrote it) ===
SELECT ctid, xmin, xmax, id, qty FROM orders WHERE id = 1;

-- ============================================================
-- Q4. heap page layout — pageinspect on block 0 of orders
-- ============================================================
\echo === Q4a: page header for orders block 0 ===
SELECT * FROM page_header(get_raw_page('orders', 0));
\echo === Q4b: first 5 item pointers + tuple headers ===
SELECT lp, lp_off, lp_len, t_xmin, t_xmax, t_ctid
FROM   heap_page_items(get_raw_page('orders', 0))
LIMIT  5;

-- ============================================================
-- Q5. B-tree metadata — root level and stats of idx_orders_user
-- ============================================================
\echo === Q5: B-tree meta for idx_orders_user ===
SELECT * FROM bt_metap('idx_orders_user');
SELECT blkno, type, live_items, dead_items, avg_item_size, free_size, btpo_level
FROM   bt_page_stats('idx_orders_user', 1);

-- ============================================================
-- Q6. dead tuples + VACUUM
-- update ~10% of rows so dead tuples are observable, then VACUUM
-- ============================================================
\echo === Q6a: stats before update ===
SELECT n_live_tup, n_dead_tup, last_vacuum FROM pg_stat_user_tables WHERE relname='orders';
UPDATE orders SET qty = qty WHERE id BETWEEN 1 AND 20000;
\echo === Q6b: live vs dead tuples right after update (via pgstattuple) ===
SELECT tuple_count, dead_tuple_count, dead_tuple_percent FROM pgstattuple('orders');
VACUUM (VERBOSE, ANALYZE) orders;
\echo === Q6c: stats after VACUUM ===
SELECT n_live_tup, n_dead_tup, last_vacuum FROM pg_stat_user_tables WHERE relname='orders';

-- ============================================================
-- Q7. WAL position + durability knobs
-- ============================================================
\echo === Q7: WAL state ===
SELECT pg_current_wal_lsn() AS current_lsn,
       pg_walfile_name(pg_current_wal_lsn()) AS current_walfile;
SELECT name, setting FROM pg_settings
WHERE name IN ('wal_level','fsync','synchronous_commit','wal_compression','checkpoint_timeout');
