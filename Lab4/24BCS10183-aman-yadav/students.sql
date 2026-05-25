-- Lab 4 — SQLite on-disk format walkthrough
-- Author: 24BCS10183 Aman Yadav (Class B, 2nd year)

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
    ('Aman',   'Yadav',   19, 'aman@example.com',   'Computer Science'),
    ('Riya',   'Verma',   20, 'riya@example.com',   'Information Tech'),
    ('Karan',  'Mehta',   19, 'karan@example.com',  'Electronics'),
    ('Sneha',  'Kapoor',  21, 'sneha@example.com',  'Mechanical'),
    ('Vivaan', 'Sharma',  20, 'vivaan@example.com', 'Civil');

VACUUM;
