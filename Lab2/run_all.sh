#!/usr/bin/env bash
# Runs both experiments and saves output under Lab2/results/.
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$DIR/results"

echo ">>> Running SQLite experiment"
bash "$DIR/sqlite_experiment.sh" 2>&1 | tee "$DIR/results/sqlite_output.txt"

echo
echo ">>> Running PostgreSQL experiment"
bash "$DIR/postgres_experiment.sh" 2>&1 | tee "$DIR/results/postgres_output.txt"

echo
echo "Results saved to $DIR/results/"
