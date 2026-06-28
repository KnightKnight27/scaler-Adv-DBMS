CREATE TABLE users (
  id    INT PRIMARY KEY,
  name  TEXT,
  email TEXT,
  age   INT,
  city  TEXT
);

INSERT INTO users VALUES
  (1, 'manasvi', 'manasvi.24bcs10406@sst.scaler.com', 19, 'Bangalore'),
  (2, 'aman',    'aman.24bcs10183@sst.scaler.com',    20, 'Mumbai'),
  (3, 'kushal',  'kushal.24bcs10123@sst.scaler.com',  19, 'Ahmedabad'),
  (4, 'akshat',  'akshat.24bcs10060@sst.scaler.com',  20, 'Delhi');

CREATE INDEX idx_users_age  ON users (age);
CREATE INDEX idx_users_city ON users (city);

CREATE TABLE products (
  id       INT PRIMARY KEY,
  name     TEXT,
  category TEXT,
  price    INT,
  stock    INT
);

INSERT INTO products VALUES
  (101, 'Notebook',     'stationery',   150,  120),
  (102, 'Pen',          'stationery',    20,  500),
  (103, 'Backpack',     'bags',        1200,   40),
  (104, 'Headphones',   'electronics', 2500,   15),
  (105, 'USB Cable',    'electronics',  299,   80),
  (106, 'Coffee Mug',   'kitchen',      350,   60),
  (107, 'Water Bottle', 'kitchen',      450,   75),
  (108, 'Lamp',         'home',        1800,   20),
  (109, 'Cushion',      'home',         800,   30),
  (110, 'Tea Box',      'kitchen',      550,   45);

CREATE INDEX idx_products_category ON products (category);
CREATE INDEX idx_products_price    ON products (price);

CREATE TABLE orders (
  id         INT PRIMARY KEY,
  user_id    INT,
  product_id INT,
  qty        INT,
  status     TEXT,
  placed_on  TEXT
);

INSERT INTO orders VALUES
  (1001, 1, 104,  1, 'paid',      '2026-06-01'),
  (1002, 2, 102,  5, 'shipped',   '2026-06-03'),
  (1003, 3, 101,  3, 'paid',      '2026-06-04'),
  (1004, 1, 105,  2, 'cancelled', '2026-06-05'),
  (1005, 4, 106,  2, 'paid',      '2026-06-06'),
  (1006, 2, 108,  1, 'pending',   '2026-06-08'),
  (1007, 3, 103,  1, 'shipped',   '2026-06-09'),
  (1008, 4, 110,  4, 'paid',      '2026-06-10'),
  (1009, 1, 107,  1, 'paid',      '2026-06-11'),
  (1010, 3, 104,  1, 'pending',   '2026-06-12'),
  (1011, 1, 109,  2, 'shipped',   '2026-06-13'),
  (1012, 2, 102, 10, 'paid',      '2026-06-14'),
  (1013, 4, 105,  1, 'paid',      '2026-06-15'),
  (1014, 3, 101,  2, 'shipped',   '2026-06-16'),
  (1015, 2, 108,  1, 'paid',      '2026-06-17');

CREATE INDEX idx_orders_user    ON orders (user_id);
CREATE INDEX idx_orders_product ON orders (product_id);
CREATE INDEX idx_orders_status  ON orders (status);

.tables

SELECT * FROM users;
SELECT * FROM products;
SELECT * FROM orders;

SELECT name, city, age FROM users WHERE age > 19;
SELECT name, category, price FROM products WHERE category = 'electronics';

SELECT u.name, p.name, o.qty, o.status
FROM orders o
JOIN users u    ON u.id = o.user_id
JOIN products p ON p.id = o.product_id;

EXPLAIN SELECT * FROM users WHERE age = 19;
EXPLAIN SELECT * FROM users WHERE age > 18;

EXPLAIN
SELECT u.name, p.name, o.qty
FROM orders o
JOIN users u    ON u.id = o.user_id
JOIN products p ON p.id = o.product_id
WHERE o.status = 'paid';

.stats

.exit
