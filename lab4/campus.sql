CREATE TABLE students (
  id    INTEGER PRIMARY KEY,
  name  TEXT NOT NULL,
  grade INTEGER
);
CREATE INDEX idx_students_grade ON students(grade);
INSERT INTO students (name, grade) VALUES
  ('Alice', 92),
  ('Bob',   85),
  ('Carol', 91),
  ('Dave',  78),
  ('Eve',   88);
VACUUM;