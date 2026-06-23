-- B+Tree index: create a table, build an index, and query by key.
CREATE TABLE users (id INT, name VARCHAR(32));
INSERT INTO users VALUES (1, 'Alice');
INSERT INTO users VALUES (2, 'Bob');
INSERT INTO users VALUES (3, 'Carol');
CREATE INDEX ix_users_id ON users(id);
ANALYZE users;
-- The planner uses the index for this selective equality lookup.
EXPLAIN SELECT name FROM users WHERE id = 2;
SELECT name FROM users WHERE id = 2;
