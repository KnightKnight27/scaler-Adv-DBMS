CREATE TABLE users (id INT, name VARCHAR(16), age INT, PRIMARY KEY (id));
INSERT INTO users VALUES (1, 'alice', 30);
INSERT INTO users VALUES (2, 'bob', 25);
INSERT INTO users VALUES (3, 'carol', 40);
INSERT INTO users VALUES (4, 'dave', 25);
SELECT * FROM users;
SELECT name, age FROM users WHERE age > 28;
EXPLAIN SELECT * FROM users WHERE id = 3;
EXPLAIN SELECT * FROM users WHERE age = 25;
SELECT * FROM users WHERE id = 3;
CREATE TABLE orders (oid INT, uid INT, amount INT, PRIMARY KEY (oid));
INSERT INTO orders VALUES (100, 1, 50);
INSERT INTO orders VALUES (101, 2, 75);
INSERT INTO orders VALUES (102, 1, 20);
SELECT users.name, orders.amount FROM orders JOIN users ON orders.uid = users.id;
EXPLAIN SELECT users.name, orders.amount FROM orders JOIN users ON orders.uid = users.id;
DELETE FROM users WHERE id = 4;
SELECT * FROM users;
.tables
.quit