#!/bin/bash
# demo_replication.sh — Automated Primary-Replica Replication Demo for MiniDB
# Team Blast

# Exit on error
set -e

# Work from the build directory where the binaries are located
if [ -d "build" ]; then
    cd build
fi

echo "============================================================"
echo "    MINIDB PRIMARY-REPLICA REPLICATION DEMO"
echo "============================================================"
echo

# 1. Clean up old databases, WALs, and replication logs
echo "[Setup] Cleaning up old database files..."
rm -f minidb.db minidb.wal minidb_replica.db minidb_replica.wal minidb_replication.log
rm -f pri_pipe rep_pipe
echo "[Setup] Clean complete."
echo

# 2. Create pipes for interacting with background REPL processes
mkfifo pri_pipe
mkfifo rep_pipe

# 3. Start Replica in background, redirecting stdout to a log file
echo "[Setup] Starting Replica in background (read-only mode)..."
./minidb --replica < rep_pipe > replica_output.log 2>&1 &
REPLICA_PID=$!

# 4. Start Primary in background, redirecting stdout to another log file
echo "[Setup] Starting Primary in background (read-write mode)..."
./minidb < pri_pipe > primary_output.log 2>&1 &
PRIMARY_PID=$!

# Open file descriptors to keep pipes open
exec 3>pri_pipe
exec 4>rep_pipe

# Wait for startup
sleep 1.5

# 5. Send CREATE and INSERT commands to Primary
echo "[Step 1] Creating table and inserting data on the Primary..."
echo "CREATE TABLE products" >&3
sleep 0.5
echo "INSERT INTO products VALUES (1, Laptop)" >&3
sleep 0.5
echo "INSERT INTO products VALUES (2, Phone)" >&3
sleep 0.5

# 6. Send Transactional INSERT commands to Primary
echo "[Step 2] Sending a transaction on the Primary..."
echo "BEGIN" >&3
sleep 0.5
echo "INSERT INTO products VALUES (3, Tablet)" >&3
sleep 0.5
echo "COMMIT" >&3
sleep 1.0  # Give replication polling loop time to catch up

# 7. Query the Replica to verify records have replicated
echo "[Step 3] Querying the Replica via SELECT *..."
echo "SELECT * FROM products" >&4
sleep 1.0

# Print replica logs
echo "------------------------------------------------------------"
echo "Replica Output Log:"
cat replica_output.log | grep -E "Replica|Results|row\(s\)|key="|grep -v "Type HELP" || true
echo "------------------------------------------------------------"

# 8. Attempt an illegal write command on the replica
echo "[Step 4] Attempting an illegal INSERT directly on the Replica..."
echo "INSERT INTO products VALUES (4, Monitor)" >&4
sleep 0.5

# Print replica logs showing read-only restriction error
echo "------------------------------------------------------------"
echo "Replica Output Log (after illegal write attempt):"
cat replica_output.log | tail -n 5
echo "------------------------------------------------------------"

# 9. Clean up processes and pipes
echo "[Cleanup] Stopping primary and replica instances..."
echo "QUIT" >&3
echo "QUIT" >&4
sleep 0.5

# Close file descriptors
exec 3>&-
exec 4>&-

kill $PRIMARY_PID $REPLICA_PID 2>/dev/null || true
rm -f pri_pipe rep_pipe
echo "[Success] Replication successfully verified!"
echo "============================================================"
