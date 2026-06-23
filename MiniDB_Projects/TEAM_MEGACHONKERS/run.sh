#!/usr/bin/env bash
#
# run.sh -- run a built MiniDB executable.
#
#   ./run.sh                 Start the interactive minidb REPL (default).
#   ./run.sh lsm_benchmark   Run the performance benchmark.
#   ./run.sh minidb < q.sql  Pipe SQL into the REPL.
#
# Any trailing arguments are forwarded to the chosen executable. Build first
# with ./build.sh.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) BUILD_DIR="build-win" ;;
  *)                    BUILD_DIR="build" ;;
esac

# Make runtime DLLs discoverable on Windows/MSYS2.
GXX_DIR="$(dirname "$(command -v g++ 2>/dev/null || echo /usr/bin/g++)")"
export PATH="$GXX_DIR:$PATH"

# First positional arg names the target (default: minidb); the rest pass through.
TARGET="${1:-minidb}"
[ "$#" -gt 0 ] && shift

BIN=""
for cand in "$BUILD_DIR/$TARGET" "$BUILD_DIR/$TARGET.exe"; do
  if [ -x "$cand" ]; then BIN="$cand"; break; fi
done

if [ -z "$BIN" ]; then
  echo "Executable '$TARGET' not found in $BUILD_DIR/." >&2
  echo "Build it first:  ./build.sh" >&2
  exit 1
fi

echo ">> Running $BIN"
exec "$BIN" "$@"
