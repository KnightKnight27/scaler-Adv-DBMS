#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$DOC_DIR/../.." && pwd)"
WORK_DIR="$ROOT_DIR/.local/mysql-innodb"
DATADIR="$WORK_DIR/data"
RUNDIR="$WORK_DIR/run"
TMPDIR="$WORK_DIR/tmp"
SOCKET="$RUNDIR/mysql.sock"
PIDFILE="$RUNDIR/mysql.pid"
LOGFILE="$WORK_DIR/mysql.log"
OUTPUT="$DOC_DIR/EXPERIMENT_RESULTS.md"

rm -rf "$WORK_DIR"
mkdir -p "$DATADIR" "$RUNDIR" "$TMPDIR"

mysqld --no-defaults \
  --initialize-insecure \
  --datadir="$DATADIR" \
  --basedir=/opt/homebrew/Cellar/mysql/9.5.0_2 \
  --log-error-verbosity=2 >/dev/null 2>"$WORK_DIR/init.log"

mysqld --no-defaults \
  --datadir="$DATADIR" \
  --socket="$SOCKET" \
  --pid-file="$PIDFILE" \
  --tmpdir="$TMPDIR" \
  --skip-networking \
  --mysqlx=OFF \
  --log-error="$LOGFILE" &
MYSQLD_PID=$!

cleanup() {
  if mysqladmin --socket="$SOCKET" -uroot ping >/dev/null 2>&1; then
    mysqladmin --socket="$SOCKET" -uroot shutdown >/dev/null 2>&1 || true
  fi
  if kill -0 "$MYSQLD_PID" >/dev/null 2>&1; then
    wait "$MYSQLD_PID" || true
  fi
}
trap cleanup EXIT

ready=0
for _ in $(seq 1 60); do
  if mysqladmin --socket="$SOCKET" -uroot ping >/dev/null 2>&1; then
    ready=1
    break
  fi
  sleep 1
done

if [ "$ready" -ne 1 ]; then
  cat "$LOGFILE" >&2 || true
  exit 1
fi

mysql --socket="$SOCKET" -uroot --table >/dev/null <<'SQL'
CREATE DATABASE adbms_innodb_lab;
USE adbms_innodb_lab;

CREATE TABLE digits (d int PRIMARY KEY) ENGINE=InnoDB;
INSERT INTO digits VALUES (0), (1), (2), (3), (4), (5), (6), (7), (8), (9);

CREATE TABLE customers (
  customer_id int NOT NULL,
  email varchar(100) NOT NULL,
  city varchar(32) NOT NULL,
  tier varchar(16) NOT NULL,
  balance decimal(12, 2) NOT NULL,
  created_at date NOT NULL,
  PRIMARY KEY (customer_id),
  UNIQUE KEY uq_customers_email (email),
  KEY idx_city_created (city, created_at),
  KEY idx_tier_balance (tier, balance)
) ENGINE=InnoDB;

INSERT INTO customers (customer_id, email, city, tier, balance, created_at)
SELECT
  n,
  CONCAT('user', n, '@example.com'),
  CASE
    WHEN n % 5 = 0 THEN 'Bengaluru'
    WHEN n % 5 = 1 THEN 'Mumbai'
    WHEN n % 5 = 2 THEN 'Delhi'
    WHEN n % 5 = 3 THEN 'Hyderabad'
    ELSE 'Pune'
  END,
  CASE
    WHEN n % 10 = 0 THEN 'enterprise'
    WHEN n % 3 = 0 THEN 'plus'
    ELSE 'standard'
  END,
  ROUND(((n * 19) % 200000) / 100, 2),
  DATE_ADD('2024-01-01', INTERVAL ((n * 7) % 720) DAY)
FROM (
  SELECT a.d + b.d * 10 + c.d * 100 + d.d * 1000 + 1 AS n
  FROM digits a
  CROSS JOIN digits b
  CROSS JOIN digits c
  CROSS JOIN digits d
) seq
WHERE n <= 5000;

CREATE TABLE orders (
  order_id int NOT NULL,
  customer_id int NOT NULL,
  order_date date NOT NULL,
  amount decimal(12, 2) NOT NULL,
  status varchar(16) NOT NULL,
  PRIMARY KEY (order_id),
  KEY idx_orders_customer_date (customer_id, order_date),
  KEY idx_orders_status_date (status, order_date),
  CONSTRAINT fk_orders_customer FOREIGN KEY (customer_id) REFERENCES customers(customer_id)
) ENGINE=InnoDB;

INSERT INTO orders (order_id, customer_id, order_date, amount, status)
SELECT
  n,
  ((n * 37) % 5000) + 1,
  DATE_ADD('2025-01-01', INTERVAL ((n * 13) % 540) DAY),
  ROUND(((n * 17) % 10000) / 100, 2),
  CASE
    WHEN n % 11 = 0 THEN 'cancelled'
    WHEN n % 7 = 0 THEN 'returned'
    ELSE 'paid'
  END
FROM (
  SELECT a.d + b.d * 10 + c.d * 100 + d.d * 1000 + e.d * 10000 + 1 AS n
  FROM digits a
  CROSS JOIN digits b
  CROSS JOIN digits c
  CROSS JOIN digits d
  CROSS JOIN digits e
) seq
WHERE n <= 50000;

ANALYZE TABLE customers, orders;
SQL

