-- Two test queries used to compare engines.
-- Q1: filter by department  (single-table scan, no join)
-- Q2: join students with their courses  (cross-table)

-- Q1
SELECT id, name, age, dept
FROM students
WHERE dept = 'CS'
ORDER BY id;

-- Q2
SELECT s.name, s.dept, c.course_name, c.grade
FROM students s
JOIN courses c ON c.student_id = s.id
ORDER BY s.id;
