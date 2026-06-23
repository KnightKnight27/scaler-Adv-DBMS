-- PostgreSQL sample schema: same small e-commerce dataset as the SQLite run,
-- built with generate_series so the planner has real statistics to work with.

DROP TABLE IF EXISTS order_items, orders, products, customers CASCADE;

CREATE TABLE customers (
    id      int PRIMARY KEY,
    name    text NOT NULL,
    city    text NOT NULL,
    created date NOT NULL
);

CREATE TABLE products (
    id    int PRIMARY KEY,
    title text NOT NULL,
    price numeric(10,2) NOT NULL
);

CREATE TABLE orders (
    id          int PRIMARY KEY,
    customer_id int NOT NULL REFERENCES customers(id),
    order_date  date NOT NULL,
    total       numeric(10,2) NOT NULL
);

CREATE TABLE order_items (
    id         int PRIMARY KEY,
    order_id   int NOT NULL REFERENCES orders(id),
    product_id int NOT NULL REFERENCES products(id),
    qty        int NOT NULL
);

INSERT INTO customers
SELECT g,
       'cust_' || g,
       (ARRAY['Mumbai','Delhi','Pune','Bangalore','Chennai'])[1 + g % 5],
       DATE '2026-01-01'
FROM generate_series(1, 50000) g;

INSERT INTO products
SELECT g, 'product_' || g, ((g % 100) + 0.99)::numeric(10,2)
FROM generate_series(1, 1000) g;

INSERT INTO orders
SELECT g, (g % 50000) + 1, DATE '2026-02-01', ((g % 500) + 10.0)::numeric(10,2)
FROM generate_series(1, 200000) g;

INSERT INTO order_items
SELECT g, (g % 200000) + 1, (g % 1000) + 1, (g % 5) + 1
FROM generate_series(1, 200000) g;

ANALYZE;
