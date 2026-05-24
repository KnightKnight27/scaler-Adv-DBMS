#!/usr/bin/env bash
# Compare SQLite, PostgreSQL, and DuckDB on the same schema and queries.
#
# Schema:
#   users(user_id, country, signup_date, is_premium)
#   orders(order_id, user_id, order_date, amount, status)
#   Indexes: orders(user_id), orders(order_date), orders(status), users(country)
#
# Three queries (each run 3 times per DB; report the average):
#   Q1 (analytical join + group by): top 10 countries by total order amount
#                                    among premium users.
#   Q2 (filter + group):             order count by status in a date window.
#   Q3 (selective point lookup):     all orders for a single user_id.
#
# Usage:
#   ./compare.sh                # uses defaults (100000 users / 1000000 orders)
#   USERS=10000 ORDERS=100000 ./compare.sh   # smaller run

set -uo pipefail
cd "$(dirname "$0")"

USERS=${USERS:-100000}
ORDERS=${ORDERS:-1000000}
PG_DB=${PG_DB:-db_compare}
PG_USER=${PG_USER:-$USER}

SQLITE_DB="sqlite_test.db"
DUCKDB_DB="duck_test.db"

# Cutoff date used in Q2; pick the user_id used in Q3.
Q2_CUTOFF="2025-01-01"
Q3_USER_ID=4242

banner() {
    echo
    echo "========================================="
    echo "$1"
    echo "========================================="
}

have() { command -v "$1" >/dev/null 2>&1; }

# --- Step 1: data generation ------------------------------------------------
banner "Generating data (USERS=$USERS, ORDERS=$ORDERS)"
if [ ! -f users.csv ] || [ ! -f orders.csv ]; then
    python3 gen_data.py "$USERS" "$ORDERS"
else
    echo "users.csv and orders.csv already exist; reusing them."
    echo "Delete them to regenerate."
fi
echo
echo "CSV sizes:"
ls -lh users.csv orders.csv

# --- Step 2: SQLite ---------------------------------------------------------
if have sqlite3; then
    banner "SQLite Experiment"
    rm -f "$SQLITE_DB"

    sqlite3 "$SQLITE_DB" <<EOF
CREATE TABLE users(
    user_id     INTEGER PRIMARY KEY,
    country     TEXT,
    signup_date TEXT,
    is_premium  INTEGER
);
CREATE TABLE orders(
    order_id   INTEGER PRIMARY KEY,
    user_id    INTEGER,
    order_date TEXT,
    amount     REAL,
    status     TEXT
);
.mode csv
.import --skip 1 users.csv users
.import --skip 1 orders.csv orders
CREATE INDEX idx_orders_user_id    ON orders(user_id);
CREATE INDEX idx_orders_order_date ON orders(order_date);
CREATE INDEX idx_orders_status     ON orders(status);
CREATE INDEX idx_users_country     ON users(country);
ANALYZE;
EOF

    echo
    echo "SQLite DB file size:"
    ls -lh "$SQLITE_DB"

    echo
    echo "SQLite internal numbers:"
    sqlite3 "$SQLITE_DB" <<EOF
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
EOF

    echo
    echo "SQLite query timings (run each 3x):"
    sqlite3 "$SQLITE_DB" <<EOF
.timer on
.headers off
-- Q1
SELECT u.country, SUM(o.amount) FROM users u JOIN orders o ON o.user_id=u.user_id WHERE u.is_premium=1 GROUP BY u.country ORDER BY 2 DESC LIMIT 10;
SELECT u.country, SUM(o.amount) FROM users u JOIN orders o ON o.user_id=u.user_id WHERE u.is_premium=1 GROUP BY u.country ORDER BY 2 DESC LIMIT 10;
SELECT u.country, SUM(o.amount) FROM users u JOIN orders o ON o.user_id=u.user_id WHERE u.is_premium=1 GROUP BY u.country ORDER BY 2 DESC LIMIT 10;
-- Q2
SELECT status, COUNT(*) FROM orders WHERE order_date >= '$Q2_CUTOFF' GROUP BY status;
SELECT status, COUNT(*) FROM orders WHERE order_date >= '$Q2_CUTOFF' GROUP BY status;
SELECT status, COUNT(*) FROM orders WHERE order_date >= '$Q2_CUTOFF' GROUP BY status;
-- Q3
SELECT * FROM orders WHERE user_id = $Q3_USER_ID;
SELECT * FROM orders WHERE user_id = $Q3_USER_ID;
SELECT * FROM orders WHERE user_id = $Q3_USER_ID;
EOF
else
    echo "WARN: sqlite3 not found, skipping SQLite section."
fi

# --- Step 3: PostgreSQL -----------------------------------------------------
if have psql; then
    banner "PostgreSQL Experiment (db=$PG_DB user=$PG_USER)"
    psql -U "$PG_USER" -d postgres -c "DROP DATABASE IF EXISTS $PG_DB;" >/dev/null
    psql -U "$PG_USER" -d postgres -c "CREATE DATABASE $PG_DB;"     >/dev/null

    psql -U "$PG_USER" -d "$PG_DB" <<EOF
