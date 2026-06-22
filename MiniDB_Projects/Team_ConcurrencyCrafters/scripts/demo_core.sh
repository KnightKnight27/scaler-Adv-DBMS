#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
PYTHONPATH=src python -m minidb.cli \
  --data-dir data/demo_core \
  --sql "CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT);" \
  --sql "INSERT INTO users VALUES (1, 'Ada', 31);" \
  --sql "INSERT INTO users VALUES (2, 'Bob', 28);" \
  --sql "EXPLAIN SELECT * FROM users WHERE id = 2;" \
  --sql "SELECT * FROM users WHERE id = 2;"

