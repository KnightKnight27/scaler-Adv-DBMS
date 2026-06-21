#!/usr/bin/env bash
# Crash-recovery demo: commit some rows, start an uncommitted transaction, then
# "crash" (exit with no clean checkpoint).  On reopen, recovery replays the WAL:
# committed data survives, uncommitted data is rolled back.
# Run from the repo root after building:  bash scripts/crash_test.sh
set -euo pipefail

BIN="${BIN:-./build/walterdb}"
DB="$(mktemp -d)/crashdb"
trap 'rm -rf "$(dirname "$DB")"' EXIT

echo "============ WALterDB crash-recovery demo ============"

echo
echo "### Session 1: commit rows {1,2,3}, then BEGIN + insert {4,5} UNCOMMITTED, then crash"
echo "
CREATE TABLE t (id INT PRIMARY KEY, v INT);
INSERT INTO t VALUES (1,10),(2,20),(3,30);
BEGIN;
INSERT INTO t VALUES (4,40),(5,50);
SELECT id, v FROM t;
.crash
" | "$BIN" "$DB"

echo
echo "### Session 2: reopen the SAME database -> recovery runs automatically"
echo "
SELECT id, v FROM t;
" | "$BIN" "$DB"

echo
echo "Expected: rows 1,2,3 present (committed survived); rows 4,5 absent (uncommitted rolled back)."
echo "============ crash-recovery demo complete ============"
