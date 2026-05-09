#!/usr/bin/env bash
set -euo pipefail

DB_FILE="${1:-sample.db}"

command -v sqlite3 >/dev/null 2>&1 || {
  echo "sqlite3 is not installed or not on PATH" >&2
  exit 1
}

if [[ ! -f "${DB_FILE}" ]]; then
  echo "${DB_FILE} not found. Run ./setup_sqlite.sh first." >&2
  exit 1
fi

echo "Timing SQLite query without mmap"
/usr/bin/time -p sqlite3 "${DB_FILE}" "PRAGMA mmap_size=0; SELECT * FROM users;" > /tmp/sqlite_no_mmap.out

echo
echo "Timing SQLite query with mmap"
/usr/bin/time -p sqlite3 "${DB_FILE}" "PRAGMA mmap_size=268435456; SELECT * FROM users;" > /tmp/sqlite_with_mmap.out

echo
echo "Query output files:"
ls -lh /tmp/sqlite_no_mmap.out /tmp/sqlite_with_mmap.out
