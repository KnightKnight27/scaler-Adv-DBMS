#!/usr/bin/env bash
#
# build.sh -- configure & build MiniDB (TEAM_MEGACHONKERS).
#
#   ./build.sh            Configure (if needed) and build all targets.
#   ./build.sh --test     Build, then run the full ctest suite.
#   ./build.sh --clean     Remove the build directory first, then build.
#   ./build.sh --clean -t  Clean rebuild + tests.
#
# Works on Linux/WSL (Unix Makefiles, build/) and natively on Windows via
# MSYS2/MinGW (MinGW Makefiles, build-win/). On Windows it prepends the
# compiler's bin directory to PATH so CMake's gtest_discover_tests step -- which
# launches the freshly built test executables -- can resolve libstdc++/libgcc/
# libwinpthread.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# --- options ---------------------------------------------------------------
RUN_TESTS=0
CLEAN=0
for arg in "$@"; do
  case "$arg" in
    --test|-t)  RUN_TESTS=1 ;;
    --clean|-c) CLEAN=1 ;;
    --help|-h)
      echo "Usage: ./build.sh [--clean|-c] [--test|-t]"
      exit 0 ;;
    *)
      echo "Unknown option: $arg" >&2
      echo "Usage: ./build.sh [--clean|-c] [--test|-t]" >&2
      exit 1 ;;
  esac
done

# --- platform detection ----------------------------------------------------
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    BUILD_DIR="build-win"
    GENERATOR="MinGW Makefiles" ;;
  *)
    BUILD_DIR="build"
    GENERATOR="" ;;   # CMake default (usually Unix Makefiles)
esac

# Make the compiler's runtime DLLs discoverable for the test-discovery step.
GXX_DIR="$(dirname "$(command -v g++ 2>/dev/null || echo /usr/bin/g++)")"
export PATH="$GXX_DIR:$PATH"

# Reuse an already-fetched googletest checkout when present (offline-friendly).
GTEST_SRC="$SCRIPT_DIR/build/_deps/googletest-src"

if [ "$CLEAN" -eq 1 ]; then
  echo ">> Cleaning $BUILD_DIR/"
  rm -rf "$BUILD_DIR"
fi

# --- configure -------------------------------------------------------------
CMAKE_ARGS=(-S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug)
[ -n "$GENERATOR" ] && CMAKE_ARGS+=(-G "$GENERATOR")
[ -d "$GTEST_SRC" ] && CMAKE_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$GTEST_SRC")

echo ">> Configuring ${GENERATOR:-default} -> $BUILD_DIR/"
cmake "${CMAKE_ARGS[@]}"

# --- build -----------------------------------------------------------------
JOBS="$(nproc 2>/dev/null || echo 4)"
echo ">> Building with $JOBS job(s)"
cmake --build "$BUILD_DIR" -j"$JOBS"

# --- optional tests --------------------------------------------------------
if [ "$RUN_TESTS" -eq 1 ]; then
  echo ">> Running tests"
  ( cd "$BUILD_DIR" && ctest --output-on-failure )
fi

echo ">> Done. Binaries are in $BUILD_DIR/ (run ./run.sh to start the engine)."
