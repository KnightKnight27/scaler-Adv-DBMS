#!/usr/bin/env bash
# Lab 4: SQLite3 Database Internal Structure Analysis Using XXD

DB="lab4_analysis.db"

divider() { printf '\n%s\n' "$(printf '─%.0s' {1..60})"; }

echo "=== Lab 4: SQLite3 Internal Structure Analysis Using XXD ==="

# ─── Task 1: Database Creation ────────────────────────────────────
divider
echo "Task 1: Database Creation"
divider

rm -f "$DB"
sqlite3 "$DB" <<'SQL'
CREATE TABLE students (
    id         INTEGER PRIMARY KEY,
    name       TEXT    NOT NULL,
    age        INTEGER,
    department TEXT
);
INSERT INTO students VALUES (1, 'Alice Johnson',  20, 'Computer Science');
INSERT INTO students VALUES (2, 'Bob Smith',      21, 'Mathematics');
INSERT INTO students VALUES (3, 'Carol White',    22, 'Physics');
INSERT INTO students VALUES (4, 'David Brown',    20, 'Computer Science');
INSERT INTO students VALUES (5, 'Eva Green',      21, 'Chemistry');
SELECT * FROM students;
SQL

echo ""
echo "File size: $(wc -c < "$DB") bytes"

# ─── Task 2: Metadata ────────────────────────────────────────────
divider
echo "Task 2: Database Metadata"
divider

sqlite3 "$DB" <<'SQL'
PRAGMA page_size;
PRAGMA page_count;
SELECT name, rootpage, sql FROM sqlite_master WHERE type='table';
SQL

# ─── Task 3: File Header Inspection ──────────────────────────────
divider
echo "Task 3: SQLite File Header (first 100 bytes)"
divider

xxd -l 100 "$DB"
echo ""
echo "Magic string at byte 0-15: $(xxd -l 16 "$DB" | head -1)"
echo "  → 'SQLite format 3\\000' identifies this as a valid SQLite3 file."

# Page size stored at offset 16-17 (big-endian):
PAGE_SIZE_HEX=$(xxd -s 16 -l 2 "$DB" | awk '{print $2$3}' | cut -c1-4)
echo "  → Page size bytes (offset 16-17): 0x${PAGE_SIZE_HEX}"

# ─── Task 4: B-Tree Page Analysis ────────────────────────────────
divider
echo "Task 4: B-Tree Page Analysis (first 256 bytes of page 1)"
divider

xxd -l 256 "$DB"
echo ""
echo "  Byte 100: page type (0x0D = leaf table B-tree page)"
PAGE_TYPE=$(xxd -s 100 -l 1 "$DB" | awk '{print $2}')
echo "  Page type byte: 0x${PAGE_TYPE}"

# ─── Task 5: Cell Pointer Array ──────────────────────────────────
divider
echo "Task 5: Cell Pointer Array (bytes 101-108 of page 1)"
divider

echo "Cell pointer array (offsets of records within the page):"
xxd -s 101 -l 10 "$DB"

# ─── Task 6: Record Payload ───────────────────────────────────────
divider
echo "Task 6: Record Storage – searching for stored text values"
divider

echo "ASCII strings found in database file:"
strings "$DB" | grep -E '[A-Za-z]{4,}'

# ─── Task 7: Schema Storage ───────────────────────────────────────
divider
echo "Task 7: Schema Storage – CREATE TABLE stored in file"
divider

echo "Hex dump of region containing schema (offset 0x200-0x400):"
xxd -s 512 -l 256 "$DB" | head -20
echo ""
echo "Schema string visible in binary:"
strings "$DB" | grep -i "CREATE"

# ─── Task 8: Physical Layout Summary ─────────────────────────────
divider
echo "Task 8: Physical File Layout Summary"
divider

PAGE_SIZE=$(sqlite3  "$DB" "PRAGMA page_size;")
PAGE_COUNT=$(sqlite3 "$DB" "PRAGMA page_count;")
echo "  Page size       : ${PAGE_SIZE} bytes"
echo "  Page count      : ${PAGE_COUNT}"
echo "  Total size      : $((PAGE_SIZE * PAGE_COUNT)) bytes"
echo "  File size       : $(wc -c < "$DB") bytes"
echo ""
echo "  Page 1 (offset 0)       : Database header + root B-tree page"
echo "  Remaining pages          : Table leaf pages with record data"
echo ""
echo "Full hex dump (first 512 bytes):"
xxd -l 512 "$DB"

rm -f "$DB"
echo ""
echo "=== Analysis complete ==="
