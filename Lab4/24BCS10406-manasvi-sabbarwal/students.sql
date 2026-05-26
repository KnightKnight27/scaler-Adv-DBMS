-- Lab 4 — SQLite on-disk format walkthrough
-- Author: 24BCS10406 Manasvi Sabbarwal

CREATE TABLE students (
    student_id INTEGER PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    last_name  VARCHAR(100) NOT NULL,
    age        INT,
    email      VARCHAR(255) UNIQUE,
    course     VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO students (first_name, last_name, age, email, course) VALUES
    ('Manasvi', 'Sabbarwal', 20, 'manasvi@example.com', 'Computer Science'),
    ('Priya',   'Sharma',    21, 'priya@example.com',   'Information Tech'),
    ('Arjun',   'Patel',     19, 'arjun@example.com',   'Electronics'),
    ('Neha',    'Gupta',     20, 'neha@example.com',    'Mechanical'),
    ('Rohan',   'Singh',     22, 'rohan@example.com',   'Civil');

VACUUM;
