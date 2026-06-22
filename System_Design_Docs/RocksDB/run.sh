#!/usr/bin/env bash
# Build amp_bench and run it under leveled then universal compaction.
# Captures stdout into bench_results.txt; raw RocksDB LOG lines into bench_natural.txt.
set -euo pipefail

cd "$(dirname "$0")"

BREW_PREFIX="$(brew --prefix rocksdb 2>/dev/null || true)"
CXXFLAGS="-std=c++17 -O2"
LIBS="-lrocksdb"

if [[ -n "$BREW_PREFIX" ]]; then
    CXXFLAGS="$CXXFLAGS -I$BREW_PREFIX/include"
    LIBS="-L$BREW_PREFIX/lib $LIBS"
fi

echo "==> building amp_bench"
c++ $CXXFLAGS amp_bench.cpp $LIBS -o amp_bench

KEYS="${KEYS:-1000000}"
READS="${READS:-100000}"
OUT=bench_results.txt
NATURAL=bench_natural.txt
> "$OUT"
> "$NATURAL"

for policy in leveled universal; do
    echo "==> running policy=$policy"
    DB_PATH="/tmp/amp_bench_${policy}"
    rm -rf "$DB_PATH"
    {
        echo "============================================================"
        echo " policy=$policy   keys=$KEYS   reads=$READS"
        echo "============================================================"
        ./amp_bench --policy "$policy" --keys "$KEYS" --reads "$READS" --path "$DB_PATH"
        echo
    } | tee -a "$OUT"
    # also grab RocksDB's own LOG file (compaction events, per-level moves)
    if [[ -f "$DB_PATH/LOG" ]]; then
        {
            echo "--- LOG: $policy ---"
            grep -E "EVENT_LOG_v1|compaction|flush_finished" "$DB_PATH/LOG" \
              | tail -40
            echo
        } >> "$NATURAL"
    fi
done

echo "==> wrote $OUT and $NATURAL"
