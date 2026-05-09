-- Run this inside the sqlite3 shell, not as plain SQL only.

DROP TABLE IF EXISTS users;

CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT
);

INSERT INTO users (name, email) VALUES
('Aarav', 'aarav@example.com'),
('Diya', 'diya@example.com'),
('Rohan', 'rohan@example.com'),
('Meera', 'meera@example.com');

.tables
.timer ON

PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;

PRAGMA mmap_size = 0;
SELECT * FROM users;

PRAGMA mmap_size = 268435456;
PRAGMA mmap_size;
SELECT * FROM users;

-- Run these in the terminal, not inside plain SQL:
-- sqlite3 sample.db
-- ls -lh sample.db
-- ps aux | grep sqlite
