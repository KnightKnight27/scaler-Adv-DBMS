#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DB_DIR="$(mktemp -d /tmp/minidb_storage_demo.XXXXXX)"

echo "Storage demo: INSERT should show page allocation, buffer usage, WAL, and disk write"
echo "Database path: ${DB_DIR}"
printf 'INSERT users 1 Sushant\nEXIT\n' | "${ROOT}/build/minidb_cli" "${DB_DIR}"
