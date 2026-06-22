#!/bin/bash
# psql_explore.sh - PostgreSQL page/performance exploration
# Praveen Kumar | 24BCS10048

DB="lab2_test"

echo "=== PostgreSQL Exploration ==="
echo ""

# create database and table
psql -U postgres -c "DROP DATABASE IF EXISTS $DB;"
psql -U postgres -c "CREATE DATABASE $DB;"

psql -U postgres -d $DB <<EOF
CREATE TABLE IF NOT EXISTS users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    email TEXT,
    age INTEGER
);

INSERT INTO users (name, email, age)
SELECT
    'user_' || g,
    'user_' || g || '@example.com',
    18 + (random() * 50)::int
FROM generate_series(1, 1000) AS g;
EOF

echo "--- Page size (block_size) ---"
psql -U postgres -d $DB -c "SHOW block_size;"

echo "--- Page count (relation size / block_size) ---"
psql -U postgres -d $DB -c "
SELECT
    pg_relation_size('users') AS relation_bytes,
    pg_relation_size('users') / current_setting('block_size')::int AS page_count
;"

echo "--- Shared buffers ---"
psql -U postgres -d $DB -c "SHOW shared_buffers;"

echo ""
echo "=== Query timing ==="
psql -U postgres -d $DB -c "\timing on" -c "SELECT * FROM users;" > /dev/null

echo ""
echo "--- With EXPLAIN ANALYZE ---"
psql -U postgres -d $DB -c "EXPLAIN ANALYZE SELECT * FROM users;"

# cleanup
psql -U postgres -c "DROP DATABASE IF EXISTS $DB;"

echo ""
echo "Done."
