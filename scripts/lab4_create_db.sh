#!/usr/bin/env bash
# Lab 4: Recreate students.db and dump hex for analysis.
set -euo pipefail

DB="${1:-students.db}"
rm -f "$DB"

sqlite3 "$DB" <<'SQL'
CREATE TABLE students (
    student_id SERIAL PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    last_name VARCHAR(100) NOT NULL,
    age INT,
    email VARCHAR(255) UNIQUE,
    course VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO students (first_name, last_name, age, email, course)
VALUES ('Kartik', 'Bhatia', 22, 'kartik@example.com', 'Computer Science');

INSERT INTO students (first_name, last_name, age, email, course)
VALUES ('Prashansa', 'Sharma', 21, 'prashansa@example.com', 'Electronics');
SQL

echo "=== PRAGMA ==="
sqlite3 "$DB" "PRAGMA page_size; PRAGMA page_count;"
echo "=== .schema ==="
sqlite3 "$DB" ".schema students"
echo "=== xxd header ==="
xxd -g 1 -l 256 "$DB"
