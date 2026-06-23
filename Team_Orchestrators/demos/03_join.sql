-- Inner equi-join across two tables, with qualified columns and ORDER BY.
CREATE TABLE customers (id INT, name VARCHAR(32));
CREATE TABLE orders (oid INT, cid INT, amount INT);
INSERT INTO customers VALUES (1, 'Alice');
INSERT INTO customers VALUES (2, 'Bob');
INSERT INTO orders VALUES (10, 1, 100);
INSERT INTO orders VALUES (11, 1, 250);
INSERT INTO orders VALUES (12, 2, 75);
SELECT customers.name, orders.amount
  FROM customers INNER JOIN orders ON customers.id = orders.cid
  ORDER BY orders.amount;
