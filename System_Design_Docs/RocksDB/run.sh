#!/usr/bin/env bash
# Compile and run the amplification benchmark against Homebrew librocksdb.
# Produces bench_results.txt (with forced full compaction) and
# bench_natural.txt (natural steady-state level distribution).
set -euo pipefail

RDB="$(brew --prefix rocksdb)"
CXX="${CXX:-c++}"

echo ">> building amp_bench (forced full compaction)"
"$CXX" -std=c++20 -O2 amp_bench.cpp -I"$RDB/include" -L"$RDB/lib" -lrocksdb -o amp_bench

echo ">> building amp_natural (no forced compaction)"
sed 's#db->CompactRange(CompactRangeOptions(), nullptr, nullptr); // settle the LSM#// (no forced compaction: observe natural LSM shape)#' \
    amp_bench.cpp > amp_natural.cpp
"$CXX" -std=c++20 -O2 amp_natural.cpp -I"$RDB/include" -L"$RDB/lib" -lrocksdb -o amp_natural

export DYLD_LIBRARY_PATH="$RDB/lib"
echo ">> running settled benchmark"; ./amp_bench   | tee bench_results.txt
echo ">> running natural benchmark"; ./amp_natural | tee bench_natural.txt

rm -f amp_bench amp_natural amp_natural.cpp
