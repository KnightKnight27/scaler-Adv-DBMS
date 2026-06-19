-- Shared schema and data for SQLite vs PostgreSQL comparison

CREATE TABLE students (
    id   INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    age  INTEGER,
    dept TEXT
);

CREATE TABLE courses (
    id          INTEGER PRIMARY KEY,
    student_id  INTEGER NOT NULL,
    course_name TEXT NOT NULL,
    grade       TEXT,
    FOREIGN KEY (student_id) REFERENCES students(id)
);

INSERT INTO students (id, name, age, dept) VALUES
    (1,  'Aarav',  20, 'CS'),
    (2,  'Diya',   21, 'EE'),
    (3,  'Rohan',  22, 'CS'),
    (4,  'Isha',   20, 'ME'),
    (5,  'Kabir',  23, 'CS'),
    (6,  'Mira',   21, 'EE'),
    (7,  'Vivaan', 22, 'ME'),
    (8,  'Anaya',  20, 'CS'),
    (9,  'Aditya', 23, 'EE'),
    (10, 'Sara',   21, 'CS');

INSERT INTO courses (id, student_id, course_name, grade) VALUES
    (1,  1,  'DBMS',    'A'),
    (2,  2,  'OS',      'B'),
    (3,  3,  'DBMS',    'A'),
    (4,  4,  'Thermo',  'C'),
    (5,  5,  'AI',      'A'),
    (6,  6,  'Signals', 'B'),
    (7,  7,  'CAD',     'B'),
    (8,  8,  'AI',      'A'),
    (9,  9,  'OS',      'C'),
    (10, 10, 'DBMS',    'B');
