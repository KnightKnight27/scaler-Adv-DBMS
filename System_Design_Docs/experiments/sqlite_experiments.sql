-- SQLite experiments (run on SQLite 3.51)
--   sqlite3 shop.db < sqlite_experiments.sql
-- Covers: single-file page storage, query plans, clustered rowid, WAL mode.

CREATE TABLE customers(id INTEGER PRIMARY KEY, name TEXT, city TEXT);
CREATE TABLE orders(id INTEGER PRIMARY KEY, customer_id INT, amount REAL, status TEXT);
INSERT INTO customers SELECT value,'Customer_'||value,'City_'||(abs(random())%6)
  FROM generate_series(1,10000);
INSERT INTO orders SELECT value, 1+abs(random())%10000, abs(random())%1000,
  (CASE abs(random())%4 WHEN 0 THEN 'paid' WHEN 1 THEN 'pending'
                        WHEN 2 THEN 'shipped' ELSE 'cancelled' END)
  FROM generate_series(1,200000);
CREATE INDEX idx_orders_customer ON orders(customer_id);
ANALYZE;

-- Storage internals: the whole DB is one file of fixed-size pages.
PRAGMA page_size;     -- 4096
PRAGMA page_count;    -- file size = page_size * page_count
PRAGMA journal_mode;  -- default 'delete' (rollback journal)

-- Query plans. The INTEGER PRIMARY KEY *is* the rowid -> table is clustered on it.
EXPLAIN QUERY PLAN SELECT * FROM orders WHERE id = 12345;          -- INTEGER PRIMARY KEY (rowid=?)
EXPLAIN QUERY PLAN SELECT * FROM orders WHERE customer_id = 4242;  -- secondary index
EXPLAIN QUERY PLAN SELECT count(*) FROM orders WHERE status='paid';-- full scan (no index)
EXPLAIN QUERY PLAN
  SELECT c.city,count(*) FROM customers c JOIN orders o ON o.customer_id=c.id
  WHERE c.city='City_1' GROUP BY c.city;                          -- nested-loop join

-- WAL mode (better read/write concurrency than the default rollback journal).
PRAGMA journal_mode = WAL;
INSERT INTO orders VALUES (900001, 5, 5, 'paid');
PRAGMA wal_checkpoint;   -- folds the WAL back into the main db file
