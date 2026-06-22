-- Q1: JOIN
SELECT s.name, c.course_name, c.grade
FROM students s
JOIN courses c ON s.id = c.student_id;

-- Q2: COUNT
SELECT COUNT(*) FROM students WHERE dept = 'CS';
