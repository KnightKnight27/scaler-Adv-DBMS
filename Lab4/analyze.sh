#!/usr/bin/env bash
# Tasks 3–8 — slice the database file with xxd into named regions.
set -euo pipefail
cd "$(dirname "$0")"

DB=students.db
RESULTS=results
mkdir -p "$RESULTS"

if [[ ! -f "$DB" ]]; then
    echo "[analyze] $DB missing — run ./setup.sh first." >&2
    exit 1
fi

PAGE_SIZE=$(sqlite3 "$DB" "PRAGMA page_size;")
PAGE_COUNT=$(sqlite3 "$DB" "PRAGMA page_count;")
echo "[analyze] page_size=$PAGE_SIZE  page_count=$PAGE_COUNT"

# Whole-file dump for reference (Task 8).
xxd "$DB" > "$RESULTS/full_dump.hex"

# Task 3 — first 100 bytes are the SQLite file header.
xxd -l 100 "$DB" > "$RESULTS/header.hex"

# Task 4/5/7 — page 1 holds the file header (first 100 bytes) AND the
# sqlite_master B-tree (immediately after, in the same page).
xxd -l "$PAGE_SIZE" "$DB" > "$RESULTS/page1.hex"

# B-tree page header for page 1 starts at offset 100, 8 bytes for a leaf.
xxd -s 100 -l 8 "$DB" > "$RESULTS/page1_btree_header.hex"

# Task 4/5/6 — page 2 is the root of the `students` table B-tree
# (because rootpage=2 for the only user table).
if (( PAGE_COUNT >= 2 )); then
    OFFSET_P2=$PAGE_SIZE
    xxd -s "$OFFSET_P2" -l "$PAGE_SIZE" "$DB" > "$RESULTS/page2.hex"
    xxd -s "$OFFSET_P2" -l 8           "$DB" > "$RESULTS/page2_btree_header.hex"
fi

{
    echo "============================================================"
    echo " ANALYSIS SUMMARY"
    echo "============================================================"
    echo "Page size  : $PAGE_SIZE"
    echo "Page count : $PAGE_COUNT"
    echo "File size  : $(stat -c %s "$DB" 2>/dev/null || wc -c < "$DB") bytes"
    echo
    echo "Dumps written:"
    ls -1 "$RESULTS"/*.hex
} > "$RESULTS/analysis_summary.txt"

cat "$RESULTS/analysis_summary.txt"
echo "[analyze] Hex slices written to $RESULTS/."
