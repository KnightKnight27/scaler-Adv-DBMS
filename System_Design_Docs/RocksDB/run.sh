#!/usr/bin/env bash
#
# Build and run the small RocksDB benchmark for this folder's README.
#
#   Roll Number: 24BCS10183
#   Name:        Aman Yadav
#   Class:       B (2nd Year)
#
# Requirements:
#   - macOS with Homebrew (`brew install rocksdb`), or any Linux with the
#     rocksdb headers + shared library installed.
#   - g++/clang++ supporting -std=c++17.
#
# Usage:
#   ./run.sh                                # full demo (all modes), 200k keys, leveled
#   ./run.sh fillrandom 200000              # write-only run
#   ./run.sh fillrandom 200000 universal    # universal compaction style
#   ./run.sh readrandom 200000              # read-only run on an existing db
#
# Reproduces Section 5 of README.md.

set -euo pipefail

# Homebrew installs rocksdb under a versioned opt prefix; allow override.
ROCKSDB_PREFIX="${ROCKSDB_PREFIX:-/opt/homebrew/opt/rocksdb}"
if [ ! -d "$ROCKSDB_PREFIX/include" ]; then
  # Fall back to a Linux-style location.
  ROCKSDB_PREFIX="${ROCKSDB_PREFIX_FALLBACK:-/usr/local}"
fi

WORKDIR="${WORKDIR:-/tmp/rocks_bench}"
DBPATH="${DBPATH:-$WORKDIR/db}"
MODE="${1:-all}"
N="${2:-200000}"
STYLE="${3:-leveled}"

mkdir -p "$WORKDIR"
cp "$(dirname "$0")/bench.cpp" "$WORKDIR/bench.cpp"

echo ">> building bench (rocksdb prefix: $ROCKSDB_PREFIX)"
g++ -std=c++17 -O2 \
    -I "$ROCKSDB_PREFIX/include" \
    -L "$ROCKSDB_PREFIX/lib" \
    "$WORKDIR/bench.cpp" -o "$WORKDIR/bench" \
    -lrocksdb -lpthread -ldl

# Start each run from a clean database so the numbers are reproducible.
rm -rf "$DBPATH"

echo ">> running: bench $DBPATH $MODE $N $STYLE"
DYLD_LIBRARY_PATH="$ROCKSDB_PREFIX/lib" \
LD_LIBRARY_PATH="$ROCKSDB_PREFIX/lib" \
    "$WORKDIR/bench" "$DBPATH" "$MODE" "$N" "$STYLE"
