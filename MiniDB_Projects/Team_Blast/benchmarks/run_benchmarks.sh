#!/bin/bash
# run_benchmarks.sh — Run all performance benchmarks for MiniDB
# Team Blast

# Exit on error
set -e

# Work from the build directory
if [ -d "build" ]; then
    cd build
fi

echo "============================================================"
echo "          MINIDB PERFORMANCE BENCHMARKS"
echo "============================================================"
echo

echo "1. Running Insert Latency Benchmark (10,000 inserts)..."
echo "------------------------------------------------------------"
# We run bench_insert and use tail to filter out the per-transaction logs
./bench_insert | grep -v "TX" || true
echo "------------------------------------------------------------"
echo

echo "2. Running Select (Scan vs Index) Benchmark..."
echo "------------------------------------------------------------"
# Run bench_select and filter out the loading transaction logs
./bench_select | grep -v "TX" || true
echo "------------------------------------------------------------"
echo
echo "All benchmarks completed successfully."
echo "============================================================"
