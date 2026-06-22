-- Build the SQLite side of the comparison.
-- Run with:  sqlite3 lab.db < sqlite_setup.sql
-- The Postgres side reuses ../PostgreSQL_Internals/setup.sql (same schema, 20k/200k rows).
PRAGMA page_size=4096;

CREATE TABLE students(
    id INTEGER PRIMARY KEY,
    name TEXT,
    dept TEXT,
    join_year INT
);
CREATE TABLE enrollments(
    id INTEGER PRIMARY KEY,
    student_id INT,
    course_id INT,
    grade INT
);

WITH RECURSIVE seq(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM seq WHERE x<20000)
INSERT INTO students
SELECT x, 'student_'||x,
       (CASE x%5 WHEN 0 THEN 'CS' WHEN 1 THEN 'EE' WHEN 2 THEN 'ME' WHEN 3 THEN 'CE' ELSE 'MA' END),
       2018+(x%6)
FROM seq;

WITH RECURSIVE seq(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM seq WHERE x<200000)
INSERT INTO enrollments
SELECT x, 1+(x%20000), 1+(x%500), x%11 FROM seq;

CREATE INDEX idx_enr_student ON enrollments(student_id);
CREATE INDEX idx_students_dept ON students(dept);

-- inspect storage:
--   PRAGMA page_size; PRAGMA page_count; PRAGMA journal_mode;
--   SELECT name, SUM(pgsize), COUNT(*) FROM dbstat GROUP BY name;
-- query plans:
--   EXPLAIN QUERY PLAN SELECT * FROM enrollments WHERE student_id=12345;
--   EXPLAIN QUERY PLAN SELECT * FROM enrollments WHERE grade=7;
