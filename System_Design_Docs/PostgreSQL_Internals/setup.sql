-- PostgreSQL Internals — schema + sample data
--
-- Three tables in a tiny e-commerce shape so we can show:
--   1. selective index lookup vs unselective seq scan
--   2. parallel hash join across three relations
--   3. MVCC version chains during UPDATE
--   4. heap and B-tree page internals via pageinspect
--   5. dead tuples and VACUUM
--   6. WAL position and checkpoint state
--
-- Sizes are picked so that:
--   - users.country='IN' covers ~20% of users        → hash-joinable side
--   - orders.user_id=12345 has ~10 rows              → highly selective (1 in 20k)
--   - orders.qty=3 covers ~30% of the table          → big enough to favor seq scan
-- Run from psql: \i setup.sql

DROP TABLE IF EXISTS orders CASCADE;
DROP TABLE IF EXISTS products CASCADE;
DROP TABLE IF EXISTS users CASCADE;

CREATE TABLE users (
    id         SERIAL PRIMARY KEY,
    email      TEXT NOT NULL,
    country    CHAR(2) NOT NULL,
    created_at DATE    NOT NULL
);

CREATE TABLE products (
    id           SERIAL PRIMARY KEY,
    sku          TEXT NOT NULL,
    category     TEXT NOT NULL,
    price_cents  INT  NOT NULL
);

CREATE TABLE orders (
    id          SERIAL PRIMARY KEY,
    user_id     INT NOT NULL REFERENCES users(id),
    product_id  INT NOT NULL REFERENCES products(id),
    qty         INT NOT NULL,
    ordered_at  DATE NOT NULL
);

-- 20k users, ~20% in 'IN', the rest spread across 9 countries
INSERT INTO users (email, country, created_at)
SELECT
    'user' || i || '@mail.test',
    (ARRAY['IN','IN','US','GB','DE','FR','BR','JP','AU','SG'])[1 + (i % 10)],
    DATE '2024-01-01' + ((i % 540) || ' days')::INTERVAL
FROM generate_series(1, 20000) AS s(i);

-- 500 products across 5 categories
INSERT INTO products (sku, category, price_cents)
SELECT
    'SKU-' || lpad(i::text, 4, '0'),
    (ARRAY['books','electronics','home','grocery','apparel'])[1 + (i % 5)],
    500 + (i * 37) % 9500
FROM generate_series(1, 500) AS s(i);

-- 200k orders. user_id mod 20000 makes each user own ~10 orders.
-- qty is in {1..5}; qty=3 falls on ~30% of rows (rows where i % 7 < 2).
INSERT INTO orders (user_id, product_id, qty, ordered_at)
SELECT
    1 + (i * 2654435761) % 20000,    -- knuth-like spread, deterministic
    1 + (i % 500),
    CASE WHEN i % 7 < 2 THEN 3 ELSE 1 + (i % 5) END,
    DATE '2024-06-01' + ((i % 365) || ' days')::INTERVAL
FROM generate_series(1, 200000) AS s(i);

CREATE INDEX idx_orders_user ON orders(user_id);
CREATE INDEX idx_users_country ON users(country);

ANALYZE users;
ANALYZE products;
ANALYZE orders;

SELECT 'users'    AS table, count(*) FROM users
UNION ALL SELECT 'products', count(*) FROM products
UNION ALL SELECT 'orders',   count(*) FROM orders;
