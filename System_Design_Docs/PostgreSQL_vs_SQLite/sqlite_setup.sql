-- PostgreSQL vs SQLite — SQLite-side schema and data
-- Run: sqlite3 demo.db < sqlite_setup.sql
--
-- Same logical schema as the Postgres side (users / products / orders)
-- so we can compare like-for-like:
--   - file size on disk
--   - query plans (SQLite uses EXPLAIN QUERY PLAN, no cost-based parallel join)
--   - per-statement timing under .timer on

.headers on
.mode column
.timer on

DROP TABLE IF EXISTS orders;
DROP TABLE IF EXISTS products;
DROP TABLE IF EXISTS users;

CREATE TABLE users (
    id         INTEGER PRIMARY KEY,
    email      TEXT NOT NULL,
    country    TEXT NOT NULL,
    created_at TEXT NOT NULL
);

CREATE TABLE products (
    id          INTEGER PRIMARY KEY,
    sku         TEXT NOT NULL,
    category    TEXT NOT NULL,
    price_cents INTEGER NOT NULL
);

CREATE TABLE orders (
    id         INTEGER PRIMARY KEY,
    user_id    INTEGER NOT NULL REFERENCES users(id),
    product_id INTEGER NOT NULL REFERENCES products(id),
    qty        INTEGER NOT NULL,
    ordered_at TEXT NOT NULL
);

-- 20k users; ~20% in 'IN', the rest spread across 9 countries
WITH RECURSIVE seq(i) AS (
    SELECT 1 UNION ALL SELECT i + 1 FROM seq WHERE i < 20000
)
INSERT INTO users (id, email, country, created_at)
SELECT i,
       'user' || i || '@mail.test',
       CASE i % 10
            WHEN 0 THEN 'IN' WHEN 1 THEN 'IN'
            WHEN 2 THEN 'US' WHEN 3 THEN 'GB' WHEN 4 THEN 'DE'
            WHEN 5 THEN 'FR' WHEN 6 THEN 'BR' WHEN 7 THEN 'JP'
            WHEN 8 THEN 'AU' ELSE 'SG'
       END,
       date('2024-01-01', '+' || (i % 540) || ' days')
FROM seq;

-- 500 products across 5 categories
WITH RECURSIVE seq(i) AS (
    SELECT 1 UNION ALL SELECT i + 1 FROM seq WHERE i < 500
)
INSERT INTO products (id, sku, category, price_cents)
SELECT i,
       printf('SKU-%04d', i),
       CASE i % 5
            WHEN 0 THEN 'books' WHEN 1 THEN 'electronics'
            WHEN 2 THEN 'home'  WHEN 3 THEN 'grocery'
            ELSE 'apparel'
       END,
       500 + (i * 37) % 9500
FROM seq;

-- 200k orders; same Knuth-spread on user_id so each user owns ~10 orders
WITH RECURSIVE seq(i) AS (
    SELECT 1 UNION ALL SELECT i + 1 FROM seq WHERE i < 200000
)
INSERT INTO orders (id, user_id, product_id, qty, ordered_at)
SELECT i,
       1 + ((i * 2654435761) % 20000),
       1 + (i % 500),
       CASE WHEN (i % 7) < 2 THEN 3 ELSE 1 + (i % 5) END,
       date('2024-06-01', '+' || (i % 365) || ' days')
FROM seq;

CREATE INDEX idx_orders_user   ON orders(user_id);
CREATE INDEX idx_users_country ON users(country);

ANALYZE;

SELECT 'users'    AS tbl, count(*) AS rows FROM users
UNION ALL SELECT 'products', count(*) FROM products
UNION ALL SELECT 'orders',   count(*) FROM orders;

-- ===== comparison queries =====
.print
.print --- file size + page geometry ---
PRAGMA page_size;
PRAGMA page_count;
SELECT (SELECT page_size FROM pragma_page_size) * (SELECT page_count FROM pragma_page_count) AS bytes_on_disk;

.print
.print --- Q1: 3-table join, IN-country revenue per category ---
EXPLAIN QUERY PLAN
SELECT p.category, SUM(o.qty * p.price_cents) AS revenue
FROM orders o
JOIN users    u ON u.id = o.user_id
JOIN products p ON p.id = o.product_id
WHERE u.country = 'IN'
GROUP BY p.category
ORDER BY revenue DESC;

SELECT p.category, SUM(o.qty * p.price_cents) AS revenue
FROM orders o
JOIN users    u ON u.id = o.user_id
JOIN products p ON p.id = o.product_id
WHERE u.country = 'IN'
GROUP BY p.category
ORDER BY revenue DESC;

.print
.print --- Q2a: highly selective index lookup ---
EXPLAIN QUERY PLAN SELECT * FROM orders WHERE user_id = 12345;
SELECT count(*) FROM orders WHERE user_id = 12345;

.print
.print --- Q2b: non-selective predicate — expect full scan ---
EXPLAIN QUERY PLAN SELECT count(*) FROM orders WHERE qty = 3;
SELECT count(*) FROM orders WHERE qty = 3;

.print
.print --- WAL + journal mode ---
PRAGMA journal_mode;
PRAGMA synchronous;