versions="$WORK_DIR/versions.txt"
metadata="$WORK_DIR/metadata.txt"
indexes="$WORK_DIR/indexes.txt"
plans="$WORK_DIR/plans.txt"
redo="$WORK_DIR/redo.txt"
redo_before="$WORK_DIR/redo_before.txt"
redo_after="$WORK_DIR/redo_after.txt"
locks="$WORK_DIR/locks.txt"
status="$WORK_DIR/status.txt"

{
  mysqld --version
  mysql --version
  mysql --socket="$SOCKET" -uroot --table -e "SELECT VERSION() AS version, @@transaction_isolation AS isolation_level, @@default_storage_engine AS default_engine, @@innodb_page_size AS innodb_page_size, @@innodb_flush_log_at_trx_commit AS flush_log_at_commit;"
} > "$versions"

mysql --socket="$SOCKET" -uroot --table adbms_innodb_lab > "$metadata" <<'SQL'
SHOW CREATE TABLE customers;
SELECT TABLE_NAME, ENGINE, ROW_FORMAT, TABLE_ROWS, DATA_LENGTH, INDEX_LENGTH
FROM information_schema.TABLES
WHERE TABLE_SCHEMA = 'adbms_innodb_lab'
ORDER BY TABLE_NAME;
SQL

mysql --socket="$SOCKET" -uroot --table adbms_innodb_lab > "$indexes" <<'SQL'
SELECT TABLE_NAME, INDEX_NAME, NON_UNIQUE, SEQ_IN_INDEX, COLUMN_NAME
FROM information_schema.STATISTICS
WHERE TABLE_SCHEMA = 'adbms_innodb_lab'
  AND TABLE_NAME IN ('customers', 'orders')
ORDER BY TABLE_NAME, INDEX_NAME, SEQ_IN_INDEX;
SQL

mysql --socket="$SOCKET" -uroot --table adbms_innodb_lab > "$plans" <<'SQL'
EXPLAIN FORMAT=TREE
SELECT customer_id, email, balance
FROM customers
WHERE customer_id = 2048;

EXPLAIN FORMAT=TREE
SELECT customer_id, email, balance
FROM customers
WHERE email = 'user2048@example.com';

EXPLAIN FORMAT=TREE
SELECT c.city, COUNT(*) AS order_count, ROUND(SUM(o.amount), 2) AS revenue
FROM customers c
JOIN orders o ON o.customer_id = c.customer_id
WHERE c.city = 'Bengaluru'
  AND o.order_date >= '2026-01-01'
GROUP BY c.city;
SQL

mysql --socket="$SOCKET" -uroot --vertical adbms_innodb_lab -e "SHOW ENGINE INNODB STATUS" > "$redo_before"
mysql --socket="$SOCKET" -uroot --table adbms_innodb_lab >/dev/null <<'SQL'
START TRANSACTION;
UPDATE customers
SET balance = balance + 10
WHERE customer_id BETWEEN 100 AND 200;
COMMIT;
SQL
mysql --socket="$SOCKET" -uroot --vertical adbms_innodb_lab -e "SHOW ENGINE INNODB STATUS" > "$redo_after"
{
  printf "Before update:\n"
  grep -E 'Log sequence number|Log flushed up to|Pages flushed up to|Last checkpoint at' "$redo_before" || true
  printf "\nAfter update:\n"
  grep -E 'Log sequence number|Log flushed up to|Pages flushed up to|Last checkpoint at' "$redo_after" || true
} > "$redo"

mysql --socket="$SOCKET" -uroot --table adbms_innodb_lab > "$locks" <<'SQL'
SELECT @@transaction_isolation AS isolation_level;
EXPLAIN FORMAT=TREE
SELECT *
FROM customers
WHERE customer_id BETWEEN 100 AND 200
FOR UPDATE;
SQL

mysql --socket="$SOCKET" -uroot --table adbms_innodb_lab > "$status" <<'SQL'
SHOW GLOBAL STATUS LIKE 'Innodb_buffer_pool_reads';
SHOW GLOBAL STATUS LIKE 'Innodb_buffer_pool_read_requests';
SHOW GLOBAL STATUS LIKE 'Innodb_data_reads';
SHOW GLOBAL STATUS LIKE 'Innodb_data_writes';
SQL

{
  printf "# MySQL InnoDB Experiment Results\n\n"
  printf "Generated locally by \`experiments/run_experiments.sh\`.\n\n"
  printf "## Tool Versions And Settings\n\n"
  printf "\`\`\`text\n"
  cat "$versions"
  printf "\`\`\`\n\n"
  printf "## Table Metadata\n\n"
  printf "\`\`\`text\n"
  cat "$metadata"
  printf "\`\`\`\n\n"
  printf "## Index Metadata\n\n"
  printf "\`\`\`text\n"
  cat "$indexes"
  printf "\`\`\`\n\n"
  printf "## Query Plans\n\n"
  printf "\`\`\`text\n"
  cat "$plans"
  printf "\`\`\`\n\n"
  printf "## Redo LSN Observation\n\n"
  printf "\`\`\`text\n"
  cat "$redo"
  printf "\`\`\`\n\n"
  printf "## Locking Read Observation\n\n"
  printf "\`\`\`text\n"
  cat "$locks"
  printf "\`\`\`\n\n"
  printf "## InnoDB Status Counters\n\n"
  printf "\`\`\`text\n"
  cat "$status"
  printf "\`\`\`\n"
} > "$OUTPUT"

printf "Wrote %s\n" "$OUTPUT"
