#!/usr/bin/env bash
# Simple one-shot build for systems without `make`.
# Produces ./minidb (REPL/demo) and ./minidb_bench (benchmarks).
set -e
CXX="${CXX:-clang++}"
FLAGS="-std=c++17 -O2 -Wall -Wextra -pthread -I."

echo "Building minidb..."
$CXX $FLAGS -o minidb main.cpp

echo "Building minidb_bench..."
$CXX $FLAGS -o minidb_bench benchmarks/benchmark.cpp

echo "Done. Try:  ./minidb demo   |   ./minidb_bench   |   ./minidb"
