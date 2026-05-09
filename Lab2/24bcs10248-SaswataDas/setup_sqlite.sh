#!/usr/bin/env bash
set -euo pipefail

DB_FILE="${1:-sample.db}"
ROW_COUNT="${ROW_COUNT:-10000}"

command -v sqlite3 >/dev/null 2>&1 || {
  echo "sqlite3 is not installed or not on PATH" >&2
  exit 1
}

echo "SQLite version:"
sqlite3 --version

echo "Creating SQLite database: ${DB_FILE}"
sqlite3 "${DB_FILE}" <<SQL
DROP TABLE IF EXISTS users;
CREATE TABLE users(
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  email TEXT NOT NULL,
  created_at TEXT NOT NULL
);

WITH RECURSIVE seq(n) AS (
  VALUES(1)
  UNION ALL
  SELECT n + 1 FROM seq WHERE n < ${ROW_COUNT}
)
INSERT INTO users(name, email, created_at)
SELECT
  'User ' || n,
  'user' || n || '@example.com',
  datetime('now', '-' || n || ' minutes')
FROM seq;
SQL

echo
printf 'Database file size:\n'
ls -lh "${DB_FILE}"

echo
printf 'SQLite page details and mmap values:\n'
sqlite3 "${DB_FILE}" <<'SQL'
.headers on
.mode column
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size = 0;
PRAGMA mmap_size;
PRAGMA mmap_size = 268435456;
PRAGMA mmap_size;
SQL
