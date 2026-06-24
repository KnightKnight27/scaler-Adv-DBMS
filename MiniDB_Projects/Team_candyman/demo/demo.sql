-- MiniDB feature tour. Run on either engine:
--   go run ./cmd/minidb --data ./data --engine heap < demo/demo.sql
--   go run ./cmd/minidb --data ./data2 --engine lsm  < demo/demo.sql

-- 1. DDL: tables require a PRIMARY KEY (it backs the primary index).
CREATE TABLE users (id INT PRIMARY KEY, name TEXT, dept TEXT);
CREATE TABLE orders (oid INT PRIMARY KEY, uid INT, total INT);

-- 2. INSERT (single and multi-row).
INSERT INTO users VALUES (1, 'alice', 'eng');
INSERT INTO users VALUES (2, 'bob', 'eng'), (3, 'carol', 'sales'), (4, 'dan', 'sales');
INSERT INTO orders VALUES (10, 1, 100), (11, 1, 250), (12, 2, 75), (13, 3, 500), (14, 4, 40);

-- 3. SELECT with a WHERE predicate.
SELECT id, name, dept FROM users WHERE dept = 'eng';

-- 4. Primary-key equality uses the index. EXPLAIN shows the chosen plan.
EXPLAIN SELECT name FROM users WHERE id = 3;
SELECT name FROM users WHERE id = 3;

-- 5. A non-key predicate falls back to a sequential scan.
EXPLAIN SELECT name FROM users WHERE name = 'bob';

-- 6. JOIN: users to their orders, filtered.
EXPLAIN SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.uid WHERE o.total > 90;
SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.uid WHERE o.total > 90;

-- 7. Aggregation with GROUP BY.
SELECT dept, COUNT(*), SUM(id), MAX(name) FROM users GROUP BY dept;

-- 8. DELETE, then confirm.
DELETE FROM users WHERE id = 4;
SELECT id, name FROM users;

-- 9. Explicit transaction with ROLLBACK (changes are undone).
BEGIN;
INSERT INTO users VALUES (9, 'temp', 'tmp');
SELECT id, name FROM users WHERE id = 9;
ROLLBACK;
SELECT id, name FROM users WHERE id = 9;
