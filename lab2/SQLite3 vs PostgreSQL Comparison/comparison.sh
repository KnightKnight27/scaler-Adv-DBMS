#!/usr/bin/env bash

echo "========================================="
echo "Lab 2: SQLite3 and PostgreSQL Comparison"
echo "Student: Jatin Chulet"
echo "Roll No: 24BCS10213"
echo "========================================="

rm -f jatin_lab.db

sqlite3 jatin_lab.db <<EOF

CREATE TABLE students(
    id INTEGER PRIMARY KEY,
    name TEXT
);

INSERT INTO students(name)
VALUES ('Jatin'), ('Kartik'), ('Kshitij');

EOF

echo
echo "SQLite Database File Size:"
ls -lh jatin_lab.db

echo
echo "SQLite Page Information:"

sqlite3 jatin_lab.db <<EOF

PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;

EOF

echo
echo "SQLite Query Timing Experiment:"

sqlite3 jatin_lab.db <<EOF

.timer on

SELECT * FROM students;

PRAGMA mmap_size=268435456;

SELECT * FROM students;

PRAGMA mmap_size=0;

SELECT * FROM students;

EOF

echo
echo "SQLite Processes (SQLite only runs when the database is accessed):"
ps aux | grep sqlite | grep -v grep

echo
echo "========================================="
echo "PostgreSQL Experiment"
echo "(Requires sudo privileges for postgres user)"
echo "========================================="

sudo -u postgres psql <<EOF

DROP DATABASE IF EXISTS jatin_db;
CREATE DATABASE jatin_db;

\c jatin_db

CREATE TABLE students(
    id SERIAL PRIMARY KEY,
    name TEXT
);

INSERT INTO students(name)
VALUES ('Jatin'), ('Kartik'), ('Kshitij');

SELECT pg_size_pretty(pg_database_size('jatin_db'));

SHOW block_size;

SELECT
    pg_relation_size('students') /
    current_setting('block_size')::int
    AS approx_page_count;

\timing on

SELECT * FROM students;
SELECT * FROM students;
SELECT * FROM students;

SHOW shared_buffers;

SHOW effective_cache_size;

EXPLAIN ANALYZE SELECT * FROM students;

EOF

echo
echo "PostgreSQL Processes:"
ps aux | grep postgres | grep -v grep

echo
echo "========================================="
echo "Experiment Completed Successfully"
echo "========================================="