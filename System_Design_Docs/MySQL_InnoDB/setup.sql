-- MySQL / InnoDB — schema + sample data
--
-- Same e-commerce shape as the Postgres side. InnoDB-specific knobs we want
-- to probe later:
--   - clustered PK lookup vs secondary index back-reference vs covering index
--   - selectivity-driven plan switch (index → seq scan)
--   - next-key locks on a range under REPEATABLE READ
--   - undo log / history list length during a long-running transaction
--
-- Schema notes:
--   - id columns are INT PRIMARY KEY → InnoDB makes the table itself a B-tree
--     on id (the clustered index)
--   - idx_user is a secondary index, so leaves store (user_id, id) → lookup
--     goes through a back-reference to the clustered tree
--   - idx_user_qty exists to show a *covering* lookup that skips the
--     clustered tree

DROP TABLE IF EXISTS orders;
DROP TABLE IF EXISTS products;
DROP TABLE IF EXISTS users;

CREATE TABLE users (
    id         INT          PRIMARY KEY,
    email      VARCHAR(60)  NOT NULL,
    country    CHAR(2)      NOT NULL,
    created_at DATE         NOT NULL,
    KEY idx_country (country)
) ENGINE=InnoDB;

CREATE TABLE products (
    id          INT          PRIMARY KEY,
    sku         VARCHAR(20)  NOT NULL,
    category    VARCHAR(20)  NOT NULL,
    price_cents INT          NOT NULL
) ENGINE=InnoDB;

CREATE TABLE orders (
    id         INT PRIMARY KEY,
    user_id    INT NOT NULL,
    product_id INT NOT NULL,
    qty        INT NOT NULL,
    ordered_at DATE NOT NULL,
    KEY idx_user     (user_id),
    KEY idx_user_qty (user_id, qty)
) ENGINE=InnoDB;

-- generate seeds via a recursive CTE (MySQL 8 supports them)
SET cte_max_recursion_depth = 1000000;

INSERT INTO users (id, email, country, created_at)
WITH RECURSIVE seq(i) AS (
    SELECT 1 UNION ALL SELECT i + 1 FROM seq WHERE i < 20000
)
SELECT i,
       CONCAT('user', i, '@mail.test'),
       ELT(1 + (i % 10),'IN','IN','US','GB','DE','FR','BR','JP','AU','SG'),
       DATE_ADD('2024-01-01', INTERVAL (i % 540) DAY)
FROM seq;

INSERT INTO products (id, sku, category, price_cents)
WITH RECURSIVE seq(i) AS (
    SELECT 1 UNION ALL SELECT i + 1 FROM seq WHERE i < 500
)
SELECT i,
       LPAD(CONCAT('SKU-', i), 8, '0'),
       ELT(1 + (i % 5), 'books','electronics','home','grocery','apparel'),
       500 + (i * 37) MOD 9500
FROM seq;

INSERT INTO orders (id, user_id, product_id, qty, ordered_at)
WITH RECURSIVE seq(i) AS (
    SELECT 1 UNION ALL SELECT i + 1 FROM seq WHERE i < 200000
)
SELECT i,
       1 + (i * 2654435761) MOD 20000,
       1 + (i MOD 500),
       IF((i MOD 7) < 2, 3, 1 + (i MOD 5)),
       DATE_ADD('2024-06-01', INTERVAL (i MOD 365) DAY)
FROM seq;

ANALYZE TABLE users, products, orders;

SELECT 'users' AS tbl, COUNT(*) AS cnt FROM users
UNION ALL SELECT 'products', COUNT(*) FROM products
UNION ALL SELECT 'orders',   COUNT(*) FROM orders;
