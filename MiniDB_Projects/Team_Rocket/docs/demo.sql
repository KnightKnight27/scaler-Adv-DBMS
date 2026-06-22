-- MiniDB end-to-end demo. Run with:  ./build/minidb run < docs/demo.sql
CREATE TABLE users (id INT, name TEXT, age INT);
CREATE TABLE orders (id INT, user_id INT, total INT);

INSERT INTO users VALUES (1, 'alice', 30);
INSERT INTO users VALUES (2, 'bob', 25);
INSERT INTO users VALUES (3, 'carol', 41);

INSERT INTO orders VALUES (100, 1, 250);
INSERT INTO orders VALUES (101, 1, 80);
INSERT INTO orders VALUES (102, 3, 500);

-- sequential scan with a filter
SELECT * FROM users WHERE age > 28;

-- index scan: equality on the indexed primary column hits the B+ Tree
SELECT * FROM users WHERE id = 2;

-- projection
SELECT name, age FROM users;

-- inner join; the optimizer picks the join order by estimated cardinality
SELECT * FROM users JOIN orders ON users.id = orders.user_id;

-- transaction + delete + durability through a simulated crash
BEGIN;
DELETE FROM orders WHERE id = 101;
COMMIT;
SELECT * FROM orders;
CRASH;
SELECT * FROM orders;

-- rollback: an aborted transaction leaves no trace
BEGIN;
INSERT INTO users VALUES (9, 'temp', 99);
SELECT * FROM users;
ABORT;
SELECT * FROM users;
