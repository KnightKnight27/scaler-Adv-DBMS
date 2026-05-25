-- Lab 4: SQLite on-disk format walkthrough.
-- Build with: sqlite3 campus.db < campus.sql

CREATE TABLE students (
  id    INTEGER PRIMARY KEY,
  name  TEXT NOT NULL,
  grade INTEGER
);

CREATE INDEX idx_students_grade ON students(grade);

INSERT INTO students (name, grade) VALUES
  ('Aarav',  87),
  ('Diya',   93),
  ('Ishaan', 76),
  ('Meera',  89),
  ('Rohit',  82);

VACUUM;
