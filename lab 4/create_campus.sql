-- ============================================================
-- Lab 4 - SQLite Hex & B-Tree Analysis
-- Campus Student Database Schema and Sample Data
-- ============================================================

-- Drop tables if they exist (for re-runs)
DROP TABLE IF EXISTS students;

-- ─── Table Definition ────────────────────────────────────────
-- SQLite stores this table internally as a B-Tree (leaf table).
-- The INTEGER PRIMARY KEY becomes the rowid — no extra index needed.
CREATE TABLE students (
    id         INTEGER PRIMARY KEY,   -- rowid alias; B-Tree key
    name       TEXT    NOT NULL,
    grade      INTEGER,
    department TEXT
);

-- ─── Index Definition ────────────────────────────────────────
-- Creates a separate B-Tree index page on the 'grade' column.
-- Speeds up range queries like: WHERE grade BETWEEN 80 AND 90
CREATE INDEX idx_students_grade
ON students(grade);

-- ─── Sample Data ─────────────────────────────────────────────
-- 15 realistic student records across 5 departments
INSERT INTO students (name, grade, department) VALUES
    ('Alice',  91, 'CSE'),
    ('Bob',    85, 'ISE'),
    ('Carol',  88, 'ECE'),
    ('David',  76, 'ME'),
    ('Eva',    95, 'CSE'),
    ('Frank',  82, 'EEE'),
    ('Grace',  89, 'ISE'),
    ('Helen',  92, 'ECE'),
    ('Ian',    80, 'CSE'),
    ('Jack',   87, 'ME'),
    ('Kevin',  78, 'EEE'),
    ('Luna',   90, 'CSE'),
    ('Mia',    84, 'ISE'),
    ('Noah',   93, 'ECE'),
    ('Olivia', 86, 'ME');

-- ─── VACUUM ──────────────────────────────────────────────────
-- Rebuilds the database file, removing any fragmentation.
-- Ensures the hex dump reflects a clean, compact layout.
VACUUM;