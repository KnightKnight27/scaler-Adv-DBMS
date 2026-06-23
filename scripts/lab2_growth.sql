-- Lab 2: incremental growth demo for test.db
-- Usage: rm -f test.db && sqlite3 test.db < scripts/lab2_growth.sql
-- After each section, run: ls -lh test.db && sqlite3 test.db "PRAGMA page_count;"

CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    email TEXT,
    age INTEGER
);

-- 1,000 rows
INSERT INTO users (name, email, age)
WITH RECURSIVE cnt(x) AS (
    SELECT 1 UNION ALL SELECT x + 1 FROM cnt WHERE x < 1000
)
SELECT 'user_' || x, 'user' || x || '@example.com', (x % 80) + 18 FROM cnt;
