#!/usr/bin/env bash
# Crash-recovery demo: insert committed data, KILL the process (simulating a
# crash before clean shutdown), then restart and show the data was recovered
# from the write-ahead log.
set -e
cd "$(dirname "$0")/.."
BIN=./build/minidb
DB=recoverydemo
rm -f $DB.db $DB.wal

echo "=================================================================="
echo " Session 1: create table, insert COMMITTED rows, then CRASH (kill -9)"
echo "=================================================================="
# Keep stdin open (sleep) so the shell stays alive until we kill it.
( printf "CREATE TABLE acct (id INT, bal INT, PRIMARY KEY (id))\n\
INSERT INTO acct VALUES (1, 100)\n\
INSERT INTO acct VALUES (2, 200)\n\
INSERT INTO acct VALUES (3, 300)\n\
SELECT * FROM acct\n"; sleep 3 ) | $BIN $DB &
PID=$!
sleep 1.2
kill -9 $PID 2>/dev/null || true
wait $PID 2>/dev/null || true
echo
echo ">>> Process killed (crash). The .db file is scratch; only the WAL is durable."
echo

echo "=================================================================="
echo " Session 2: RESTART -> recovery replays the WAL, committed rows return"
echo "=================================================================="
printf "SELECT * FROM acct\n.exit\n" | $BIN $DB

rm -f $DB.db $DB.wal
echo
echo "Recovery demo complete: committed transactions survived the crash."
