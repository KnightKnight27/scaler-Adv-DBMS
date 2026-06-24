#!/usr/bin/env bash
# Lab2 - PostgreSQL exploration
# Roll: 24BCS10115  Name: Gauri Shukla
#
# Run blocks manually — read each output before moving on.
#
# Prereq (macOS):
#   brew install postgresql@16
#   brew services start postgresql@16
#   createdb adbms_lab2
#
# Notes:
# - PostgreSQL's "page" is fixed at compile time (default 8 KB / 8192 bytes).
#   You can't change it at runtime (unlike SQLite). It's exposed via
#   `SHOW block_size;` and the system column `pg_class.relpages` for table page count.
# - PostgreSQL does NOT use mmap for the buffer pool. It uses `shared_buffers`
#   plus the OS page cache. So the SQLite-style "mmap on vs off" experiment
#   has no direct PostgreSQL equivalent — we vary `shared_buffers` instead
#   (or compare cold vs warm cache), which is the closest analogue.

set -u
DB="adbms_lab2"
PSQL="psql -d $DB -X -P pager=off"

echo "=========================================="
echo "[0] Versions"
echo "=========================================="
psql --version
$PSQL -c "SELECT version();"
echo

echo "=========================================="
echo "[1] Create a sample table and load ~100k rows"
echo "=========================================="
$PSQL <<'SQL'
DROP TABLE IF EXISTS users;
CREATE TABLE users (
    id          serial PRIMARY KEY,
    name        text NOT NULL,
    email       text NOT NULL,
    age         int  NOT NULL,
    created_at  timestamptz DEFAULT now()
);

INSERT INTO users (name, email, age)
SELECT
    'user_' || g,
    'user_' || g || '@example.com',
    (random() * 80)::int
FROM generate_series(1, 100000) g;

ANALYZE users;
SELECT COUNT(*) AS row_count FROM users;
SQL
echo

echo "=========================================="
echo "[2] Page size, page count, table size"
echo "=========================================="
$PSQL <<'SQL'
SHOW block_size;                              -- "page size"
SHOW shared_buffers;                          -- buffer pool size
SHOW effective_cache_size;                    -- planner's view of OS+PG cache

SELECT relname,
       relpages   AS page_count,              -- pages used by the table
       reltuples  AS approx_rows,
       pg_size_pretty(pg_relation_size('users'))      AS heap_size,
       pg_size_pretty(pg_total_relation_size('users')) AS total_size_with_indexes
FROM pg_class
WHERE relname = 'users';
SQL
echo
echo "Sanity check: block_size * relpages should ~= heap_size"
echo

echo "=========================================="
echo "[3] Time a SELECT * (cold vs warm cache)"
echo "=========================================="
# Cold cache: restart Postgres so shared_buffers + OS cache are flushed
# (simplest reliable cold-cache trick on macOS Homebrew install).
echo "--- Restarting Postgres for COLD cache run ---"
brew services restart postgresql@16 >/dev/null
sleep 2

echo "--- COLD run ---"
$PSQL -c "\timing on" -c "SELECT * FROM users;" > /tmp/pg_cold.out
wc -l /tmp/pg_cold.out

echo "--- WARM run (data now in shared_buffers + OS cache) ---"
$PSQL -c "\timing on" -c "SELECT * FROM users;" > /tmp/pg_warm.out
wc -l /tmp/pg_warm.out
echo

echo "=========================================="
echo "[4] EXPLAIN (ANALYZE, BUFFERS) for a heavier query"
echo "=========================================="
$PSQL <<'SQL'
EXPLAIN (ANALYZE, BUFFERS)
SELECT age, COUNT(*) AS c
FROM users
GROUP BY age
ORDER BY c DESC
LIMIT 10;
SQL
echo

echo "=========================================="
echo "[5] Process inspection while a query runs"
echo "=========================================="
$PSQL -c "SELECT COUNT(*) FROM users u1, users u2 WHERE u1.age = u2.age LIMIT 1;" >/dev/null &
PID=$!
sleep 1
ps aux | grep -v grep | grep -E "postgres|PID"
wait $PID 2>/dev/null
echo

echo "=========================================="
echo "[6] How storage layout changes after deletes (page reuse)"
echo "=========================================="
$PSQL <<'SQL'
DELETE FROM users WHERE id % 4 = 0;
SELECT relname, relpages, reltuples,
       pg_size_pretty(pg_relation_size('users')) AS heap_size_after_delete
FROM pg_class WHERE relname='users';

VACUUM (VERBOSE) users;

SELECT relname, relpages, reltuples,
       pg_size_pretty(pg_relation_size('users')) AS heap_size_after_vacuum
FROM pg_class WHERE relname='users';
SQL

echo
echo "Done. Capture the numbers above into README.md."
