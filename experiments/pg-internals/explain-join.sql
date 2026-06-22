DISCARD ALL;
\echo '--- Cold buffer run ---'
EXPLAIN (ANALYZE, BUFFERS)
SELECT s.name, d.name, e.grade
FROM students s
JOIN departments d ON s.dept_id = d.id
JOIN enrollments e ON e.student_id = s.id
WHERE d.name = 'Dept_2' AND e.grade = 'A'
LIMIT 100;

\echo '--- Warm buffer run ---'
EXPLAIN (ANALYZE, BUFFERS)
SELECT s.name, d.name, e.grade
FROM students s
JOIN departments d ON s.dept_id = d.id
JOIN enrollments e ON e.student_id = s.id
WHERE d.name = 'Dept_2' AND e.grade = 'A'
LIMIT 100;

\echo '--- pg_stats grade column ---'
SELECT attname, n_distinct, most_common_vals, most_common_freqs, correlation
FROM pg_stats WHERE tablename = 'enrollments' AND attname = 'grade';
