#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$DOC_DIR/../.." && pwd)"
WORK_DIR="$ROOT_DIR/.local/postgresql-vs-sqlite"
PGDATA="$WORK_DIR/pgdata"
PGPORT="${PGPORT:-55432}"
PGSOCKET="${PGSOCKET:-/tmp/adbms-pg-socket-${USER:-local}}"
PGDATABASE="adbms_architecture_lab"
SQLITEDB="$WORK_DIR/retail.sqlite"
OUTPUT="$DOC_DIR/EXPERIMENT_RESULTS.md"

mkdir -p "$WORK_DIR" "$PGSOCKET"

if [ ! -s "$PGDATA/PG_VERSION" ]; then
  initdb -D "$PGDATA" --auth=trust --username=postgres --no-locale >/dev/null
fi

pg_started_by_script=0
if ! pg_ctl -D "$PGDATA" status >/dev/null 2>&1; then
  pg_ctl -D "$PGDATA" -l "$WORK_DIR/postgres.log" -o "-k $PGSOCKET -p $PGPORT" -w start >/dev/null
  pg_started_by_script=1
fi

cleanup() {
  if [ "$pg_started_by_script" -eq 1 ]; then
    pg_ctl -D "$PGDATA" -w stop -m fast >/dev/null
  fi
}
trap cleanup EXIT

dropdb -h "$PGSOCKET" -p "$PGPORT" -U postgres --if-exists "$PGDATABASE" >/dev/null 2>&1 || true
createdb -h "$PGSOCKET" -p "$PGPORT" -U postgres "$PGDATABASE"

psql -h "$PGSOCKET" -p "$PGPORT" -U postgres -d "$PGDATABASE" -X -v ON_ERROR_STOP=1 >/dev/null <<'SQL'
CREATE TABLE customers (
  customer_id integer PRIMARY KEY,
  city text NOT NULL,
  tier text NOT NULL
);

CREATE TABLE orders (
  order_id integer PRIMARY KEY,
  customer_id integer NOT NULL REFERENCES customers(customer_id),
  order_date date NOT NULL,
  amount numeric(10, 2) NOT NULL,
  status text NOT NULL
);

INSERT INTO customers (customer_id, city, tier)
SELECT
  g,
  CASE
    WHEN g % 5 = 0 THEN 'Bengaluru'
    WHEN g % 5 = 1 THEN 'Mumbai'
    WHEN g % 5 = 2 THEN 'Delhi'
    WHEN g % 5 = 3 THEN 'Hyderabad'
    ELSE 'Pune'
  END,
  CASE
    WHEN g % 10 = 0 THEN 'enterprise'
    WHEN g % 3 = 0 THEN 'plus'
    ELSE 'standard'
  END
FROM generate_series(1, 5000) AS g;

INSERT INTO orders (order_id, customer_id, order_date, amount, status)
SELECT
  g,
  ((g * 37) % 5000) + 1,
  DATE '2025-01-01' + ((g * 13) % 540),
  (((g * 17) % 10000) / 100.0)::numeric(10, 2),
  CASE
    WHEN g % 11 = 0 THEN 'cancelled'
    WHEN g % 7 = 0 THEN 'returned'
    ELSE 'paid'
  END
FROM generate_series(1, 50000) AS g;

CREATE INDEX idx_customers_city ON customers (city);
CREATE INDEX idx_orders_customer_date ON orders (customer_id, order_date);
CREATE INDEX idx_orders_date ON orders (order_date);
ANALYZE;
SQL

rm -f "$SQLITEDB" "$SQLITEDB-wal" "$SQLITEDB-shm"
sqlite3 "$SQLITEDB" >/dev/null <<'SQL'
PRAGMA page_size = 4096;
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;

CREATE TABLE customers (
  customer_id INTEGER PRIMARY KEY,
  city TEXT NOT NULL,
  tier TEXT NOT NULL
);

CREATE TABLE orders (
  order_id INTEGER PRIMARY KEY,
  customer_id INTEGER NOT NULL REFERENCES customers(customer_id),
  order_date TEXT NOT NULL,
  amount NUMERIC NOT NULL,
  status TEXT NOT NULL
);

WITH digits(d) AS (
  VALUES (0), (1), (2), (3), (4), (5), (6), (7), (8), (9)
),
seq(g) AS (
  SELECT a.d + b.d * 10 + c.d * 100 + d.d * 1000 + 1
  FROM digits a
  CROSS JOIN digits b
  CROSS JOIN digits c
  CROSS JOIN digits d
  WHERE a.d + b.d * 10 + c.d * 100 + d.d * 1000 + 1 <= 5000
)
INSERT INTO customers (customer_id, city, tier)
SELECT
  g,
  CASE
    WHEN g % 5 = 0 THEN 'Bengaluru'
    WHEN g % 5 = 1 THEN 'Mumbai'
    WHEN g % 5 = 2 THEN 'Delhi'
    WHEN g % 5 = 3 THEN 'Hyderabad'
    ELSE 'Pune'
  END,
  CASE
    WHEN g % 10 = 0 THEN 'enterprise'
    WHEN g % 3 = 0 THEN 'plus'
    ELSE 'standard'
  END
FROM seq;

