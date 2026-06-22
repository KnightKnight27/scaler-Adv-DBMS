\timing on
CREATE EXTENSION IF NOT EXISTS pageinspect;
CREATE EXTENSION IF NOT EXISTS pg_buffercache;

\echo '==================== Q1: 3-table join, EXPLAIN ANALYZE + BUFFERS ===================='
EXPLAIN (ANALYZE, BUFFERS, COSTS)
SELECT s.dept, count(*) AS n, avg(e.grade)::numeric(4,2) AS avg_grade
FROM students s
JOIN enrollments e ON e.student_id = s.id
JOIN courses c ON c.id = e.course_id
WHERE s.dept = 'CS' AND c.credits = 4
GROUP BY s.dept;

\echo '==================== Q2: single-row index lookup vs seq scan ===================='
EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM enrollments WHERE student_id = 12345;
EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM enrollments WHERE grade = 7;

\echo '==================== Statistics the planner used (pg_stats) ===================='
SELECT attname, n_distinct, null_frac, round(avg_width,1) AS avg_width,
       most_common_vals
FROM pg_stats
WHERE tablename = 'enrollments' AND attname IN ('student_id','grade','course_id');

SELECT relname, reltuples::bigint AS est_rows, relpages AS pages_8kb
FROM pg_class WHERE relname IN ('students','courses','enrollments');

\echo '==================== MVCC: xmin / xmax / ctid across an UPDATE ===================='
SELECT id, ctid, xmin, xmax FROM students WHERE id = 1;
BEGIN;
UPDATE students SET dept = 'CS' WHERE id = 1;
SELECT id, ctid, xmin, xmax FROM students WHERE id = 1;   -- new version, new ctid
COMMIT;
SELECT id, ctid, xmin, xmax FROM students WHERE id = 1;   -- committed version

\echo '==================== Heap page layout (pageinspect) ===================='
SELECT lp, lp_off, lp_len, t_xmin, t_xmax, t_ctid
FROM heap_page_items(get_raw_page('students', 0))
WHERE lp <= 5;

SELECT * FROM page_header(get_raw_page('students', 0));

\echo '==================== B-tree index page layout (pageinspect) ===================='
SELECT * FROM bt_metap('students_pkey');
SELECT itemoffset, ctid, itemlen, data
FROM bt_page_items('students_pkey', 1) LIMIT 5;

\echo '==================== Dead tuples + VACUUM ===================='
UPDATE enrollments SET grade = grade WHERE student_id < 2000;  -- create dead tuples
SELECT n_live_tup, n_dead_tup, last_autovacuum
FROM pg_stat_user_tables WHERE relname = 'enrollments';
VACUUM (VERBOSE) enrollments;
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname = 'enrollments';

\echo '==================== WAL state ===================='
SELECT pg_current_wal_lsn() AS current_lsn, pg_walfile_name(pg_current_wal_lsn()) AS walfile;
SHOW wal_level;
SHOW fsync;
SHOW synchronous_commit;

\echo '==================== Buffer cache contents (shared_buffers) ===================='
SHOW shared_buffers;
SELECT c.relname, count(*) AS buffers, pg_size_pretty(count(*) * 8192) AS cached
FROM pg_buffercache b JOIN pg_class c ON b.relfilenode = pg_relation_filenode(c.oid)
WHERE c.relname IN ('students','courses','enrollments','students_pkey','idx_enr_student')
GROUP BY c.relname ORDER BY buffers DESC;
