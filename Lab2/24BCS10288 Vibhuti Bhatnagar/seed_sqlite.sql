-- Seed script for SQLite sample database
DROP TABLE IF EXISTS users;
CREATE TABLE users (
    id      INTEGER PRIMARY KEY,
    name    TEXT NOT NULL,
    email   TEXT NOT NULL,
    age     INTEGER,
    city    TEXT,
    created TEXT
);

-- Insert ~100,000 rows using a recursive CTE so the file is large enough
-- for page size / page count observations to be meaningful.
WITH RECURSIVE seq(n) AS (
    SELECT 1
    UNION ALL
    SELECT n+1 FROM seq WHERE n < 100000
)
INSERT INTO users(id, name, email, age, city, created)
SELECT
    n,
    'User_' || n,
    'user' || n || '@example.com',
    20 + (n % 60),
    CASE (n % 5)
        WHEN 0 THEN 'Bangalore'
        WHEN 1 THEN 'Mumbai'
        WHEN 2 THEN 'Delhi'
        WHEN 3 THEN 'Chennai'
        ELSE 'Hyderabad'
    END,
    datetime('now', '-' || (n % 365) || ' days')
FROM seq;

CREATE INDEX idx_users_city ON users(city);
CREATE INDEX idx_users_age  ON users(age);
