#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
./setup.sh
./analyze.sh
echo "[run_all] done. See results/."
