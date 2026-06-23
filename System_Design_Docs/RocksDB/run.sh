#!/usr/bin/env bash
#
# Build and run the small RocksDB benchmark used in this folder's README.
#
# Requirements:
#   - macOS with Homebrew, or any Linux with rocksdb headers + libs installed
#   - g++ supporting -std=c++17
#
# Tested on:
#   - macOS, RocksDB 11.1.1 installed via `brew install rocksdb`
#
# Usage:
#   ./run.sh                              # runs the full demo at /tmp/rocks_bench/db
#   ./run.sh fillrandom 200000            # custom mode and key count
#   ./run.sh fillrandom 500000 universal  # universal compaction style

set -euo pipefail

ROCKSDB_PREFIX="${ROCKSDB_PREFIX:-/opt/homebrew/opt/rocksdb}"
WORKDIR="${WORKDIR:-/tmp/rocks_bench}"
DBPATH="${DBPATH:-$WORKDIR/db}"
MODE="${1:-all}"
N="${2:-200000}"
STYLE="${3:-leveled}"

mkdir -p "$WORKDIR"
cp "$(dirname "$0")/bench.cpp" "$WORKDIR/bench.cpp"

g++ -std=c++17 -O2 \
    -I "$ROCKSDB_PREFIX/include" \
    -L "$ROCKSDB_PREFIX/lib" \
    -lrocksdb -lpthread \
    "$WORKDIR/bench.cpp" -o "$WORKDIR/bench"

rm -rf "$DBPATH"

DYLD_LIBRARY_PATH="$ROCKSDB_PREFIX/lib" \
LD_LIBRARY_PATH="$ROCKSDB_PREFIX/lib" \
    "$WORKDIR/bench" "$DBPATH" "$MODE" "$N" "$STYLE"
