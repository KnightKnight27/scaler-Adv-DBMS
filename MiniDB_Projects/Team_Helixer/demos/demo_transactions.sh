#!/usr/bin/env bash
# Transaction demo: BEGIN / ABORT rolls back changes; BEGIN / COMMIT persists.
set -e
cd "$(dirname "$0")/.."
BIN=./build/minidb
DB=txndemo
rm -f $DB.db $DB.wal

$BIN $DB <<'SQL'
CREATE TABLE t (id INT, v INT, PRIMARY KEY (id))
INSERT INTO t VALUES (1, 10)
SELECT * FROM t
BEGIN
INSERT INTO t VALUES (2, 20)
INSERT INTO t VALUES (3, 30)
SELECT * FROM t
ABORT
SELECT * FROM t
BEGIN
INSERT INTO t VALUES (4, 40)
COMMIT
SELECT * FROM t
.exit
SQL

rm -f $DB.db $DB.wal
echo
echo "Notice: rows 2 and 3 vanished after ABORT; row 4 persisted after COMMIT."
