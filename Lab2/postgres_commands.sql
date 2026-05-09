-- Run this inside psql.
-- Example connection command:
-- psql -U postgres -h 127.0.0.1 -p 5432

DROP DATABASE IF EXISTS lab2db;
CREATE DATABASE lab2db;

\c lab2db

CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100),
    email VARCHAR(100)
);

INSERT INTO users (name, email) VALUES
('Aarav', 'aarav@example.com'),
('Diya', 'diya@example.com'),
('Rohan', 'rohan@example.com'),
('Meera', 'meera@example.com');

SELECT version();
SELECT current_setting('block_size');
SELECT pg_database_size('lab2db');

\timing
SELECT * FROM users;

SELECT pg_relation_size('users') AS users_table_size;
SELECT relpages, reltuples FROM pg_class WHERE relname = 'users';
