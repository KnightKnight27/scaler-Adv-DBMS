CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    grade INTEGER,
    department TEXT
);

CREATE INDEX idx_students_grade
ON students(grade);

INSERT INTO students (name, grade, department) VALUES
('Alice', 91, 'CSE'),
('Bob', 85, 'ISE'),
('Carol', 88, 'ECE'),
('David', 76, 'ME'),
('Eva', 95, 'CSE'),
('Frank', 82, 'EEE'),
('Grace', 89, 'ISE'),
('Helen', 92, 'ECE'),
('Ian', 80, 'CSE'),
('Jack', 87, 'ME'),
('Kevin', 78, 'EEE'),
('Luna', 90, 'CSE'),
('Mia', 84, 'ISE'),
('Noah', 93, 'ECE'),
('Olivia', 86, 'ME');

VACUUM;
