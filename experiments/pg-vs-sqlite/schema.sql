-- Shared schema for PostgreSQL vs SQLite comparison
DROP TABLE IF EXISTS bench_orders;
DROP TABLE IF EXISTS bench_users;

CREATE TABLE bench_users (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    dept_id INTEGER
);

CREATE TABLE bench_orders (
    id INTEGER PRIMARY KEY,
    user_id INTEGER,
    amount REAL,
    status TEXT
);
