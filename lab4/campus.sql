PRAGMA page_size = 1024;
PRAGMA auto_vacuum = 0;

DROP TABLE IF EXISTS enrollments;
DROP TABLE IF EXISTS students;
DROP TABLE IF EXISTS courses;

CREATE TABLE courses (
    course_id INTEGER PRIMARY KEY,
    code TEXT NOT NULL UNIQUE,
    title TEXT NOT NULL,
    credits INTEGER NOT NULL
);

CREATE TABLE students (
    student_id INTEGER PRIMARY KEY,
    roll_no TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    branch TEXT NOT NULL,
    semester INTEGER NOT NULL,
    cgpa REAL NOT NULL
);

CREATE TABLE enrollments (
    enrollment_id INTEGER PRIMARY KEY,
    student_id INTEGER NOT NULL,
    course_id INTEGER NOT NULL,
    grade TEXT NOT NULL,
    attendance INTEGER NOT NULL,
    FOREIGN KEY (student_id) REFERENCES students(student_id),
    FOREIGN KEY (course_id) REFERENCES courses(course_id)
);

CREATE INDEX idx_students_roll_no ON students(roll_no);
CREATE INDEX idx_enrollments_student ON enrollments(student_id);

INSERT INTO courses (course_id, code, title, credits) VALUES
    (1, 'ADBMS', 'Advanced Database Management Systems', 4),
    (2, 'OS', 'Operating Systems', 4),
    (3, 'CN', 'Computer Networks', 3),
    (4, 'DAA', 'Design and Analysis of Algorithms', 4),
    (5, 'SE', 'Software Engineering', 3);

WITH RECURSIVE seq(n) AS (
    VALUES(1)
    UNION ALL
    SELECT n + 1 FROM seq WHERE n < 120
)
INSERT INTO students (roll_no, name, branch, semester, cgpa)
SELECT
    printf('24BCS%05d', 10000 + n),
    printf('Student %03d', n),
    CASE n % 4
        WHEN 0 THEN 'CSE'
        WHEN 1 THEN 'AIML'
        WHEN 2 THEN 'DS'
        ELSE 'Cyber'
    END,
    4,
    round(7.00 + ((n % 25) * 0.08), 2)
FROM seq;

WITH RECURSIVE seq(n) AS (
    VALUES(1)
    UNION ALL
    SELECT n + 1 FROM seq WHERE n < 120
)
INSERT INTO enrollments (student_id, course_id, grade, attendance)
SELECT
    n,
    ((n - 1) % 5) + 1,
    CASE n % 5
        WHEN 0 THEN 'A'
        WHEN 1 THEN 'B+'
        WHEN 2 THEN 'A-'
        WHEN 3 THEN 'B'
        ELSE 'A+'
    END,
    75 + (n % 24)
FROM seq;

ANALYZE;
