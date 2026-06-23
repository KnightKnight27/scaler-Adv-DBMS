#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DB_DIR="$(mktemp -d /tmp/minidb_recovery_demo.XXXXXX)"
"${ROOT}/build/minidb_recovery_demo" "${DB_DIR}"