WITH digits(d) AS (
  VALUES (0), (1), (2), (3), (4), (5), (6), (7), (8), (9)
),
seq(g) AS (
  SELECT a.d + b.d * 10 + c.d * 100 + d.d * 1000 + e.d * 10000 + 1
  FROM digits a
  CROSS JOIN digits b
  CROSS JOIN digits c
  CROSS JOIN digits d
  CROSS JOIN digits e
  WHERE a.d + b.d * 10 + c.d * 100 + d.d * 1000 + e.d * 10000 + 1 <= 50000
)
INSERT INTO orders (order_id, customer_id, order_date, amount, status)
SELECT
  g,
  ((g * 37) % 5000) + 1,
  date('2025-01-01', printf('+%d days', (g * 13) % 540)),
  round(((g * 17) % 10000) / 100.0, 2),
  CASE
    WHEN g % 11 = 0 THEN 'cancelled'
    WHEN g % 7 = 0 THEN 'returned'
    ELSE 'paid'
  END
FROM seq;

CREATE INDEX idx_customers_city ON customers (city);
CREATE INDEX idx_orders_customer_date ON orders (customer_id, order_date);
CREATE INDEX idx_orders_date ON orders (order_date);
ANALYZE;
SQL

postgres_plan="$WORK_DIR/postgres_plan.txt"
postgres_result="$WORK_DIR/postgres_result.txt"
sqlite_plan="$WORK_DIR/sqlite_plan.txt"
sqlite_result="$WORK_DIR/sqlite_result.txt"
sqlite_pragmas="$WORK_DIR/sqlite_pragmas.txt"

psql -h "$PGSOCKET" -p "$PGPORT" -U postgres -d "$PGDATABASE" -X -qAt >"$postgres_plan" <<'SQL'
EXPLAIN (ANALYZE, BUFFERS, TIMING OFF)
SELECT c.city, COUNT(*) AS order_count, ROUND(SUM(o.amount), 2) AS revenue
FROM customers c
JOIN orders o ON o.customer_id = c.customer_id
WHERE c.city = 'Bengaluru'
  AND o.order_date >= DATE '2026-01-01'
GROUP BY c.city;
SQL

psql -h "$PGSOCKET" -p "$PGPORT" -U postgres -d "$PGDATABASE" -X -q >"$postgres_result" <<'SQL'
SELECT c.city, COUNT(*) AS order_count, ROUND(SUM(o.amount), 2) AS revenue
FROM customers c
JOIN orders o ON o.customer_id = c.customer_id
WHERE c.city = 'Bengaluru'
  AND o.order_date >= DATE '2026-01-01'
GROUP BY c.city;
SQL

sqlite3 "$SQLITEDB" >"$sqlite_plan" <<'SQL'
.headers on
.mode column
EXPLAIN QUERY PLAN
SELECT c.city, COUNT(*) AS order_count, ROUND(SUM(o.amount), 2) AS revenue
FROM customers c
JOIN orders o ON o.customer_id = c.customer_id
WHERE c.city = 'Bengaluru'
  AND o.order_date >= '2026-01-01'
GROUP BY c.city;
SQL

sqlite3 "$SQLITEDB" >"$sqlite_result" <<'SQL'
.headers on
.mode column
SELECT c.city, COUNT(*) AS order_count, ROUND(SUM(o.amount), 2) AS revenue
FROM customers c
JOIN orders o ON o.customer_id = c.customer_id
WHERE c.city = 'Bengaluru'
  AND o.order_date >= '2026-01-01'
GROUP BY c.city;
SQL

sqlite3 "$SQLITEDB" >"$sqlite_pragmas" <<'SQL'
.headers on
.mode column
SELECT 'page_size' AS metric, page_size AS value FROM pragma_page_size
UNION ALL
SELECT 'page_count', page_count FROM pragma_page_count
UNION ALL
SELECT 'journal_mode', journal_mode FROM pragma_journal_mode
UNION ALL
SELECT 'freelist_count', freelist_count FROM pragma_freelist_count;
SQL

{
  printf "# PostgreSQL vs SQLite Experiment Results\n\n"
  printf "Generated locally by \`experiments/run_experiments.sh\`.\n\n"
  printf "## Tool Versions\n\n"
  printf "\`\`\`text\n"
  postgres --version
  psql --version
  printf "sqlite3 %s\n" "$(sqlite3 --version)"
  printf "\`\`\`\n\n"
  printf "## Workload\n\n"
  printf "%s\n" "- 5,000 customers across five cities."
  printf "%s\n" "- 50,000 orders with deterministic customer/date/status distribution."
  printf "%s\n" "- Indexes: \`customers(city)\`, \`orders(customer_id, order_date)\`, and \`orders(order_date)\`."
  printf "%s\n\n" "- Query: city-level revenue for Bengaluru orders from 2026-01-01 onward."
  printf "## PostgreSQL Query Result\n\n"
  printf "\`\`\`text\n"
  cat "$postgres_result"
  printf "\`\`\`\n\n"
  printf "## PostgreSQL EXPLAIN ANALYZE\n\n"
  printf "\`\`\`text\n"
  cat "$postgres_plan"
  printf "\`\`\`\n\n"
  printf "## SQLite Query Result\n\n"
  printf "\`\`\`text\n"
  cat "$sqlite_result"
  printf "\`\`\`\n\n"
  printf "## SQLite EXPLAIN QUERY PLAN\n\n"
  printf "\`\`\`text\n"
  cat "$sqlite_plan"
  printf "\`\`\`\n\n"
  printf "## SQLite File Metrics\n\n"
  printf "\`\`\`text\n"
  cat "$sqlite_pragmas"
  printf "\`\`\`\n"
} > "$OUTPUT"

printf "Wrote %s\n" "$OUTPUT"
