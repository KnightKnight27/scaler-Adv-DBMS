#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DB_DIR="$(mktemp -d /tmp/minidb_lsm_demo.XXXXXX)"
"${ROOT}/build/minidb_lsm_demo" "${DB_DIR}"
