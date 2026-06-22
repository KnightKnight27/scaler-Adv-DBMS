-- Build the InnoDB dataset.  Run with:  mysql -uroot < setup.sql
DROP DATABASE IF EXISTS dbms_lab;
CREATE DATABASE dbms_lab;
USE dbms_lab;

CREATE TABLE students(
  id INT PRIMARY KEY,
  name VARCHAR(40),
  dept VARCHAR(4),
  join_year INT,
  INDEX idx_dept (dept)
) ENGINE=InnoDB;

CREATE TABLE enrollments(
  id INT PRIMARY KEY,
  student_id INT,
  course_id INT,
  grade INT,
  INDEX idx_student (student_id)
) ENGINE=InnoDB;

SET cte_max_recursion_depth = 300000;

INSERT INTO students
WITH RECURSIVE seq(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM seq WHERE x<20000)
SELECT x, CONCAT('student_',x), ELT(1+(x%5),'CS','EE','ME','CE','MA'), 2018+(x%6) FROM seq;

INSERT INTO enrollments
WITH RECURSIVE seq(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM seq WHERE x<200000)
SELECT x, 1+(x%20000), 1+(x%500), x%11 FROM seq;

ANALYZE TABLE students, enrollments;

-- Experiments captured in results.txt / locks.txt:
--   EXPLAIN SELECT * FROM enrollments WHERE id=12345;            -- clustered
--   EXPLAIN SELECT * FROM enrollments WHERE student_id=12345;    -- secondary + back-ref
--   EXPLAIN SELECT id,student_id FROM enrollments WHERE student_id=12345;  -- covering
--   index sizes: SELECT table_name,index_name,stat_value FROM mysql.innodb_index_stats
--                WHERE database_name='dbms_lab' AND stat_name='size';
-- gap locks (run the SELECT...FOR UPDATE in one session, inspect from another):
--   START TRANSACTION; SELECT * FROM students WHERE id BETWEEN 100 AND 110 FOR UPDATE;
--   SELECT INDEX_NAME,LOCK_TYPE,LOCK_MODE,LOCK_DATA FROM performance_schema.data_locks;
-- redo/undo:  SHOW ENGINE INNODB STATUS \G  (LOG section, History list length)
