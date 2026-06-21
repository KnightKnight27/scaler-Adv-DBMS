-- MiniDB end-to-end demo script
-- Run with: ./build/minidb --batch docs/demo.sql

-- ── Storage + DDL ─────────────────────────────────────────────────────────
CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR(64), grade INT);
CREATE TABLE courses  (cid INT PRIMARY KEY, title VARCHAR(64));
CREATE TABLE enroll   (sid INT, cid INT);

-- ── INSERT ────────────────────────────────────────────────────────────────
INSERT INTO students VALUES (1, 'Alice', 90);
INSERT INTO students VALUES (2, 'Bob',   75);
INSERT INTO students VALUES (3, 'Carol', 88);
INSERT INTO courses  VALUES (101, 'Databases');
INSERT INTO courses  VALUES (102, 'Algorithms');
INSERT INTO enroll   VALUES (1, 101);
INSERT INTO enroll   VALUES (2, 101);
INSERT INTO enroll   VALUES (3, 102);
INSERT INTO enroll   VALUES (1, 102);

-- ── SELECT: full scan ─────────────────────────────────────────────────────
SELECT * FROM students;

-- ── SELECT: index scan (WHERE on PK) ─────────────────────────────────────
SELECT * FROM students WHERE id = 2;

-- ── SELECT: range WHERE ───────────────────────────────────────────────────
SELECT * FROM students WHERE grade >= 85;

-- ── JOIN ─────────────────────────────────────────────────────────────────
SELECT * FROM enroll JOIN students ON enroll.sid = students.id;

-- ── DELETE ────────────────────────────────────────────────────────────────
DELETE FROM students WHERE id = 2;
SELECT * FROM students;

-- ── Transaction: commit ───────────────────────────────────────────────────
BEGIN;
INSERT INTO students VALUES (4, 'Dave', 70);
SELECT * FROM students WHERE id = 4;
COMMIT;

-- ── Transaction: rollback ─────────────────────────────────────────────────
BEGIN;
INSERT INTO students VALUES (5, 'Eve', 95);
SELECT * FROM students WHERE id = 5;
ROLLBACK;
-- Eve should not appear:
SELECT * FROM students WHERE id = 5;

-- Done
SELECT * FROM students;
