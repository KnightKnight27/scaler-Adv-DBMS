#!/usr/bin/env bash
# SQLite3 page-size / page-count / mmap experiments.
set -e

DB=/tmp/lab2_sqlite.db
ROWS=${ROWS:-1000000}

echo "=========================================="
echo "  SQLite3 Experiment ($ROWS rows)"
echo "=========================================="
sqlite3 --version

rm -f "$DB" "$DB-wal" "$DB-shm"

# Schema
sqlite3 "$DB" <<SQL
CREATE TABLE users (
  id    INTEGER PRIMARY KEY,
  name  TEXT,
  email TEXT,
  age   INTEGER
);
SQL

echo
echo "Inserting $ROWS rows (single transaction)..."
sqlite3 "$DB" <<SQL
BEGIN;
WITH RECURSIVE seq(x) AS (
  SELECT 1 UNION ALL SELECT x+1 FROM seq WHERE x < $ROWS
)
INSERT INTO users(id, name, email, age)
SELECT x, 'user_'||x, 'user_'||x||'@test.com', (x % 80) + 18
FROM seq;
COMMIT;
SQL

echo
echo "=== ls -lh on db file ==="
ls -lh "$DB"

echo
echo "=== Page metadata (PRAGMA) ==="
sqlite3 "$DB" <<SQL
.headers on
.mode column
PRAGMA page_size;
PRAGMA page_count;
PRAGMA freelist_count;
PRAGMA cache_size;
PRAGMA journal_mode;
PRAGMA mmap_size;
SQL

echo
echo "=== Query timing — mmap DISABLED (mmap_size=0) ==="
sqlite3 "$DB" <<SQL
.timer on
PRAGMA mmap_size = 0;
SELECT COUNT(*) AS rows_total, AVG(age) AS avg_age FROM users;
SELECT COUNT(*) AS adults_over_30 FROM users WHERE age > 30;
SQL

echo
echo "=== Query timing — mmap ENABLED (mmap_size=256 MB) ==="
sqlite3 "$DB" <<SQL
.timer on
PRAGMA mmap_size = 268435456;
SELECT COUNT(*) AS rows_total, AVG(age) AS avg_age FROM users;
SELECT COUNT(*) AS adults_over_30 FROM users WHERE age > 30;
SQL

echo
echo "=== ps aux | grep sqlite ==="
ps aux | grep -i sqlite | grep -v grep || echo "(no long-running sqlite procs — sqlite is an in-process library, not a server)"

echo
echo "=========================================="
echo "  SQLite experiment done."
echo "=========================================="
