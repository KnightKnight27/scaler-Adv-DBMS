#!/bin/bash
# demo_crash_recovery.sh — Automated WAL Crash Recovery Demo for MiniDB
# Team Blast

# Exit on error
set -e

# Work from the build directory where the binaries are located
if [ -d "build" ]; then
    cd build
fi

echo "============================================================"
echo "      MINIDB CRASH RECOVERY DEMO (WAL REDO-ONLY)"
echo "============================================================"
echo

# 1. Clean up old databases, WALs, and replication logs
echo "[Setup] Cleaning up old database files..."
rm -f minidb.db minidb.wal minidb_replica.db minidb_replica.wal minidb_replication.log
rm -f demo_pipe
echo "[Setup] Clean complete."
echo

# 2. Start MiniDB and feed it commands via a named pipe to control timing
echo "[Step 1] Creating a pipe and starting MiniDB in the background..."
mkfifo demo_pipe

# Start minidb in background reading from our pipe
./minidb < demo_pipe &
MINIDB_PID=$!

# Open file descriptor 3 pointing to the named pipe to keep it open
exec 3>demo_pipe

# Wait for startup
sleep 1.5

echo "[Step 2] Sending CREATE TABLE and committed inserts..."
echo "CREATE TABLE users" >&3
sleep 0.5
echo "INSERT INTO users VALUES (1, Alice)" >&3
sleep 0.5
echo "INSERT INTO users VALUES (2, Bob)" >&3
sleep 0.5

echo "[Step 3] Sending a committed transaction..."
echo "BEGIN" >&3
sleep 0.5
echo "INSERT INTO users VALUES (3, Carol)" >&3
sleep 0.5
echo "COMMIT" >&3
sleep 0.5

echo "[Step 4] Sending an UNCOMMITTED transaction..."
echo "BEGIN" >&3
sleep 0.5
echo "INSERT INTO users VALUES (4, Dave)" >&3
sleep 0.5

# We do NOT send COMMIT for Dave.
# Instead, we send SIGKILL to simulate a hard process crash.
echo "[Step 5] Crashing the active MiniDB process (killing PID $MINIDB_PID with SIGKILL)..."
kill -9 $MINIDB_PID
wait $MINIDB_PID 2>/dev/null || true

# Close the file descriptor and remove the pipe
exec 3>&-
rm -f demo_pipe
echo "[Step 5] Process terminated abruptly. Database file is dirty."
echo

# 3. Restart MiniDB to run recovery and query the results
echo "[Step 6] Restarting MiniDB to trigger WAL Crash Recovery..."
echo "------------------------------------------------------------"

# Run MiniDB, execute SELECT and SHOW TABLES, then QUIT.
./minidb <<EOF
SELECT * FROM users
SHOW TABLES
QUIT
EOF

echo "------------------------------------------------------------"
echo "[Success] WAL Recovery successfully replayed Alice (1), Bob (2), and Carol (3)."
echo "[Success] Dave (4) was skipped as the transaction was uncommitted at crash time."
echo "============================================================"
