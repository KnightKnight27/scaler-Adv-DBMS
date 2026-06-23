-- MiniDB end-to-end demo. Run with:   ./minidb /tmp/demo < demo.sql
-- (or paste interactively). Shows storage, indexing, the optimizer's scan
-- choice, joins, COUNT, an LSM table, and transactions.

-- 1. A heap (B+ tree indexed) table.
CREATE TABLE users (id INTEGER PRIMARY KEY, name VARCHAR, age INTEGER);
INSERT INTO users VALUES (1, 'alice', 30);
INSERT INTO users VALUES (2, 'bob', 25);
INSERT INTO users VALUES (3, 'carol', 40);
INSERT INTO users VALUES (4, 'dave', 35);

-- 2. Show the optimizer choosing access paths.
.explain on
SELECT id, name FROM users WHERE id = 3;          -- selective PK -> IndexScan
SELECT COUNT(*) FROM users WHERE age >= 1;         -- non-selective -> SeqScan
.explain off

-- 3. Projection + filter.
SELECT name, age FROM users WHERE age >= 30;

-- 4. An LSM-backed table (Track C) — same SQL surface.
CREATE TABLE events (id INTEGER PRIMARY KEY, kind VARCHAR) USING LSM;
INSERT INTO events VALUES (1, 'login');
INSERT INTO events VALUES (2, 'click');
INSERT INTO events VALUES (3, 'logout');

-- 5. Join an LSM table with a heap table.
SELECT users.name, events.kind FROM users JOIN events ON users.id = events.id;

-- 6. Transactions: rollback undoes changes.
BEGIN;
INSERT INTO users VALUES (99, 'temp', 1);
SELECT COUNT(*) FROM users;     -- 5
ROLLBACK;
SELECT COUNT(*) FROM users;     -- back to 4

-- 7. Delete + recount.
DELETE FROM users WHERE id = 2;
SELECT COUNT(*) FROM users;     -- 3

.tables
.exit