CREATE TABLE users(
    user_id     INTEGER PRIMARY KEY,
    country     TEXT,
    signup_date DATE,
    is_premium  INTEGER
);
CREATE TABLE orders(
    order_id   INTEGER PRIMARY KEY,
    user_id    INTEGER,
    order_date DATE,
    amount     NUMERIC(10,2),
    status     TEXT
);
\copy users  FROM 'users.csv'  CSV HEADER
\copy orders FROM 'orders.csv' CSV HEADER
CREATE INDEX idx_orders_user_id    ON orders(user_id);
CREATE INDEX idx_orders_order_date ON orders(order_date);
CREATE INDEX idx_orders_status     ON orders(status);
CREATE INDEX idx_users_country     ON users(country);
ANALYZE;
EOF

    echo
    echo "PostgreSQL DB size + relation sizes:"
    psql -U "$PG_USER" -d "$PG_DB" <<EOF
SELECT pg_size_pretty(pg_database_size('$PG_DB')) AS db_size;
SELECT relname, pg_size_pretty(pg_relation_size(oid))
FROM pg_class WHERE relname IN ('users','orders');
SHOW block_size;
SHOW shared_buffers;
SHOW effective_cache_size;
EOF

    echo
    echo "PostgreSQL query timings (run each 3x):"
    psql -U "$PG_USER" -d "$PG_DB" <<EOF
\timing on
SELECT u.country, SUM(o.amount) FROM users u JOIN orders o ON o.user_id=u.user_id WHERE u.is_premium=1 GROUP BY u.country ORDER BY 2 DESC LIMIT 10;
SELECT u.country, SUM(o.amount) FROM users u JOIN orders o ON o.user_id=u.user_id WHERE u.is_premium=1 GROUP BY u.country ORDER BY 2 DESC LIMIT 10;
SELECT u.country, SUM(o.amount) FROM users u JOIN orders o ON o.user_id=u.user_id WHERE u.is_premium=1 GROUP BY u.country ORDER BY 2 DESC LIMIT 10;
SELECT status, COUNT(*) FROM orders WHERE order_date >= DATE '$Q2_CUTOFF' GROUP BY status;
SELECT status, COUNT(*) FROM orders WHERE order_date >= DATE '$Q2_CUTOFF' GROUP BY status;
SELECT status, COUNT(*) FROM orders WHERE order_date >= DATE '$Q2_CUTOFF' GROUP BY status;
SELECT * FROM orders WHERE user_id = $Q3_USER_ID;
SELECT * FROM orders WHERE user_id = $Q3_USER_ID;
SELECT * FROM orders WHERE user_id = $Q3_USER_ID;
EOF

    echo
    echo "PostgreSQL processes (should see server processes):"
    ps aux | grep -E '[p]ostgres' | head
else
    echo "WARN: psql not found, skipping PostgreSQL section."
fi

# --- Step 4: DuckDB ---------------------------------------------------------
if have duckdb; then
    banner "DuckDB Experiment"
    rm -f "$DUCKDB_DB"

    duckdb "$DUCKDB_DB" <<EOF
CREATE TABLE users AS SELECT * FROM read_csv_auto('users.csv', HEADER=TRUE);
CREATE TABLE orders AS SELECT * FROM read_csv_auto('orders.csv', HEADER=TRUE);
CREATE INDEX idx_orders_user_id    ON orders(user_id);
CREATE INDEX idx_orders_order_date ON orders(order_date);
CREATE INDEX idx_orders_status     ON orders(status);
CREATE INDEX idx_users_country     ON users(country);
EOF

    echo
    echo "DuckDB DB file size:"
    ls -lh "$DUCKDB_DB"

    echo
    echo "DuckDB internal numbers:"
    duckdb "$DUCKDB_DB" <<EOF
PRAGMA database_size;
EOF

    echo
    echo "DuckDB query timings (run each 3x):"
    duckdb "$DUCKDB_DB" <<EOF
.timer on
SELECT u.country, SUM(o.amount) FROM users u JOIN orders o ON o.user_id=u.user_id WHERE u.is_premium=1 GROUP BY u.country ORDER BY 2 DESC LIMIT 10;
SELECT u.country, SUM(o.amount) FROM users u JOIN orders o ON o.user_id=u.user_id WHERE u.is_premium=1 GROUP BY u.country ORDER BY 2 DESC LIMIT 10;
SELECT u.country, SUM(o.amount) FROM users u JOIN orders o ON o.user_id=u.user_id WHERE u.is_premium=1 GROUP BY u.country ORDER BY 2 DESC LIMIT 10;
SELECT status, COUNT(*) FROM orders WHERE order_date >= DATE '$Q2_CUTOFF' GROUP BY status;
SELECT status, COUNT(*) FROM orders WHERE order_date >= DATE '$Q2_CUTOFF' GROUP BY status;
SELECT status, COUNT(*) FROM orders WHERE order_date >= DATE '$Q2_CUTOFF' GROUP BY status;
SELECT * FROM orders WHERE user_id = $Q3_USER_ID;
SELECT * FROM orders WHERE user_id = $Q3_USER_ID;
SELECT * FROM orders WHERE user_id = $Q3_USER_ID;
EOF
else
    echo "WARN: duckdb not found, skipping DuckDB section."
fi

banner "Experiment Completed"
