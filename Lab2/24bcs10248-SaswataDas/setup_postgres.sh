#!/usr/bin/env bash
set -euo pipefail

DB_NAME="${1:-lab2db}"
ROW_COUNT="${ROW_COUNT:-10000}"

command -v psql >/dev/null 2>&1 || {
  echo "psql is not installed or not on PATH" >&2
  exit 1
}
command -v createdb >/dev/null 2>&1 || {
  echo "createdb is not installed or not on PATH" >&2
  exit 1
}

echo "PostgreSQL version:"
psql --version

createdb "${DB_NAME}" 2>/dev/null || true

echo "Creating PostgreSQL users table in database: ${DB_NAME}"
psql "${DB_NAME}" <<SQL
DROP TABLE IF EXISTS users;
CREATE TABLE users(
  id SERIAL PRIMARY KEY,
  name TEXT NOT NULL,
  email TEXT NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT now()
);

INSERT INTO users(name, email, created_at)
SELECT
  'User ' || n,
  'user' || n || '@example.com',
  now() - (n || ' minutes')::interval
FROM generate_series(1, ${ROW_COUNT}) AS n;

ANALYZE users;
SQL

echo
echo "PostgreSQL page size, page count, and relation size:"
psql "${DB_NAME}" <<'SQL'
SHOW block_size;
SELECT relname, relpages FROM pg_class WHERE relname = 'users';
SELECT pg_size_pretty(pg_relation_size('users')) AS users_table_size;
SQL

echo
echo "Timing PostgreSQL query"
/usr/bin/time -p psql "${DB_NAME}" -c "SELECT * FROM users;" > /tmp/postgres_users.out
ls -lh /tmp/postgres_users.out
