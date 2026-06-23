#!/usr/bin/env bash
# run_benchmarks.sh – build and run all MiniDB benchmarks
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"

echo "=== Building MiniDB benchmarks ==="
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null
make -j"$(nproc)" bench_insert bench_select

echo ""
echo "=== bench_insert ==="
./bench_insert

echo ""
echo "=== bench_select ==="
./bench_select

echo ""
echo "All benchmarks complete."
