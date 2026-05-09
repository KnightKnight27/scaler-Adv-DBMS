#!/usr/bin/env bash
set -euo pipefail

echo "SQLite related processes:"
ps aux | grep '[s]qlite' || true

echo
echo "PostgreSQL related processes:"
ps aux | grep '[p]ostgres' || true
