CREATE TABLE emp (id INT PRIMARY KEY, name STRING, dept INT INDEX);
INSERT INTO emp (id, name, dept) VALUES (1, 'Alice', 10);
INSERT INTO emp (id, name, dept) VALUES (2, 'Bob', 20);
SELECT * FROM emp WHERE id = 1;
SELECT * FROM emp WHERE dept = 10;
SELECT * FROM emp;
.stats
.quit
