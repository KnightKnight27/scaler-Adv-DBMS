-- MiniDB basic feature demo. Run with:  ./build/minidb demo < demos/demo_basic.sql
-- (Statements are one per line; trailing semicolons are optional.)

CREATE TABLE users (id INT, name VARCHAR, age INT, PRIMARY KEY (id))
CREATE TABLE orders (oid INT, uid INT, amount INT, PRIMARY KEY (oid))

INSERT INTO users VALUES (1, 'alice', 30)
INSERT INTO users VALUES (2, 'bob', 25)
INSERT INTO users VALUES (3, 'carol', 41)
INSERT INTO users VALUES (4, 'dan', 19)

INSERT INTO orders VALUES (101, 1, 500)
INSERT INTO orders VALUES (102, 1, 250)
INSERT INTO orders VALUES (103, 3, 900)

-- Full table scan
SELECT * FROM users

-- Primary-key point lookup -> optimizer picks INDEX_POINT
SELECT id, name FROM users WHERE id = 3

-- Filter on a non-key column -> optimizer picks SEQ_SCAN
SELECT name, age FROM users WHERE age > 28

-- Join users with their orders (nested-loop join, smaller relation outer)
SELECT users.name, orders.amount FROM users JOIN orders ON users.id = orders.uid

-- Delete and confirm
DELETE FROM users WHERE id = 4
SELECT * FROM users

.tables
.exit
