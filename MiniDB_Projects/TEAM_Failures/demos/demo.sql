-- ===========================================================================
-- demo.sql  --  End-to-end tour of MiniDB's SQL features.
-- Run with:   ./build/minidb /tmp/minidb_demo < demos/demo.sql
-- (Delete /tmp/minidb_demo.* first for a clean run.)
-- ===========================================================================

-- 1. DDL: create tables.  `id INT PRIMARY KEY` auto-creates a B+ Tree index.
CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR, age INT);
CREATE TABLE orders (oid INT PRIMARY KEY, uid INT, item VARCHAR);

-- 2. INSERT some rows.
INSERT INTO users VALUES (1, 'alice', 30);
INSERT INTO users VALUES (2, 'bob', 25);
INSERT INTO users VALUES (3, 'carol', 40);
INSERT INTO users VALUES (4, 'dave', 35);
INSERT INTO orders VALUES (10, 1, 'book');
INSERT INTO orders VALUES (11, 1, 'pen');
INSERT INTO orders VALUES (12, 3, 'lamp');

-- 3. Full scan (the optimizer chooses SeqScan).
SELECT * FROM users;

-- 4. Filter on a non-indexed column -> still a SeqScan, but fewer rows pass.
SELECT name, age FROM users WHERE age > 30;

-- 5. Filter on the PRIMARY KEY -> the optimizer chooses an IndexScan (look at
--    the "-- plan:" line printed above the table).
SELECT * FROM users WHERE id = 3;

-- 6. JOIN: each user with their orders.  The optimizer probes the PK index on
--    the inner table (index nested-loop join).
SELECT users.name, orders.item FROM users JOIN orders ON users.id = orders.uid;

-- 7. DELETE and re-scan.
DELETE FROM users WHERE id = 2;
SELECT * FROM users;

-- 8. Secondary index on a non-key column, then a lookup that uses it.
CREATE INDEX users_age ON users (age);
SELECT name FROM users WHERE age = 40;
