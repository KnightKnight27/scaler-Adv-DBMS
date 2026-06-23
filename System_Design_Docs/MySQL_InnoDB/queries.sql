USE shop;

SELECT '=== 1. Default isolation level (InnoDB) ===' AS x;
SELECT @@transaction_isolation, @@innodb_buffer_pool_size/1024/1024 AS buffer_pool_mb;

SELECT '=== 2. EXPLAIN: secondary index lookup, then back to clustered PK ===' AS x;
EXPLAIN FORMAT=TREE
SELECT o.id, o.total FROM orders o WHERE o.customer_id = 12345;

SELECT '=== 3. EXPLAIN: 3-table join ===' AS x;
EXPLAIN FORMAT=TREE
SELECT c.city, count(*) ord, sum(o.total) rev
FROM customers c JOIN orders o ON o.customer_id=c.id
JOIN order_items i ON i.order_id=o.id
WHERE c.city='Pune' GROUP BY c.city;

SELECT '=== 4. Clustered index: data is stored in PK order. Page/space info ===' AS x;
SELECT table_name, ROUND(data_length/1024/1024,1) AS data_mb,
       ROUND(index_length/1024/1024,1) AS index_mb, table_rows
FROM information_schema.tables WHERE table_schema='shop' ORDER BY table_name;

SELECT '=== 5. InnoDB clustered vs secondary index pages (from INNODB_TABLES) ===' AS x;
SELECT name, n_rows, clustered_index_size, sum_of_other_index_sizes
FROM information_schema.INNODB_TABLESTATS WHERE name LIKE 'shop/%' ORDER BY name;

SELECT '=== 6. Buffer pool contents by table ===' AS x;
SELECT pages.TABLE_NAME, COUNT(*) AS pages_cached
FROM information_schema.INNODB_BUFFER_PAGE pages
WHERE pages.TABLE_NAME LIKE '%shop%'
GROUP BY pages.TABLE_NAME ORDER BY pages_cached DESC LIMIT 10;

SELECT '=== 7. Redo log + undo/transaction state (excerpt of SHOW ENGINE INNODB STATUS) ===' AS x;
SHOW ENGINE INNODB STATUS\G
