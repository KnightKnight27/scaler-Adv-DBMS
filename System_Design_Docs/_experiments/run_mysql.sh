#!/bin/bash
# InnoDB internals experiment harness.
# Initializes a throwaway MySQL datadir, starts mysqld, runs InnoDB-internal
# inspection queries, and tears it down. Output -> mysql_experiments.txt
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DATADIR="$HERE/mysqldata"
PORT=33099
SOCK="/tmp/adbms_mysql_$PORT.sock"
OUT="$HERE/mysql_experiments.txt"
MYSQLD=/opt/homebrew/opt/mysql/bin/mysqld
MYSQL=/opt/homebrew/opt/mysql/bin/mysql
ERR="$HERE/mysql_server.log"
MYSQLD_PID=""

m() { "$MYSQL" --socket="$SOCK" -uroot "$@"; }

cleanup() {
  if [ -S "$SOCK" ]; then
    "$MYSQL" --socket="$SOCK" -uroot -e "SHUTDOWN" >/dev/null 2>&1 || true
  fi
  if [ -n "${MYSQLD_PID:-}" ]; then
    wait "$MYSQLD_PID" >/dev/null 2>&1 || true
  fi
  rm -rf "$DATADIR" "$SOCK" "$ERR"
}

# --- clean slate ---
cleanup
mkdir -p "$DATADIR"
trap cleanup EXIT

{
echo "############################################################"
echo "# MySQL / InnoDB internals experiment"
"$MYSQLD" --version
echo "############################################################"
} > "$OUT"

# --- initialize + start ---
"$MYSQLD" --no-defaults --initialize-insecure --datadir="$DATADIR" \
  --log-error="$ERR" >>"$OUT" 2>&1
"$MYSQLD" --no-defaults --datadir="$DATADIR" --socket="$SOCK" --port="$PORT" \
  --skip-networking=0 --log-error="$ERR" \
  --innodb_buffer_pool_size=64M --innodb_print_all_deadlocks=ON &
MYSQLD_PID=$!

# wait for socket
for i in $(seq 1 60); do [ -S "$SOCK" ] && break; sleep 0.5; done
if [ ! -S "$SOCK" ]; then
  echo "mysqld did not create $SOCK within 30 seconds; see $ERR" >>"$OUT"
  if [ -f "$ERR" ]; then
    echo "===== mysql_server.log =====" >>"$OUT"
    cat "$ERR" >>"$OUT"
  fi
  exit 1
fi

# --- schema + data ---
m <<'SQL' >>"$OUT" 2>&1
SET SESSION cte_max_recursion_depth = 1000000;
CREATE DATABASE lab;
USE lab;
SET SESSION cte_max_recursion_depth = 1000000;
CREATE TABLE authors(
  id   INT PRIMARY KEY,
  name VARCHAR(64) NOT NULL
) ENGINE=InnoDB;
CREATE TABLE books(
  id        INT PRIMARY KEY,            -- clustered index key
  author_id INT NOT NULL,
  title     VARCHAR(64) NOT NULL,
  year      INT,
  KEY idx_author (author_id),           -- secondary index
  KEY idx_year   (year)
) ENGINE=InnoDB;
INSERT INTO authors
  SELECT n, CONCAT('Author ', n)
  FROM (WITH RECURSIVE s(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM s WHERE n<50) SELECT n FROM s) t;
INSERT INTO books
  SELECT n, 1+(n%50), CONCAT('Title ', n), 1970+(n%50)
  FROM (WITH RECURSIVE s(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM s WHERE n<200000) SELECT n FROM s) t;
ANALYZE TABLE books;
SQL

run() { echo; echo "================================================================"; echo "$1"; echo "================================================================"; }

