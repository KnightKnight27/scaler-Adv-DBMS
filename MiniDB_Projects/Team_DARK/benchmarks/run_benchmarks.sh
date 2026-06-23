#!/usr/bin/env bash
# Run miniDB benchmarks multiple times and print median results.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
RUNS="${1:-5}"
BENCH_BIN="${BUILD_DIR}/benchmark"

if [[ ! -x "${BENCH_BIN}" ]]; then
  echo "Building Release benchmark binary..."
  cmake -S "${ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
  cmake --build "${BUILD_DIR}" --target benchmark -j
fi

TMPDIR="$(mktemp -d)"
trap 'rm -rf "${TMPDIR}"' EXIT

echo "Running benchmarks ${RUNS} times (median reported)..."

for ((i = 1; i <= RUNS; ++i)); do
  "${BENCH_BIN}" > "${TMPDIR}/run_${i}.txt"
done

python3 - "${TMPDIR}" "${RUNS}" <<'PY'
import glob
import os
import statistics
import sys

tmpdir = sys.argv[1]
runs = int(sys.argv[2])
files = sorted(glob.glob(os.path.join(tmpdir, "run_*.txt")))
metrics = {}

for path in files:
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line or "=" not in line:
                continue
            key, value = line.split("=", 1)
            if key in ("BENCHMARK_BUILD",):
                continue
            try:
                metrics.setdefault(key, []).append(float(value))
            except ValueError:
                pass

print("# Benchmark Summary (median of {} runs)".format(runs))
print()
for key in sorted(metrics):
    values = metrics[key]
    if len(values) != runs:
        continue
    if key.endswith("_TIMESTAMP"):
        med = int(statistics.median(values))
        print(f"{key}={med}")
    elif key.endswith("_ROWS") or key.endswith("_KEYS") or key.endswith("_LOOKUPS") or key.endswith("_READERS") or key.endswith("_READS_PER_THREAD") or key.endswith("_TOTAL_READS") or key.endswith("_BUILD"):
        print(f"{key}={int(statistics.median(values))}")
    else:
        med = statistics.median(values)
        print(f"{key}={med:.3f}")
PY
