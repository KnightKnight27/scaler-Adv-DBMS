-- Lab 4 — SQLite on-disk format walkthrough
-- Author: 24BCS10248 Saswata Das

CREATE TABLE students (
    student_id INTEGER PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    last_name  VARCHAR(100) NOT NULL,
    age        INT,
    email      VARCHAR(255) UNIQUE,
    course     VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO students (first_name, last_name, age, email, course, created_at) VALUES
    ('Saswata', 'Das',       20, 'saswata@example.com', 'Computer Science',  '2026-05-26 17:01:31'),
    ('Priya',   'Sharma',    21, 'priya@example.com',   'Information Tech',  '2026-05-26 17:01:31'),
    ('Arjun',   'Patel',     19, 'arjun@example.com',   'Electronics',       '2026-05-26 17:01:31'),
    ('Neha',    'Gupta',     20, 'neha@example.com',    'Mechanical',        '2026-05-26 17:01:31'),
    ('Rohan',   'Singh',     22, 'rohan@example.com',   'Civil',             '2026-05-26 17:01:31');

VACUUM;
