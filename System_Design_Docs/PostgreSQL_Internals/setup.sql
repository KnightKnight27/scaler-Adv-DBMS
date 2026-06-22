-- Experiment dataset for PostgreSQL internals study
-- Run with: psql -d dbms_lab -f setup.sql

DROP TABLE IF EXISTS enrollments, courses, students CASCADE;

CREATE TABLE students (
    id          serial PRIMARY KEY,
    name        text NOT NULL,
    dept        text NOT NULL,
    join_year   int  NOT NULL
);

CREATE TABLE courses (
    id          serial PRIMARY KEY,
    title       text NOT NULL,
    credits     int  NOT NULL,
    dept        text NOT NULL
);

CREATE TABLE enrollments (
    id          serial PRIMARY KEY,
    student_id  int NOT NULL REFERENCES students(id),
    course_id   int NOT NULL REFERENCES courses(id),
    grade       int,
    enrolled_on date NOT NULL
);

-- 20k students, 500 courses, 200k enrollments
INSERT INTO students (name, dept, join_year)
SELECT 'student_' || g,
       (ARRAY['CS','EE','ME','CE','MA'])[1 + (g % 5)],
       2018 + (g % 6)
FROM generate_series(1, 20000) g;

INSERT INTO courses (title, credits, dept)
SELECT 'course_' || g,
       1 + (g % 4),
       (ARRAY['CS','EE','ME','CE','MA'])[1 + (g % 5)]
FROM generate_series(1, 500) g;

INSERT INTO enrollments (student_id, course_id, grade, enrolled_on)
SELECT 1 + (g % 20000),
       1 + (g % 500),
       (g % 11),                    -- grade 0..10, some NULL below
       DATE '2022-01-01' + (g % 900)
FROM generate_series(1, 200000) g;

-- create a secondary index used by one of the queries
CREATE INDEX idx_enr_student ON enrollments(student_id);
CREATE INDEX idx_students_dept ON students(dept);

ANALYZE;
