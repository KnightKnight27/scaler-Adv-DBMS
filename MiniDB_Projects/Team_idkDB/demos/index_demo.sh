#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DB_DIR="$(mktemp -d /tmp/minidb_index_demo.XXXXXX)"
COMMANDS="$(mktemp /tmp/minidb_index_commands.XXXXXX)"

echo "Index demo: SELECT should show optimizer choosing IndexScan and [BTREE] lookup"
echo "Database path: ${DB_DIR}"
for i in $(seq 1 80); do
  printf 'INSERT users %s value_%s_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n' "$i" "$i" >> "${COMMANDS}"
done
printf 'SELECT users WHERE id=50\nEXIT\n' >> "${COMMANDS}"
"${ROOT}/build/minidb_cli" "${DB_DIR}" < "${COMMANDS}" | grep -E 'SELECT|found id=50|OPTIMIZER|BTREE|BUFFER|DISK|WAL|LOCK|index lookup|index scan' | tail -n 40
