DROP TABLE IF EXISTS users;
CREATE TABLE users (
    id      INTEGER PRIMARY KEY,
    name    TEXT NOT NULL,
    email   TEXT NOT NULL,
    age     INTEGER,
    city    TEXT,
    created TEXT,
    bio     TEXT
);
WITH RECURSIVE seq(n) AS (
    SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n < 1000000
)
INSERT INTO users(id, name, email, age, city, created, bio)
SELECT n,
       'User_' || n,
       'user' || n || '@example.com',
       20 + (n % 60),
       CASE (n % 5) WHEN 0 THEN 'Bangalore' WHEN 1 THEN 'Mumbai' WHEN 2 THEN 'Delhi' WHEN 3 THEN 'Chennai' ELSE 'Hyderabad' END,
       datetime('now', '-' || (n % 365) || ' days'),
       'Bio text padding for row ' || n || ' so each row is wider and we get more pages.'
FROM seq;
CREATE INDEX idx_large_city ON users(city);
