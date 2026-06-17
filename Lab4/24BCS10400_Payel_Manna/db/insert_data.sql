WITH RECURSIVE cnt(x) AS (
    SELECT 1
    UNION ALL
    SELECT x+1 FROM cnt WHERE x < 300
)
INSERT INTO users(name, age)
SELECT
    'user_' || x,
    20 + (x % 10)
FROM cnt;

WITH RECURSIVE cnt2(x) AS (
    SELECT 1
    UNION ALL
    SELECT x+1 FROM cnt2 WHERE x < 200
)
INSERT INTO products(title, price)
SELECT
    'product_' || x,
    100 + x
FROM cnt2;
