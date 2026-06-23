-- Shared sample schema: a small e-commerce dataset
-- Used to compare query plans and behavior across SQLite / Postgres / MySQL.

PRAGMA foreign_keys = ON;

CREATE TABLE customers (
    id        INTEGER PRIMARY KEY,
    name      TEXT NOT NULL,
    city      TEXT NOT NULL,
    created   TEXT NOT NULL
);

CREATE TABLE products (
    id        INTEGER PRIMARY KEY,
    title     TEXT NOT NULL,
    price     REAL NOT NULL
);

CREATE TABLE orders (
    id           INTEGER PRIMARY KEY,
    customer_id  INTEGER NOT NULL REFERENCES customers(id),
    order_date   TEXT NOT NULL,
    total        REAL NOT NULL
);

CREATE TABLE order_items (
    id          INTEGER PRIMARY KEY,
    order_id    INTEGER NOT NULL REFERENCES orders(id),
    product_id  INTEGER NOT NULL REFERENCES products(id),
    qty         INTEGER NOT NULL
);

-- Seed data using recursive CTEs so the dataset is large enough that
-- the planner actually has to make index-vs-scan decisions.
INSERT INTO customers(id, name, city, created)
WITH RECURSIVE c(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM c WHERE n < 50000)
SELECT n, 'cust_' || n,
       CASE n % 5 WHEN 0 THEN 'Mumbai' WHEN 1 THEN 'Delhi' WHEN 2 THEN 'Pune'
                  WHEN 3 THEN 'Bangalore' ELSE 'Chennai' END,
       '2026-01-01'
FROM c;

INSERT INTO products(id, title, price)
WITH RECURSIVE p(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM p WHERE n < 1000)
SELECT n, 'product_' || n, (n % 100) + 0.99 FROM p;

INSERT INTO orders(id, customer_id, order_date, total)
WITH RECURSIVE o(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM o WHERE n < 200000)
SELECT n, (n % 50000) + 1, '2026-02-01', (n % 500) + 10.0 FROM o;

INSERT INTO order_items(id, order_id, product_id, qty)
WITH RECURSIVE oi(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM oi WHERE n < 200000)
SELECT n, (n % 200000) + 1, (n % 1000) + 1, (n % 5) + 1 FROM oi;
