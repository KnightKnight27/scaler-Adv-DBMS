#!/usr/bin/env bash
# demo_crash_recovery.sh – demonstrates WAL-based crash recovery
set -e
BUILD_DIR="$(cd "$(dirname "$0")/../build" && pwd)"
MINIDB="$BUILD_DIR/minidb"

echo "======= MiniDB Crash-Recovery Demo ======="
echo ""
echo "Step 1: Insert some rows and commit."
printf 'INSERT 1 100\nINSERT 2 200\nINSERT 3 300\nSHOW\nQUIT\n' | "$MINIDB"

echo ""
echo "Step 2: Simulate a crash (DELETE the .db file, keep .wal)."
rm -f minidb.db
echo "  minidb.db removed. WAL intact."

echo ""
echo "Step 3: Restart – recovery replays committed TXNs from WAL."
printf 'RECOVER\nSHOW\nQUIT\n' | "$MINIDB"

echo ""
echo "Crash-Recovery demo complete. Rows should match Step 1."
