#!/usr/bin/env bash
# Lab 4: SQLite3 Database Internal Structure Analysis
# Script 1: Create the database and populate it with records
# Student: Lokendra Singh Rajawat (23bcs10075)
#
# Usage: bash create_db.sh

set -e

DB="lab4_students.db"

echo "============================================================"
echo " Lab 4 — SQLite3 Internal Structure Analysis"
echo " Step 1: Creating Database and Populating Records"
echo "============================================================"
echo ""

# Remove old DB if present so we start fresh
rm -f "$DB"

echo "[1] Creating database: $DB"
echo "[2] Creating 'students' table and inserting records..."

sqlite3 "$DB" <<'SQL'
-- Create the students table
CREATE TABLE students (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    name    TEXT    NOT NULL,
    roll_no TEXT    NOT NULL UNIQUE,
    branch  TEXT    NOT NULL,
    cgpa    REAL    NOT NULL
);

-- Insert sample records
INSERT INTO students (name, roll_no, branch, cgpa) VALUES
    ('Lokendra Singh Rajawat', '23bcs10075', 'Computer Science', 9.1),
    ('Aarav Sharma',           '23bcs10001', 'Computer Science', 8.5),
    ('Priya Verma',            '23bcs10002', 'Information Technology', 8.9),
    ('Rohan Mehta',            '23bcs10003', 'Electronics',      7.8),
    ('Sneha Patel',            '23bcs10004', 'Computer Science', 9.3),
    ('Vikram Chauhan',         '23bcs10005', 'Mechanical',       7.2),
    ('Anita Rao',              '23bcs10006', 'Computer Science', 8.7),
    ('Deepak Nair',            '23bcs10007', 'Civil Engineering', 6.9),
    ('Kavya Joshi',            '23bcs10008', 'Information Technology', 9.0),
    ('Manish Kumar',           '23bcs10009', 'Computer Science', 8.1);
SQL

echo "[3] Database created successfully."
echo ""

echo "------------------------------------------------------------"
echo " Table Schema"
echo "------------------------------------------------------------"
sqlite3 "$DB" ".schema students"

echo ""
echo "------------------------------------------------------------"
echo " Inserted Records"
echo "------------------------------------------------------------"
sqlite3 -column -header "$DB" "SELECT * FROM students;"

echo ""
echo "------------------------------------------------------------"
echo " Database Metadata (PRAGMAs)"
echo "------------------------------------------------------------"
echo -n "Page size (bytes)   : "
sqlite3 "$DB" "PRAGMA page_size;"

echo -n "Page count          : "
sqlite3 "$DB" "PRAGMA page_count;"

echo -n "Database size (B)   : "
sqlite3 "$DB" "PRAGMA page_size;" | awk -v pc="$(sqlite3 "$DB" 'PRAGMA page_count;')" '{print $1 * pc " bytes (" $1 * pc / 1024 " KB)"}'

echo -n "Encoding            : "
sqlite3 "$DB" "PRAGMA encoding;"

echo -n "SQLite version      : "
sqlite3 "$DB" "SELECT sqlite_version();"

echo ""
echo "------------------------------------------------------------"
echo " sqlite_master (Internal Schema Table)"
echo "------------------------------------------------------------"
sqlite3 -column -header "$DB" "SELECT type, name, tbl_name, rootpage, sql FROM sqlite_master;"

echo ""
echo "------------------------------------------------------------"
echo " File size on disk"
echo "------------------------------------------------------------"
ls -lh "$DB"

echo ""
echo "[DONE] Database '$DB' ready for hex inspection. Run: bash inspect.sh"
