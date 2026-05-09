#!/usr/bin/env bash
# PostgreSQL block-size / pages / query timing experiments.
set -e

DB=labtest
ROWS=${ROWS:-1000000}

echo "=========================================="
echo "  PostgreSQL Experiment ($ROWS rows)"
echo "=========================================="
psql --version

# Ensure service is up
sudo service postgresql start >/dev/null 2>&1 || true

# Recreate db
sudo -u postgres psql -v ON_ERROR_STOP=1 <<SQL >/dev/null
DROP DATABASE IF EXISTS $DB;
CREATE DATABASE $DB;
SQL

PSQL="sudo -u postgres psql -d $DB -v ON_ERROR_STOP=1"

$PSQL <<SQL >/dev/null
CREATE TABLE users (
  id    BIGSERIAL PRIMARY KEY,
  name  TEXT,
  email TEXT,
  age   INT
);
SQL

echo
echo "Inserting $ROWS rows..."
$PSQL <<SQL >/dev/null
INSERT INTO users(name, email, age)
SELECT 'user_'||g, 'user_'||g||'@test.com', (g % 80) + 18
FROM generate_series(1, $ROWS) g;
ANALYZE users;
SQL

echo
echo "=== Page / storage metadata ==="
$PSQL <<SQL
SHOW block_size;
SHOW shared_buffers;
SHOW effective_cache_size;
SHOW work_mem;
SELECT relname,
       relpages,
       reltuples::bigint AS reltuples,
       pg_size_pretty(pg_relation_size('users'))       AS heap_size,
       pg_size_pretty(pg_total_relation_size('users')) AS total_size
FROM pg_class
WHERE relname = 'users';
SQL

echo
echo "=== Underlying file on disk ==="
DATA_DIR=$(sudo -u postgres psql -tAc "SHOW data_directory;")
RELPATH=$(sudo -u postgres psql -d $DB -tAc "SELECT pg_relation_filepath('users');")
echo "Postgres data_directory : $DATA_DIR"
echo "users relfilenode path  : $RELPATH"
sudo ls -lh "$DATA_DIR/$RELPATH"* 2>/dev/null || true

echo
echo "=== Query timing — first run (cold-ish) ==="
$PSQL <<SQL
\timing on
DISCARD ALL;
SELECT COUNT(*) AS rows_total, AVG(age) AS avg_age FROM users;
SELECT COUNT(*) AS adults_over_30 FROM users WHERE age > 30;
SQL

echo
echo "=== Query timing — repeat (warm cache) ==="
$PSQL <<SQL
\timing on
SELECT COUNT(*) AS rows_total, AVG(age) AS avg_age FROM users;
SELECT COUNT(*) AS adults_over_30 FROM users WHERE age > 30;
SQL

echo
echo "=== ps aux | grep postgres ==="
ps aux | grep -i postgres | grep -v grep | head -10

echo
echo "=========================================="
echo "  PostgreSQL experiment done."
echo "=========================================="
