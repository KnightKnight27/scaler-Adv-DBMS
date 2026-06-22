#!/usr/bin/env bash
# Build everything and run the full test suite without CMake.
# Usage: ./run_tests.sh        (from the Team Avengers/ directory)
set -e
CXX="${CXX:-clang++}"
FLAGS="-std=c++17 -Wall -Wextra -O2 -Isrc"
OUT=".build"
mkdir -p "$OUT"

echo "== building minidb shell + benchmark =="
$CXX $FLAGS src/main.cpp            -o "$OUT/minidb"
$CXX $FLAGS benchmarks/mvcc_vs_2pl.cpp -o "$OUT/bench_mvcc"

echo "== building + running unit tests =="
for t in storage/test_storage index/test_bplus sql/test_parser \
         catalog/test_catalog execution/test_exec optimizer/test_optimizer \
         txn/test_txn recovery/test_recovery; do
    name="$(basename "$t")"
    $CXX $FLAGS "src/$t.cpp" -o "$OUT/$name"
    "$OUT/$name"
done

echo "== ALL TESTS PASSED =="
