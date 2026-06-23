SHOW TABLES;

CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(16), age INT);
CREATE TABLE orders (id INT PRIMARY KEY, user_id INT, total INT);

SHOW TABLES;

INSERT INTO users VALUES
  (1, 'alice', 30),
  (2, 'bob', 25),
  (3, 'carol', 41);

INSERT INTO orders VALUES
  (10, 1, 99),
  (11, 2, 150),
  (12, 2, 80);

SELECT * FROM users;
SELECT name FROM users WHERE id = 2;
SELECT users.id, orders.total
FROM users JOIN orders ON users.id = orders.user_id
WHERE orders.total >= 100;

DELETE FROM users WHERE id = 3;
SELECT * FROM users WHERE id = 3;

BEGIN;
COMMIT;
