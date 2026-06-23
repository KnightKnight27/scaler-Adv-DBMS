#!/usr/bin/env bash
# demo_replication.sh – demonstrates primary-replica log shipping
set -e
BUILD_DIR="$(cd "$(dirname "$0")/../build" && pwd)"
MINIDB="$BUILD_DIR/minidb"

echo "======= MiniDB Replication Demo ======="
echo ""
echo "Primary will insert rows. Replica syncs in background."
echo ""
printf 'INSERT 10 1000\nINSERT 20 2000\nINSERT 30 3000\nSHOW\n' | "$MINIDB" &
PID=$!

sleep 2
echo ""
echo "Checking replica state:"
printf 'REPLICA\nQUIT\n' | "$MINIDB"

wait $PID || true
echo ""
echo "Replication demo complete."
