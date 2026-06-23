-- Lab 2: SQLite3 Database Setup Script
-- Creates a sample students database for PRAGMA exploration

-- Create students table
CREATE TABLE IF NOT EXISTS students (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    age INTEGER,
    gpa REAL,
    major TEXT
);

-- Insert sample data
INSERT INTO students (name, age, gpa, major) VALUES
    ('Alice Johnson', 22, 3.8, 'Computer Science'),
    ('Bob Smith', 25, 2.9, 'Mathematics'),
    ('Carol Davis', 21, 3.5, 'Physics'),
    ('Dave Wilson', 30, 3.1, 'Engineering'),
    ('Eve Brown', 23, 3.9, 'Computer Science'),
    ('Frank Miller', 24, 2.7, 'Chemistry'),
    ('Grace Lee', 22, 3.6, 'Biology'),
    ('Henry Taylor', 26, 3.2, 'Mathematics');

-- Create courses table for more complex queries
CREATE TABLE IF NOT EXISTS courses (
    course_id INTEGER PRIMARY KEY AUTOINCREMENT,
    course_name TEXT NOT NULL,
    credits INTEGER,
    department TEXT
);

-- Insert course data
INSERT INTO courses (course_name, credits, department) VALUES
    ('Advanced Databases', 4, 'Computer Science'),
    ('Data Structures', 4, 'Computer Science'),
    ('Linear Algebra', 3, 'Mathematics'),
    ('Quantum Physics', 4, 'Physics'),
    ('Organic Chemistry', 4, 'Chemistry');

-- Create enrollment table (many-to-many relationship)
CREATE TABLE IF NOT EXISTS enrollments (
    student_id INTEGER,
    course_id INTEGER,
    semester TEXT,
    grade TEXT,
    FOREIGN KEY (student_id) REFERENCES students(id),
    FOREIGN KEY (course_id) REFERENCES courses(course_id)
);

-- Insert enrollment data
INSERT INTO enrollments (student_id, course_id, semester, grade) VALUES
    (1, 1, 'Fall 2025', 'A'),
    (1, 2, 'Fall 2025', 'A-'),
    (2, 3, 'Fall 2025', 'B+'),
    (3, 4, 'Fall 2025', 'A'),
    (5, 1, 'Fall 2025', 'A+'),
    (5, 2, 'Spring 2026', 'A');

-- Display summary
SELECT 'Database setup complete!' as message;
SELECT COUNT(*) as student_count FROM students;
SELECT COUNT(*) as course_count FROM courses;
SELECT COUNT(*) as enrollment_count FROM enrollments;
