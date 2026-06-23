#!/usr/bin/env bash
# Build MiniDB with the MSYS2 UCRT64 toolchain (GCC 16 / CMake / Ninja).
# Usage: ./build.sh [test]
set -euo pipefail

UCRT="/c/msys64/ucrt64/bin"
export PATH="$UCRT:$PATH"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"

cmake -S "$ROOT" -B "$BUILD" -G Ninja >/dev/null
cmake --build "$BUILD"

if [[ "${1:-}" == "test" ]]; then
  ctest --test-dir "$BUILD" --output-on-failure
fi
