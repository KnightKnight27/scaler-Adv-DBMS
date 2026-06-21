#!/usr/bin/env bash
# End-to-end feature demo for WALterDB, driven non-interactively through the CLI.
# Run from the repo root after building:  bash scripts/demo.sh
set -euo pipefail

BIN="${BIN:-./build/walterdb}"
DB="$(mktemp -d)/demo"
trap 'rm -rf "$(dirname "$DB")"' EXIT

run() { echo; echo "### $1"; echo "$2" | "$BIN" "$DB"; }

echo "================ WALterDB demo ================"

run "Create tables" "
CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR, age INT);
CREATE TABLE orders (oid INT PRIMARY KEY, uid INT, amount INT);
.tables
.schema users
"

run "Insert rows" "
INSERT INTO users VALUES (1,'alice',30),(2,'bob',25),(3,'carol',40);
INSERT INTO orders VALUES (10,1,100),(11,1,250),(12,3,75);
"

run "SELECT with WHERE + projection" "
SELECT name, age FROM users WHERE age >= 30;
"

run "JOIN across tables" "
SELECT users.name, orders.amount FROM users JOIN orders ON users.id = orders.uid WHERE orders.amount > 90;
"

run "Optimizer: index scan vs sequential scan (same query shape)" "
EXPLAIN SELECT * FROM users WHERE id = 2;
EXPLAIN SELECT * FROM users WHERE name = 'bob';
"

run "Transaction COMMIT" "
BEGIN;
INSERT INTO users VALUES (4,'dave',50);
COMMIT;
SELECT id, name FROM users WHERE id = 4;
"

run "Transaction ROLLBACK (changes undone)" "
BEGIN;
INSERT INTO users VALUES (5,'eve',22);
DELETE FROM users WHERE id = 1;
SELECT id, name FROM users;
ROLLBACK;
SELECT id, name FROM users;
"

echo; echo "================ demo complete ================"
