#!/usr/bin/env bash
# Lab 2: Demonstrate SQLite file size growth as records are added.
set -euo pipefail

DB="${1:-/tmp/lab2_growth.db}"
rm -f "$DB"

sqlite3 "$DB" "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, email TEXT);"

report() {
    local rows="$1"
    local size pages
    size=$(ls -lh "$DB" | awk '{print $5}')
    pages=$(sqlite3 "$DB" "PRAGMA page_count;")
    printf "%6s rows -> %6s, %4s pages\n" "$rows" "$size" "$pages"
}

report 0

total=0
for batch in 100 500 1000 5000 10000; do
    sqlite3 "$DB" "WITH RECURSIVE cnt(x) AS (
        SELECT $((total + 1))
        UNION ALL SELECT x + 1 FROM cnt WHERE x < $((total + batch))
    ) INSERT INTO users SELECT x, 'user' || x, 'user' || x || '@test.com' FROM cnt;"
    total=$((total + batch))
    report "$total"
done

echo "Done. Database: $DB"
