#!/usr/bin/env bash

echo "========================================="
echo "SQLite3 Experiment"
echo "========================================="

rm -f test.db

sqlite3 test.db <<EOF

CREATE TABLE users(
    id INTEGER PRIMARY KEY,
    name TEXT
);

INSERT INTO users(name)
VALUES ('Alice'), ('Bob'), ('Charlie');

EOF

echo
echo "SQLite Database File Size:"
ls -lh test.db

echo
echo "SQLite Page Information:"

sqlite3 test.db <<EOF

PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;

EOF

echo
echo "SQLite Query Timing Experiment:"

sqlite3 test.db <<EOF

.timer on

SELECT * FROM users;

PRAGMA mmap_size=268435456;

SELECT * FROM users;

PRAGMA mmap_size=0;

SELECT * FROM users;

EOF

echo
echo "SQLite Processes (should be empty as sqllite only runs when we open a db):"
ps aux | grep sqlite | grep -v grep

echo
echo "========================================="
echo "PostgreSQL Experiment"
echo "(Requires sudo as default postgres user is a protected system/database admin user)"
echo "========================================="

sudo -u postgres psql <<EOF

DROP DATABASE IF EXISTS testdb;
CREATE DATABASE testdb;

\c testdb

CREATE TABLE users(
    id SERIAL PRIMARY KEY,
    name TEXT
);

INSERT INTO users(name)
VALUES ('Alice'), ('Bob'), ('Charlie');

SELECT pg_size_pretty(pg_database_size('testdb'));

SHOW block_size;

SELECT
    pg_relation_size('users') / current_setting('block_size')::int
    AS approx_page_count;

\timing on

SELECT * FROM users;
SELECT * FROM users;
SELECT * FROM users;

SHOW shared_buffers;

SHOW effective_cache_size;

EXPLAIN ANALYZE SELECT * FROM users;

EOF

echo
echo "PostgreSQL Processes:"
ps aux | grep postgres | grep -v grep

echo
echo "========================================="
echo "Experiment Completed"
echo "========================================="