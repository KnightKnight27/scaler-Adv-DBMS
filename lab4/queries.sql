-- =============================================================
-- Lab 4: SQLite3 Internal Structure Analysis
-- queries.sql — PRAGMA introspection and metadata queries
-- Run: sqlite3 students.db < queries.sql
-- =============================================================

-- ---------------------------------------------------------------
-- Part 2: Database Metadata
-- ---------------------------------------------------------------

-- Page configuration
PRAGMA page_size;        -- Expected: 4096
PRAGMA page_count;       -- Expected: 3
PRAGMA encoding;         -- Expected: UTF-8
PRAGMA freelist_count;   -- Expected: 0 (no free pages)

-- Schema catalog (formerly sqlite_master)
SELECT COUNT(*) AS schema_row_count FROM sqlite_master;

-- Inspect full schema catalog
SELECT name, rootpage, sql
FROM sqlite_master
ORDER BY rootpage;

-- Rootpage lookup for specific tables
SELECT name, rootpage
FROM sqlite_master
WHERE name IN ('students', 'sqlite_sequence');

-- ---------------------------------------------------------------
-- Part 6: Record-level queries (cross-check with hex decoded data)
-- ---------------------------------------------------------------

-- All rows ordered by id (matches cell pointer order)
SELECT id, name, dept, grade, marks
FROM students
ORDER BY id;

-- Payload size approximation per row
-- (SQLite does not expose raw payload size via SQL; this shows text lengths)
SELECT
    id,
    name,
    length(name)  AS name_len,
    length(dept)  AS dept_len,
    length(grade) AS grade_len,
    marks
FROM students
ORDER BY id;

-- ---------------------------------------------------------------
-- Part 7: Schema storage verification
-- ---------------------------------------------------------------

-- Confirm sqlite_sequence counter after 8 inserts
SELECT * FROM sqlite_sequence;   -- Expected: students | 8

-- ---------------------------------------------------------------
-- Additional: Database file integrity check
-- ---------------------------------------------------------------
PRAGMA integrity_check;   -- Expected: ok
