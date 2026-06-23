-- MySQL InnoDB lab for clustered indexes, secondary indexes, and range locks.
--
-- Usage:
--   mysql your_database < innodb_lab.sql
--
-- For the lock section, open two MySQL sessions after running setup.

DROP TABLE IF EXISTS orders_seq;
DROP TABLE IF EXISTS orders_uuid;

CREATE TABLE orders_seq (
  id bigint PRIMARY KEY,
  customer_id bigint NOT NULL,
  created_at datetime NOT NULL,
  status varchar(20) NOT NULL,
  amount decimal(12, 2) NOT NULL,
  INDEX orders_seq_status_idx (status),
  INDEX orders_seq_customer_status_idx (customer_id, status)
) ENGINE=InnoDB;

CREATE TABLE orders_uuid (
  id char(36) PRIMARY KEY,
  customer_id bigint NOT NULL,
  created_at datetime NOT NULL,
  status varchar(20) NOT NULL,
  amount decimal(12, 2) NOT NULL,
  INDEX orders_uuid_status_idx (status)
) ENGINE=InnoDB;

-- MySQL 8 supports recursive CTEs. Increase recursion depth for this session.
SET SESSION cte_max_recursion_depth = 20000;

INSERT INTO orders_seq (id, customer_id, created_at, status, amount)
WITH RECURSIVE seq(n) AS (
  SELECT 1
  UNION ALL
  SELECT n + 1 FROM seq WHERE n < 10000
)
SELECT
  n,
  (n % 1000) + 1,
  NOW() - INTERVAL (n % 730) DAY,
  CASE
    WHEN n % 10 = 0 THEN 'cancelled'
    WHEN n % 3 = 0 THEN 'pending'
    ELSE 'paid'
  END,
  ((n % 5000) + 100) / 10
FROM seq;

INSERT INTO orders_uuid (id, customer_id, created_at, status, amount)
SELECT UUID(), customer_id, created_at, status, amount
FROM orders_seq;

ANALYZE TABLE orders_seq;
ANALYZE TABLE orders_uuid;

-- Clustered index and table size observations.
SHOW TABLE STATUS LIKE 'orders_seq';
SHOW TABLE STATUS LIKE 'orders_uuid';

EXPLAIN SELECT * FROM orders_seq WHERE id BETWEEN 1000 AND 2000;

-- Covered secondary index versus lookup requiring clustered index access.
EXPLAIN SELECT id, status FROM orders_seq WHERE status = 'paid';
EXPLAIN SELECT * FROM orders_seq WHERE status = 'paid';

-- Session 1 range-lock test:
-- SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
-- START TRANSACTION;
-- SELECT * FROM orders_seq
-- WHERE id BETWEEN 1000 AND 2000
-- FOR UPDATE;

-- Session 2:
-- INSERT INTO orders_seq(id, customer_id, created_at, status, amount)
-- VALUES (1500, 1, NOW(), 'paid', 10.00);
--
-- Expected:
--   If id 1500 already exists, try 1501 or delete a small test range first.
--   Inserts into a locked range can wait because next-key locks protect gaps.

