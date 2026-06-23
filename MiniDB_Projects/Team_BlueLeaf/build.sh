#!/usr/bin/env bash
# Build MiniDB. Prefers cmake if available, otherwise falls back to the Makefile.
set -e
cd "$(dirname "$0")"

if command -v cmake >/dev/null 2>&1; then
  cmake -S . -B build-cmake
  cmake --build build-cmake -j
  cp build-cmake/minidb ./minidb
  echo "built ./minidb (cmake)"
else
  make -j
  echo "built ./minidb (make)"
fi
