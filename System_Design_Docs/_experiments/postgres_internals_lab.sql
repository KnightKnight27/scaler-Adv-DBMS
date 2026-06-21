-- PostgreSQL internals lab for planner statistics, joins, MVCC, and VACUUM.
--
-- Usage:
--   psql -d your_database -f postgres_internals_lab.sql
--
-- The data volume is modest so it can run on a local PostgreSQL instance.

DROP TABLE IF EXISTS payments;
DROP TABLE IF EXISTS orders;
DROP TABLE IF EXISTS customers;

CREATE TABLE customers (
  id bigserial PRIMARY KEY,
  region text NOT NULL
);

CREATE TABLE orders (
  id bigserial PRIMARY KEY,
  customer_id bigint NOT NULL REFERENCES customers(id),
  status text NOT NULL,
  created_at timestamp NOT NULL
);

CREATE TABLE payments (
  id bigserial PRIMARY KEY,
  order_id bigint NOT NULL REFERENCES orders(id),
  amount numeric(12, 2) NOT NULL
);

INSERT INTO customers (region)
SELECT CASE
  WHEN gs % 4 = 0 THEN 'north'
  WHEN gs % 4 = 1 THEN 'south'
  WHEN gs % 4 = 2 THEN 'east'
  ELSE 'west'
END
FROM generate_series(1, 1000) AS gs;

INSERT INTO orders (customer_id, status, created_at)
SELECT
  ((gs - 1) % 1000) + 1,
  CASE
    WHEN gs % 10 = 0 THEN 'cancelled'
    WHEN gs % 3 = 0 THEN 'pending'
    ELSE 'paid'
  END,
  now() - ((gs % 730) || ' days')::interval
FROM generate_series(1, 20000) AS gs;

INSERT INTO payments (order_id, amount)
SELECT id, ((id % 5000) + 100)::numeric / 10
FROM orders
WHERE status = 'paid';

CREATE INDEX orders_customer_status_idx ON orders(customer_id, status);
CREATE INDEX orders_status_idx ON orders(status);
CREATE INDEX payments_order_idx ON payments(order_id);

ANALYZE customers;
ANALYZE orders;
ANALYZE payments;

-- Join plan and buffer behavior.
EXPLAIN (ANALYZE, BUFFERS)
SELECT c.region, count(*) AS paid_orders, sum(p.amount) AS revenue
FROM customers c
JOIN orders o ON o.customer_id = c.id
JOIN payments p ON p.order_id = o.id
WHERE o.status = 'paid'
GROUP BY c.region
ORDER BY c.region;

-- Planner statistics used for selectivity estimates.
SELECT attname, null_frac, n_distinct, most_common_vals
FROM pg_stats
WHERE schemaname = current_schema()
  AND tablename = 'orders'
ORDER BY attname;

-- Selective versus less selective predicates.
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM orders WHERE status = 'paid';

EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM orders WHERE customer_id = 42 AND status = 'paid';

-- MVCC cleanup observation.
UPDATE orders
SET status = 'archived'
WHERE created_at < now() - interval '1 year';

SELECT relname, n_live_tup, n_dead_tup
FROM pg_stat_user_tables
WHERE relname IN ('orders', 'payments', 'customers')
ORDER BY relname;

VACUUM orders;

SELECT relname, n_live_tup, n_dead_tup
FROM pg_stat_user_tables
WHERE relname = 'orders';

