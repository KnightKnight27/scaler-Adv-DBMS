-- =========================
-- SQLite Database Creation
-- =========================

CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER,
    city TEXT
);

-- =========================
-- Insert Large Dataset
-- =========================

WITH RECURSIVE cnt(x) AS (
    SELECT 1
    UNION ALL
    SELECT x + 1 FROM cnt
    LIMIT 100000
)
INSERT INTO users(name, age, city)
SELECT
    'User' || x,
    20 + (x % 30),
    'City' || (x % 100)
FROM cnt;

-- =========================
-- SQLite Internal Analysis
-- =========================

PRAGMA page_size;

PRAGMA page_count;

PRAGMA mmap_size;

-- =========================
-- Disable mmap
-- =========================

PRAGMA mmap_size = 0;

-- =========================
-- Enable mmap (256 MB)
-- =========================

PRAGMA mmap_size = 268435456;

-- =========================
-- Enable Timer
-- =========================

.timer on

-- =========================
-- Benchmark Queries
-- =========================

SELECT * FROM users;

SELECT COUNT(*) FROM users;

SELECT * FROM users
WHERE age = 25;

SELECT * FROM users
ORDER BY age;

-- =========================
-- PostgreSQL Queries
-- =========================

CREATE DATABASE lab02;

-- Connect manually:
-- \c lab02

CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    age INT,
    city TEXT
);

INSERT INTO users(name, age, city)
SELECT
    'User' || i,
    20 + (i % 30),
    'City' || (i % 100)
FROM generate_series(1,100000) i;

-- =========================
-- PostgreSQL Timing
-- =========================

\timing

SELECT * FROM users;

SELECT COUNT(*) FROM users;

SELECT * FROM users
WHERE age = 25;

SELECT * FROM users
ORDER BY age;