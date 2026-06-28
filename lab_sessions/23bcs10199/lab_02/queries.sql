-- Lab 2 — SQLite3 Internals: PRAGMA Exploration + DB Comparison Queries
-- Student : Indrajeet Yadav | Roll No: 23BCS10199
--
-- Usage:
--   sqlite3 students.db < queries.sql
-- Or paste interactively:
--   sqlite3 students.db

-- ─────────────────────────────────────────────────────────────
-- Section 1: SQLite PRAGMA introspection
-- ─────────────────────────────────────────────────────────────

-- Show the page size (bytes). Default = 4096, matching the Linux page size.
-- This means one SQLite page = one OS virtual memory page = one I/O unit.
PRAGMA page_size;

-- Switch to WAL (Write-Ahead Log) mode for better concurrent read performance.
-- In DELETE (default) mode, readers block writers. WAL lets readers see a
-- consistent snapshot while a writer appends to the WAL file.
PRAGMA journal_mode = WAL;

-- Show total allocated pages in the database file.
-- file_size_bytes = page_size * page_count
PRAGMA page_count;

-- In-process page cache size. Negative = KiB; positive = number of pages.
-- -8000 means 8 MB. Larger cache → fewer pread() syscalls for hot pages.
PRAGMA cache_size = -8000;
PRAGMA cache_size;

-- Enable memory-mapped I/O for the first 256 MB of the database file.
-- Reads in this range become memory accesses (via page fault) rather than
-- pread() syscalls. Best for sequential access patterns.
PRAGMA mmap_size = 268435456;
PRAGMA mmap_size;

-- ─────────────────────────────────────────────────────────────
-- Section 2: Create schema and load sample data
-- ─────────────────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS students (
    id       INTEGER PRIMARY KEY,    -- SQLite stores PRIMARY KEY as rowid alias
    name     TEXT    NOT NULL,
    roll     TEXT    UNIQUE NOT NULL,
    gpa      REAL    DEFAULT 0.0,
    country  TEXT    DEFAULT 'India'
);

CREATE TABLE IF NOT EXISTS courses (
    course_id   INTEGER PRIMARY KEY,
    course_name TEXT NOT NULL,
    credits     INTEGER NOT NULL DEFAULT 3
);

CREATE TABLE IF NOT EXISTS enrollments (
    student_id INTEGER REFERENCES students(id),
    course_id  INTEGER REFERENCES courses(course_id),
    grade      REAL,
    semester   TEXT,
    PRIMARY KEY (student_id, course_id)
);

-- Populate students
INSERT OR IGNORE INTO students VALUES
    (1, 'Indrajeet Yadav', '23BCS10199', 9.1, 'India'),
    (2, 'Alice Sharma',    '23BCS10200', 8.7, 'India'),
    (3, 'Bob Verma',       '23BCS10201', 7.5, 'India'),
    (4, 'Carol Singh',     '23BCS10202', 9.4, 'India'),
    (5, 'Dave Kumar',      '23BCS10203', 6.8, 'India'),
    (6, 'Eve Patel',       '23BCS10204', 8.2, 'India'),
    (7, 'Frank Nair',      '23BCS10205', 7.9, 'India');

-- Populate courses
INSERT OR IGNORE INTO courses VALUES
    (101, 'Advanced DBMS',         4),
    (102, 'Operating Systems',      4),
    (103, 'Algorithms',             3),
    (104, 'Computer Networks',      3),
    (105, 'System Design',          3);

-- Populate enrollments
INSERT OR IGNORE INTO enrollments VALUES
    (1, 101, 9.5, '2024-1'), (1, 102, 9.0, '2024-1'), (1, 103, 8.8, '2024-1'),
    (2, 101, 8.5, '2024-1'), (2, 104, 9.1, '2024-1'),
    (3, 102, 7.0, '2024-1'), (3, 103, 8.0, '2024-1'), (3, 105, 7.5, '2024-1'),
    (4, 101, 9.8, '2024-1'), (4, 103, 9.2, '2024-1'), (4, 104, 9.5, '2024-1'),
    (5, 102, 6.5, '2024-1'), (5, 105, 7.0, '2024-1'),
    (6, 101, 8.0, '2024-1'), (6, 102, 8.5, '2024-1'),
    (7, 103, 8.2, '2024-1'), (7, 104, 7.8, '2024-1');

-- ─────────────────────────────────────────────────────────────
-- Section 3: PRAGMA inspection after data load
-- ─────────────────────────────────────────────────────────────

-- Page count increases as B-tree pages fill up
PRAGMA page_count;

-- Inspect column metadata for the students table
PRAGMA table_info(students);
-- cid | name | type    | notnull | dflt_value | pk
--  0  | id   | INTEGER |    0    |            |  1
--  1  | name | TEXT    |    1    |            |  0
-- ...

-- List indexes on the students table
PRAGMA index_list(students);

-- Show index details (which columns the index covers)
PRAGMA index_info(sqlite_autoindex_students_1);

-- Validate all pages of the database (B-tree consistency check)
PRAGMA integrity_check;

-- List all databases attached to this connection (main + any ATTACHed)
PRAGMA database_list;

