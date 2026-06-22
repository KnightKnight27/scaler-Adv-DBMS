-- MySQL / InnoDB experiments (run on MySQL 9.6, ENGINE=InnoDB)
--   mysql -uroot < mysql_experiments.sql
-- Covers: clustered index, secondary-index double lookup, durability infra,
--         isolation level, and record + gap (next-key) locking.

DROP DATABASE IF EXISTS adbms_exp; CREATE DATABASE adbms_exp; USE adbms_exp;
SET SESSION cte_max_recursion_depth = 300000;

CREATE TABLE customers(id INT PRIMARY KEY, name VARCHAR(40), city VARCHAR(20)) ENGINE=InnoDB;
CREATE TABLE orders(id INT PRIMARY KEY, customer_id INT, amount DECIMAL(10,2), status VARCHAR(12),
                    KEY idx_customer(customer_id), KEY idx_status(status)) ENGINE=InnoDB;
INSERT INTO customers
  WITH RECURSIVE s(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM s WHERE n<10000)
  SELECT n, CONCAT('Customer_',n), CONCAT('City_',n%6) FROM s;
INSERT INTO orders
  WITH RECURSIVE s(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM s WHERE n<200000)
  SELECT n, 1+(n*2654435761 % 10000), (n%1000), ELT(1+n%4,'paid','pending','shipped','cancelled') FROM s;
ANALYZE TABLE orders;

-- Isolation: InnoDB defaults to REPEATABLE READ (Postgres defaults to READ COMMITTED).
SELECT @@transaction_isolation, @@autocommit;

-- Clustered index: PK lookup walks the clustered B-tree whose leaves hold the row.
EXPLAIN FORMAT=TREE SELECT * FROM orders WHERE id=12345;
-- Secondary index: index seek returns the PK, then a second lookup into the clustered index.
EXPLAIN SELECT * FROM orders WHERE customer_id=4242;
EXPLAIN SELECT COUNT(*) FROM orders WHERE status='paid';

-- Durability/concurrency infrastructure.
SELECT VARIABLE_NAME, VARIABLE_VALUE FROM performance_schema.global_variables
WHERE VARIABLE_NAME IN ('innodb_buffer_pool_size','innodb_redo_log_capacity',
                        'innodb_doublewrite','innodb_flush_log_at_trx_commit');
-- Clustered data_length vs secondary index_length (secondary indexes embed the PK).
SELECT table_name, data_length, index_length FROM information_schema.tables
WHERE table_schema='adbms_exp';

-- Next-key locking: a ranged FOR UPDATE locks matched rows AND the gaps between them.
START TRANSACTION;
SELECT id FROM orders WHERE id BETWEEN 100 AND 105 FOR UPDATE;
SELECT LOCK_TYPE, LOCK_MODE, LOCK_STATUS, LOCK_DATA
FROM performance_schema.data_locks WHERE OBJECT_NAME='orders' ORDER BY LOCK_TYPE, LOCK_DATA;
COMMIT;
