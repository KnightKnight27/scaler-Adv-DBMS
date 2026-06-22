-- MySQL / InnoDB — demo queries
-- Run: mysql -uroot -pdemo dbms_demo < queries.sql > results.txt

SELECT '=== Q1: clustered PK lookup (id is the table itself) ===' AS section;
EXPLAIN FORMAT=TREE SELECT * FROM orders WHERE id = 50000;

SELECT '=== Q2: secondary index, then back-reference to clustered tree ===' AS section;
EXPLAIN FORMAT=TREE SELECT * FROM orders WHERE user_id = 12345;

SELECT '=== Q3: covering index — never touches clustered tree ===' AS section;
EXPLAIN FORMAT=TREE SELECT user_id, qty FROM orders WHERE user_id = 12345;

SELECT '=== Q4: non-selective predicate — InnoDB picks full clustered scan ===' AS section;
EXPLAIN FORMAT=TREE SELECT COUNT(*) FROM orders WHERE qty = 3;
SELECT COUNT(*) FROM orders WHERE qty = 3;

SELECT '=== Q5: 3-table join, IN-country revenue ===' AS section;
EXPLAIN FORMAT=TREE
SELECT p.category, SUM(o.qty * p.price_cents) AS revenue
FROM orders o
JOIN users u    ON u.id = o.user_id
JOIN products p ON p.id = o.product_id
WHERE u.country = 'IN'
GROUP BY p.category
ORDER BY revenue DESC;

SELECT '=== Q6: clustered vs secondary index sizes (bytes) ===' AS section;
SELECT index_name,
       SUM(stat_value) * 16384 AS bytes
FROM   mysql.innodb_index_stats
WHERE  database_name = 'dbms_demo'
  AND  table_name    = 'orders'
  AND  stat_name     = 'n_leaf_pages'
GROUP  BY index_name;

SELECT '=== Q7: buffer pool state ===' AS section;
SELECT pool_size, free_buffers, database_pages
FROM   information_schema.innodb_buffer_pool_stats;

SELECT '=== Q8: InnoDB history list length (undo-log pressure) ===' AS section;
SHOW ENGINE INNODB STATUS\G