{
run "EXPERIMENT 1: clustered (PK) lookup — touches ONLY the clustered index"
m --raw lab -e "EXPLAIN FORMAT=TREE SELECT * FROM books WHERE id=4242;"

run "EXPERIMENT 2: secondary index lookup — index range + back-to-clustered lookup"
m --raw lab -e "EXPLAIN FORMAT=TREE SELECT * FROM books WHERE author_id=7 LIMIT 5;"
echo "-- A COVERING secondary-index query needs no clustered-index access (Extra: Using index):"
m lab -e "EXPLAIN SELECT author_id FROM books WHERE author_id=7;"

run "EXPERIMENT 3: storage geometry (page size, row format, clustered layout)"
m -e "SELECT @@innodb_page_size AS page_bytes, @@innodb_default_row_format AS row_format;"
m -e "SELECT TABLE_NAME, ROW_FORMAT, TABLE_ROWS, DATA_LENGTH, INDEX_LENGTH
      FROM information_schema.TABLES WHERE TABLE_SCHEMA='lab';"
m -e "SELECT NAME, SPACE, N_COLS, ROW_FORMAT FROM information_schema.INNODB_TABLES WHERE NAME LIKE 'lab/%';"
echo "-- Index types: type 3 = clustered PK, type 0/1 = secondary:"
m -e "SELECT t.NAME AS tbl, i.NAME AS idx, i.TYPE, i.N_FIELDS
      FROM information_schema.INNODB_INDEXES i
      JOIN information_schema.INNODB_TABLES t ON i.TABLE_ID=t.TABLE_ID
      WHERE t.NAME='lab/books';"

run "EXPERIMENT 4: redo log + undo / buffer pool configuration"
m -e "SELECT VARIABLE_NAME, VARIABLE_VALUE FROM performance_schema.global_variables
      WHERE VARIABLE_NAME IN
      ('innodb_redo_log_capacity','innodb_buffer_pool_size','innodb_buffer_pool_instances',
       'innodb_flush_log_at_trx_commit','innodb_undo_tablespaces','innodb_undo_directory',
       'innodb_doublewrite') ORDER BY VARIABLE_NAME;"
m -e "SELECT VARIABLE_NAME, VARIABLE_VALUE FROM performance_schema.global_status
      WHERE VARIABLE_NAME IN
      ('Innodb_buffer_pool_pages_total','Innodb_buffer_pool_pages_data',
       'Innodb_buffer_pool_pages_dirty','Innodb_buffer_pool_read_requests',
       'Innodb_buffer_pool_reads','Innodb_os_log_written','Innodb_redo_log_enabled')
      ORDER BY VARIABLE_NAME;"
echo "-- Buffer pool hit ratio = 1 - reads/read_requests (computed from the two counters above)."

run "EXPERIMENT 5: default isolation level (InnoDB) + MVCC read view"
m -e "SELECT @@global.transaction_isolation AS global_iso, @@session.transaction_isolation AS session_iso;"
} >> "$OUT" 2>&1

run "EXPERIMENT 6: ROW LOCKS + GAP LOCKS under REPEATABLE READ" >> "$OUT"
# A single connection: take range locks, then inspect performance_schema.data_locks.
m lab >> "$OUT" 2>&1 <<'SQL'
SET SESSION transaction_isolation='REPEATABLE-READ';
START TRANSACTION;
-- Range predicate on the PK acquires next-key (record + gap) locks:
SELECT id FROM books WHERE id BETWEEN 100 AND 105 FOR UPDATE;
SELECT ENGINE, OBJECT_NAME, INDEX_NAME, LOCK_TYPE, LOCK_MODE, LOCK_STATUS, LOCK_DATA
  FROM performance_schema.data_locks
  WHERE OBJECT_NAME='books' ORDER BY LOCK_MODE, LOCK_DATA;
-- Insert into a gap a concurrent txn would block on (shown via lock modes above):
ROLLBACK;
SQL

run "EXPERIMENT 7: SHOW ENGINE INNODB STATUS (LOG + BUFFER POOL + TRANSACTIONS)" >> "$OUT"
m -E -e "SHOW ENGINE INNODB STATUS" >> "$OUT" 2>&1

# --- on-disk files ---
{
run "EXPERIMENT 8: on-disk InnoDB files (.ibd per-table tablespace, redo, undo, doublewrite)"
ls -la "$DATADIR" | grep -E "ib_|undo|#innodb|ibdata" || true
echo "-- per-table tablespace for books:"
ls -la "$DATADIR/lab/" 2>/dev/null
echo "-- redo log files:"
ls -la "$DATADIR/#innodb_redo/" 2>/dev/null | head
} >> "$OUT" 2>&1

# --- teardown ---
m -e "SHUTDOWN" 2>/dev/null || true
wait "$MYSQLD_PID" 2>/dev/null || true
MYSQLD_PID=""
trap - EXIT
rm -rf "$DATADIR" "$SOCK" "$ERR"
echo "DONE. $(wc -l < "$OUT") lines -> $OUT"
