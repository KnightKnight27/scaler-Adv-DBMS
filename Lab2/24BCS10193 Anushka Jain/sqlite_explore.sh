#!/bin/bash
# =============================================================================
#  sqlite_explore.sh
#  SQLite3 page size, page count, mmap, and query timing exploration
#
#  Author : Anushka Jain | 24BCS10193
#  Course : Advanced DBMS (Scaler)
# =============================================================================

DB="anushka_lab2.db"

echo ""
echo "============================================================"
echo "  SQLite3 Exploration — Anushka Jain | 24BCS10193"
echo "============================================================"
echo ""

# -----------------------------------------------------------------------------
#  Create database and populate a sample table
# -----------------------------------------------------------------------------
echo "[+] Creating database and inserting 1000 rows..."

sqlite3 "$DB" <<'EOF'
CREATE TABLE IF NOT EXISTS students (
    id      INTEGER PRIMARY KEY,
    name    TEXT    NOT NULL,
    email   TEXT    NOT NULL,
    score   REAL,
    dept    TEXT
);

BEGIN;
INSERT OR IGNORE INTO students VALUES (1,   'Alice',   'alice@college.edu',   88.5, 'CSE');
INSERT OR IGNORE INTO students VALUES (2,   'Bob',     'bob@college.edu',     74.0, 'ECE');
INSERT OR IGNORE INTO students VALUES (3,   'Carol',   'carol@college.edu',   91.2, 'CSE');
INSERT OR IGNORE INTO students VALUES (4,   'Dave',    'dave@college.edu',    65.5, 'MECH');
INSERT OR IGNORE INTO students VALUES (5,   'Eva',     'eva@college.edu',     83.0, 'CSE');
INSERT OR IGNORE INTO students VALUES (6,   'Frank',   'frank@college.edu',   77.8, 'ECE');
INSERT OR IGNORE INTO students VALUES (7,   'Grace',   'grace@college.edu',   95.1, 'CSE');
INSERT OR IGNORE INTO students VALUES (8,   'Hank',    'hank@college.edu',    58.3, 'MECH');
INSERT OR IGNORE INTO students VALUES (9,   'Iris',    'iris@college.edu',    89.9, 'ECE');
INSERT OR IGNORE INTO students VALUES (10,  'Jack',    'jack@college.edu',    72.4, 'CSE');
COMMIT;

-- Bulk insert remaining rows via a recursive CTE trick
WITH RECURSIVE gen(n) AS (
    SELECT 11
    UNION ALL
    SELECT n + 1 FROM gen WHERE n < 1000
)
INSERT OR IGNORE INTO students (id, name, email, score, dept)
SELECT
    n,
    'student_' || n,
    'student_' || n || '@college.edu',
    round(50.0 + (n * 17 + 13) % 50, 1),
    CASE (n % 4) WHEN 0 THEN 'CSE' WHEN 1 THEN 'ECE' WHEN 2 THEN 'MECH' ELSE 'CIVIL' END
FROM gen;
EOF

echo ""
echo "--- File size on disk ---"
ls -lh "$DB"

echo ""
echo "--- Page size (bytes) ---"
sqlite3 "$DB" "PRAGMA page_size;"

echo ""
echo "--- Page count ---"
sqlite3 "$DB" "PRAGMA page_count;"

echo ""
echo "--- Journal mode ---"
sqlite3 "$DB" "PRAGMA journal_mode;"

echo ""
echo "--- Free pages (fragmentation check) ---"
sqlite3 "$DB" "PRAGMA freelist_count;"

echo ""
echo "--- Cache size (pages) ---"
sqlite3 "$DB" "PRAGMA cache_size;"

echo ""
echo "============================================================"
echo "  QUERY TIMING"
echo "============================================================"

echo ""
echo "=== Without mmap (mmap_size = 0) ==="
sqlite3 "$DB" "PRAGMA mmap_size=0;"
time sqlite3 "$DB" "SELECT * FROM students;" > /dev/null

echo ""
echo "=== With mmap = 64 MB ==="
sqlite3 "$DB" "PRAGMA mmap_size=67108864;"
time sqlite3 "$DB" "SELECT * FROM students;" > /dev/null

echo ""
echo "=== With mmap = 256 MB ==="
sqlite3 "$DB" "PRAGMA mmap_size=268435456;"
time sqlite3 "$DB" "SELECT * FROM students;" > /dev/null

echo ""
echo "--- Aggregate query (no mmap) ---"
sqlite3 "$DB" "PRAGMA mmap_size=0;"
time sqlite3 "$DB" "SELECT dept, COUNT(*), AVG(score) FROM students GROUP BY dept;" 

echo ""
echo "--- Process snapshot while sqlite3 is running ---"
sqlite3 "$DB" "SELECT count(*) FROM students;" &
SQPID=$!
sleep 0.05
ps aux | grep sqlite3 | grep -v grep
wait $SQPID

echo ""
echo "--- Cleaning up ---"
rm -f "$DB"
echo "Done."
echo ""
