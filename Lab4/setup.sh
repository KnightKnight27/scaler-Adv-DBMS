#!/usr/bin/env bash
# Task 1 + Task 2 — create the SQLite database and capture metadata.
set -euo pipefail
cd "$(dirname "$0")"

DB=students.db
RESULTS=results
mkdir -p "$RESULTS"

# Fresh DB each run so results are reproducible.
rm -f "$DB" "$DB-journal" "$DB-wal" "$DB-shm"

sqlite3 "$DB" <<'SQL'
CREATE TABLE students (
    id    INTEGER PRIMARY KEY,
    name  TEXT NOT NULL,
    grade TEXT,
    marks INTEGER
);
INSERT INTO students (name, grade, marks) VALUES
    ('Alice',   'A', 92),
    ('Bob',     'B', 78),
    ('Charlie', 'A', 88),
    ('Diana',   'C', 65),
    ('Ethan',   'B', 73),
    ('Fiona',   'A', 95),
    ('George',  'D', 52),
    ('Hannah',  'B', 80),
    ('Ivan',    'C', 68),
    ('Julia',   'A', 90);
SQL

{
    echo "============================================================"
    echo " TASK 2 — DATABASE METADATA"
    echo "============================================================"
    echo
    echo "--- sqlite3 .dbinfo ---"
    sqlite3 "$DB" ".dbinfo" 2>&1 || echo "(.dbinfo unsupported on this SQLite build — using PRAGMAs below instead)"
    echo
    echo "--- PRAGMA encoding / schema_version / freelist_count / application_id ---"
    sqlite3 "$DB" "PRAGMA encoding;       PRAGMA schema_version; PRAGMA freelist_count; PRAGMA application_id;"
    echo
    echo "--- PRAGMA page_size  ---"
    sqlite3 "$DB" "PRAGMA page_size;"
    echo
    echo "--- PRAGMA page_count ---"
    sqlite3 "$DB" "PRAGMA page_count;"
    echo
    echo "--- Schema (.schema) ---"
    sqlite3 "$DB" ".schema"
    echo
    echo "--- sqlite_master contents ---"
    sqlite3 -header -column "$DB" \
        "SELECT type, name, tbl_name, rootpage, sql FROM sqlite_master;"
    echo
    echo "--- Record count ---"
    sqlite3 "$DB" "SELECT COUNT(*) AS rows FROM students;"
    echo
    echo "--- File size on disk ---"
    stat -c "%n  %s bytes" "$DB" 2>/dev/null || ls -l "$DB"
} > "$RESULTS/metadata.txt"

echo "[setup] $DB created. Metadata -> $RESULTS/metadata.txt"
