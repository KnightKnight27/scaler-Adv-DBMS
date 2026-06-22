-- MiniDB end-to-end SQL demo
CREATE TABLE users (id INT, name VARCHAR(16), age INT, PRIMARY KEY (id));
INSERT INTO users VALUES (1, 'alice', 30);
INSERT INTO users VALUES (2, 'bob', 25);
INSERT INTO users VALUES (3, 'carol', 40);
INSERT INTO users VALUES (4, 'dave', 25);
INSERT INTO users VALUES (5, 'erin', 33);
INSERT INTO users VALUES (6, 'frank', 28);
INSERT INTO users VALUES (7, 'grace', 51);
INSERT INTO users VALUES (8, 'heidi', 22);
INSERT INTO users VALUES (9, 'ivan', 45);
INSERT INTO users VALUES (10, 'judy', 38);

SELECT * FROM users WHERE age > 35;

-- Optimizer: equality on the PRIMARY KEY uses the B+Tree index ...
EXPLAIN SELECT * FROM users WHERE id = 7;
-- ... while a predicate on a non-indexed column falls back to a sequential scan
EXPLAIN SELECT * FROM users WHERE age = 25;

SELECT name, age FROM users WHERE id = 7;

CREATE TABLE orders (oid INT, uid INT, amount INT, PRIMARY KEY (oid));
INSERT INTO orders VALUES (100, 1, 50);
INSERT INTO orders VALUES (101, 2, 75);
INSERT INTO orders VALUES (102, 1, 20);
INSERT INTO orders VALUES (103, 9, 99);

-- Join uses an index-nested-loop (inner 'users' keyed by its PK index)
SELECT users.name, orders.amount FROM orders JOIN users ON orders.uid = users.id;
EXPLAIN SELECT users.name, orders.amount FROM orders JOIN users ON orders.uid = users.id;

DELETE FROM users WHERE id = 4;
SELECT id, name FROM users WHERE age > 30;
.tables
.quit
