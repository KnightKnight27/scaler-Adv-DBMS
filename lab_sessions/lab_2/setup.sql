-- Lab 2: SQLite3 Internals setup
CREATE TABLE IF NOT EXISTS students (
    id   INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    age  INTEGER,
    dept TEXT
);

INSERT OR IGNORE INTO students VALUES
    (1, 'Aarav',  20, 'CS'),
    (2, 'Diya',   21, 'EE'),
    (3, 'Rohan',  22, 'CS'),
    (4, 'Isha',   20, 'ME'),
    (5, 'Kabir',  23, 'CS');
