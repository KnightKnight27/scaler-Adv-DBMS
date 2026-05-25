CREATE TABLE learners (
    student_id INTEGER PRIMARY KEY,
    full_name TEXT NOT NULL,
    marks INTEGER,
    branch TEXT
);

CREATE INDEX idx_learners_marks
ON learners(marks);

INSERT INTO learners (full_name, marks, branch) VALUES
('Arjun', 90, 'CSE'),
('Bhavna', 84, 'ISE'),
('Charan', 87, 'ECE'),
('Deepa', 75, 'ME'),
('Farhan', 96, 'CSE'),
('Gauri', 81, 'EEE'),
('Harsha', 88, 'ISE'),
('Ishita', 93, 'ECE'),
('Jatin', 79, 'CSE'),
('Kiran', 86, 'ME'),
('Lavanya', 77, 'EEE'),
('Manoj', 91, 'CSE'),
('Nisha', 83, 'ISE'),
('Omkar', 94, 'ECE'),
('Pooja', 85, 'ME');

VACUUM;