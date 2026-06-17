-- Lab 4: SQLite3 Hex Dump and B-Tree Analysis
-- Name: Ankit Kumar
-- Roll No: 24BCS10189

DROP TABLE IF EXISTS students;

CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    marks INTEGER NOT NULL
);

INSERT INTO students (name, marks) VALUES
('Ankit', 92),
('Rahul', 88),
('Sneha', 91),
('Aman', 76),
('Priya', 89);
