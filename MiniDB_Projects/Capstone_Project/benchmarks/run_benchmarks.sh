#!/usr/bin/env bash
# run_benchmarks.sh — Run performance benchmarks for MiniDB.

set -euo pipefail

# Ensure execution is relative to root if build directory exists
if [ -d "build" ]; then
    cd build
fi

echo "============================================================"
echo "          MINIDB PERFORMANCE BENCHMARKS"
echo "============================================================"
echo

echo "1. Running Insert Latency Benchmark (10,000 inserts)..."
echo "------------------------------------------------------------"
# Run bench_insert and filter out verbose transaction log prints
./bench_insert | grep -v "TX" || true
echo "------------------------------------------------------------"
echo

echo "2. Running Select (Scan vs Index) Benchmark..."
echo "------------------------------------------------------------"
# Run bench_select and filter out verbose loading transaction log prints
./bench_select | grep -v "TX" || true
echo "------------------------------------------------------------"
echo
echo "All benchmarks completed successfully."
echo "============================================================"
