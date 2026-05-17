-- Schema for students table
CREATE TABLE students (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  roll TEXT NOT NULL UNIQUE,
  name TEXT NOT NULL,
  department TEXT
);

-- Example insert (optional):
-- INSERT INTO students (roll, name, department) VALUES ('24BCS10276','Sahadheep','CSE');

-- To create the database from this SQL file:
-- sqlite3 student.db < student.sql
