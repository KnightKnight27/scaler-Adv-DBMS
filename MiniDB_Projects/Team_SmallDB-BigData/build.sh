#!/bin/sh
# Build MiniDB and the Track B benchmark.
# Needs a C++17 compiler (g++ or clang++) with pthreads. Override with CXX=...
set -e

CXX="${CXX:-g++}"
FLAGS="-std=c++17 -O2 -Wall -Wextra -pthread -Isrc"

echo "building minidb..."
$CXX $FLAGS $(find src -name '*.cpp') -o minidb

echo "building benchmark..."
$CXX $FLAGS benchmarks/bench_mvcc_vs_2pl.cpp src/txn/transaction_manager.cpp -o benchmarks/bench

echo "done."
echo "  run the SQL engine : ./minidb data"
echo "  run the benchmark  : ./benchmarks/bench"
