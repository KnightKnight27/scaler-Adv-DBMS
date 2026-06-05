-- =============================================================
-- Lab 4: SQLite3 Internal Structure Analysis
-- schema.sql — Table definition and sample data
-- =============================================================

-- Drop existing table if re-running
DROP TABLE IF EXISTS students;

-- Create students table
-- INTEGER PRIMARY KEY AUTOINCREMENT makes id an alias for rowid
-- and creates the sqlite_sequence tracking table automatically.
CREATE TABLE students (
    id     INTEGER PRIMARY KEY AUTOINCREMENT,
    name   TEXT    NOT NULL,
    dept   TEXT    NOT NULL,
    grade  TEXT    NOT NULL,
    marks  INTEGER NOT NULL
);

-- Insert 8 sample records across 4 departments and 3 grade levels
INSERT INTO students (name, dept, grade, marks) VALUES
    ('Alice Johnson',  'Computer Science', 'A', 92),
    ('Bob Martinez',   'Electronics',      'B', 78),
    ('Carol Williams', 'Mechanical',        'A', 88),
    ('David Lee',      'Computer Science', 'C', 65),
    ('Eva Chen',       'Electronics',      'A', 95),
    ('Frank Brown',    'Civil',             'B', 74),
    ('Grace Kim',      'Computer Science', 'A', 91),
    ('Henry Adams',    'Mechanical',        'B', 81);

-- Verify insertion
SELECT * FROM students;
