PRAGMA page_size = 512;
VACUUM;

CREATE TABLE students (
  id INTEGER PRIMARY KEY,
  name TEXT,
  age INTEGER
);

INSERT INTO students VALUES (1, 'Asha', 20);
INSERT INTO students VALUES (2, 'Ravi', 21);
INSERT INTO students VALUES (3, 'Meena', 22);
INSERT INTO students VALUES (4, 'Kiran', 23);

CREATE INDEX idx_students_name ON students(name);