-- Show SQLite compile options (which features were compiled in)
PRAGMA compile_options;

-- ─────────────────────────────────────────────────────────────
-- Section 4: Query the data — exercises similar to DB comparison
-- ─────────────────────────────────────────────────────────────

-- Q1: Students with GPA above average, sorted descending
SELECT name, roll, gpa
FROM students
WHERE gpa > (SELECT AVG(gpa) FROM students)
ORDER BY gpa DESC;

-- Q2: Number of enrollments per student, with student name
SELECT s.name, s.roll, COUNT(e.course_id) AS num_courses, AVG(e.grade) AS avg_grade
FROM students s
LEFT JOIN enrollments e ON s.id = e.student_id
GROUP BY s.id, s.name, s.roll
ORDER BY avg_grade DESC;

-- Q3: Point lookup — all courses for student_id = 1 (uses index)
-- SQLite will use the PRIMARY KEY B-tree on enrollments to look up student_id=1
EXPLAIN QUERY PLAN
SELECT c.course_name, c.credits, e.grade
FROM enrollments e JOIN courses c ON e.course_id = c.course_id
WHERE e.student_id = 1
ORDER BY e.grade DESC;

-- Run the actual query
SELECT c.course_name, c.credits, e.grade
FROM enrollments e JOIN courses c ON e.course_id = c.course_id
WHERE e.student_id = 1
ORDER BY e.grade DESC;

-- Q4: Course popularity — number of enrolled students per course
SELECT c.course_name, COUNT(e.student_id) AS enrolled, AVG(e.grade) AS avg_grade
FROM courses c
LEFT JOIN enrollments e ON c.course_id = e.course_id
GROUP BY c.course_id, c.course_name
ORDER BY enrolled DESC;

-- Q5: Window function — rank students by GPA within each country
-- (SQLite 3.25+ supports window functions)
SELECT name, country, gpa,
       RANK() OVER (PARTITION BY country ORDER BY gpa DESC) AS country_rank,
       ROUND(gpa - AVG(gpa) OVER (PARTITION BY country), 2) AS vs_country_avg
FROM students
ORDER BY country, country_rank;

-- Q6: Running total of credits per student (cumulative)
SELECT s.name, c.course_name, c.credits,
       SUM(c.credits) OVER (
           PARTITION BY e.student_id
           ORDER BY c.course_id
           ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
       ) AS cumulative_credits
FROM enrollments e
JOIN students s ON e.student_id = s.id
JOIN courses  c ON e.course_id  = c.course_id
ORDER BY s.name, c.course_id;

-- ─────────────────────────────────────────────────────────────
-- Section 5: EXPLAIN QUERY PLAN — see SQLite's access strategy
-- ─────────────────────────────────────────────────────────────

-- Full table scan (no index on gpa column)
EXPLAIN QUERY PLAN
SELECT * FROM students WHERE gpa > 8.0;
-- SCAN students  ← sequential scan, no index

-- Create an index on gpa and observe the plan change
CREATE INDEX IF NOT EXISTS idx_students_gpa ON students(gpa);

EXPLAIN QUERY PLAN
SELECT * FROM students WHERE gpa > 8.0;
-- SEARCH students USING INDEX idx_students_gpa (gpa>?)  ← index used

-- Primary key lookup (B-tree dive — fastest possible access)
EXPLAIN QUERY PLAN
SELECT * FROM students WHERE id = 3;
-- SEARCH students USING INTEGER PRIMARY KEY (rowid=?)

-- ─────────────────────────────────────────────────────────────
-- Section 6: WAL checkpoint and cleanup
-- ─────────────────────────────────────────────────────────────

-- Flush the WAL file back into the main database file.
-- After this, the .db file contains all committed data and .db-wal is empty.
PRAGMA wal_checkpoint(FULL);

-- Reclaim freed pages (pages left by DELETEs / DROPs)
-- VACUUM rewrites the entire database into a new file with no gaps.
VACUUM;

-- Final page count after vacuum
PRAGMA page_count;

-- ─────────────────────────────────────────────────────────────
-- Summary of what each PRAGMA teaches us
-- ─────────────────────────────────────────────────────────────

-- PRAGMA page_size    → Every SQLite read/write is in page_size chunks.
--                       Matches OS page size (4096) for zero padding waste.
-- PRAGMA mmap_size    → Larger value → more file mapped via mmap() → fewer
--                       pread() syscalls for sequential access patterns.
-- PRAGMA journal_mode → WAL mode separates read and write paths: readers see
--                       a snapshot from before the WAL was written; writers
--                       append to the WAL file without touching the main db.
--                       Much better for read-heavy concurrent workloads.
-- PRAGMA integrity_check → Walks every B-tree page and verifies structure.
--                          Equivalent to PostgreSQL's pg_checksums + pg_dump verify.
-- EXPLAIN QUERY PLAN → Shows which index (if any) SQLite uses, and whether
--                      it's SCAN (full table) or SEARCH (index lookup).
--                      SEARCH = B-tree dive = O(log n) page reads.
--                      SCAN   = sequential   = O(n) page reads.
