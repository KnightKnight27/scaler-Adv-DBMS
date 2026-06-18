#!/bin/bash
# sqlite_explore.sh
# Tanishq | 24BCS10303

DB="lab2.db"

echo "=== SQLite3 Exploration ==="
echo ""

sqlite3 $DB <<EOF
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT,
    age INTEGER
);

BEGIN;
$(for i in $(seq 1 1000); do echo "INSERT OR IGNORE INTO users VALUES ($i, 'user_$i', 'user_$i@example.com', $(( (RANDOM % 50) + 18 )));"; done)
COMMIT;
EOF

echo "--- File info ---"
ls -lh $DB
echo ""

echo "--- Page size ---"
sqlite3 $DB "PRAGMA page_size;"

echo "--- Page count ---"
sqlite3 $DB "PRAGMA page_count;"

echo "--- Journal mode ---"
sqlite3 $DB "PRAGMA journal_mode;"

echo ""
echo "=== Query WITHOUT mmap ==="
sqlite3 $DB "PRAGMA mmap_size=0;"
time sqlite3 $DB "SELECT * FROM users;" > /dev/null

echo ""
echo "=== Query WITH mmap (256MB) ==="
sqlite3 $DB "PRAGMA mmap_size=268435456;"
time sqlite3 $DB "SELECT * FROM users;" > /dev/null

echo ""
echo "--- Process check ---"
sqlite3 $DB "SELECT count(*) FROM users;" &
ps aux | grep sqlite
wait

rm -f $DB
echo ""
echo "Done."
