#!/usr/bin/env bash
# Lab 4: SQLite3 Database Internal Structure Analysis
# Script 2: Inspect database internals using xxd hex dump
# Student: Lokendra Singh Rajawat (23bcs10075)
#
# Usage: bash inspect.sh
# Prerequisite: run create_db.sh first

set -e

DB="lab4_students.db"

if [ ! -f "$DB" ]; then
    echo "ERROR: $DB not found. Run 'bash create_db.sh' first."
    exit 1
fi

PAGE_SIZE=$(sqlite3 "$DB" "PRAGMA page_size;")

echo "============================================================"
echo " Lab 4 — SQLite3 Internal Structure: Hex Inspection"
echo "============================================================"
echo "Database : $DB"
echo "Page size: $PAGE_SIZE bytes"
echo ""

# ----------------------------------------------------------------
# TASK 3: SQLite File Header (first 100 bytes)
# ----------------------------------------------------------------
echo "------------------------------------------------------------"
echo " TASK 3: SQLite File Header (first 100 bytes)"
echo "------------------------------------------------------------"
echo "The first 100 bytes contain the SQLite file header."
echo "Magic string at offset 0: 'SQLite format 3\\000' (16 bytes)"
echo ""
xxd "$DB" | head -7
echo ""
echo "Annotations:"
echo "  Offset 0x00–0x0F : Magic string 'SQLite format 3\\000'"
echo "  Offset 0x10–0x11 : Page size in bytes (big-endian)"
echo "  Offset 0x12       : File format write version"
echo "  Offset 0x13       : File format read version"
echo "  Offset 0x14       : Reserved space per page"
echo "  Offset 0x1B       : Maximum embedded payload fraction (64)"
echo "  Offset 0x1C       : Minimum embedded payload fraction (32)"
echo "  Offset 0x1D       : Leaf payload fraction (32)"
echo "  Offset 0x1C–0x1F : File change counter"
echo "  Offset 0x20–0x23 : Number of pages in database"
echo ""

# ----------------------------------------------------------------
# TASK 3 cont: Verify magic string
# ----------------------------------------------------------------
echo "------------------------------------------------------------"
echo " Verifying SQLite Magic Signature"
echo "------------------------------------------------------------"
MAGIC=$(xxd -l 16 "$DB" | head -1)
echo "$MAGIC"
echo ""
echo "Expected: 5351 4c69 7465 2066 6f72 6d61 7420 3300"
echo "          (S Q L i t e   f o r m a t   3 NUL)"
echo ""

# ----------------------------------------------------------------
# TASK 4: B-Tree Page Header (bytes 100–107 of page 1)
# ----------------------------------------------------------------
echo "------------------------------------------------------------"
echo " TASK 4: B-Tree Page Header (bytes 100-116)"
echo "------------------------------------------------------------"
echo "Immediately after the file header (100 bytes) is the page 1"
echo "B-tree page header. This is an interior or leaf table B-tree."
echo ""
xxd -s 100 -l 12 "$DB"
echo ""
echo "Annotations:"
echo "  Offset 0x64 (100): Page type byte"
echo "    0x0D = Leaf table B-tree page"
echo "    0x05 = Interior table B-tree page"
echo "  Offset 0x65-0x66 : First freeblock offset"
echo "  Offset 0x67-0x68 : Number of cells on this page"
echo "  Offset 0x69-0x6A : Start of cell content area"
echo "  Offset 0x6B      : Fragmented free bytes"
echo ""

# ----------------------------------------------------------------
# TASK 5: Cell Pointer Array
# ----------------------------------------------------------------
echo "------------------------------------------------------------"
echo " TASK 5: Cell Pointer Array (bytes 108 onward)"
echo "------------------------------------------------------------"
echo "After the page header, cell pointers (2 bytes each) point"
echo "to the start of each record within the page."
echo ""
xxd -s 108 -l 24 "$DB"
echo ""

# ----------------------------------------------------------------
# TASK 6: Record Payload — search for stored text values
# ----------------------------------------------------------------
echo "------------------------------------------------------------"
echo " TASK 6: Record Storage — Searching for stored names"
echo "------------------------------------------------------------"
echo "Searching for student name 'Lokendra' in the raw DB file:"
echo ""
strings "$DB" | grep -i "Lokendra" || echo "(not found as plain string — check hex)"
echo ""
echo "Searching for roll number '23bcs10075':"
strings "$DB" | grep "23bcs10075" || echo "(not found as plain string)"
echo ""
echo "Raw hex around first record payload:"
xxd "$DB" | grep -A 2 "Loke" | head -10
echo ""

# ----------------------------------------------------------------
# TASK 7: Schema Storage — sqlite_master contents in hex
# ----------------------------------------------------------------
echo "------------------------------------------------------------"
echo " TASK 7: Schema Storage (sqlite_master)"
echo "------------------------------------------------------------"
echo "SQLite stores the CREATE TABLE SQL in sqlite_master on page 1."
echo "Searching for 'CREATE' in the raw hex dump:"
echo ""
xxd "$DB" | grep -i "4352 4541" | head -5
echo ""
echo "Searching for table name 'students' as ASCII:"
xxd "$DB" | grep -i "7374 7564" | head -5
echo ""

# ----------------------------------------------------------------
# TASK 8: Physical File Layout Summary
# ----------------------------------------------------------------
echo "------------------------------------------------------------"
echo " TASK 8: Physical File Layout Summary"
echo "------------------------------------------------------------"
TOTAL_PAGES=$(sqlite3 "$DB" "PRAGMA page_count;")
FILE_SIZE=$(ls -l "$DB" | awk '{print $5}')

echo "Total pages       : $TOTAL_PAGES"
echo "Page size         : $PAGE_SIZE bytes"
echo "File size on disk : $FILE_SIZE bytes"
echo "Calculated size   : $((TOTAL_PAGES * PAGE_SIZE)) bytes"
echo ""
echo "Page Layout:"
echo "  Page 1 (offset 0x0000) : File header (100B) + sqlite_master B-tree root"
echo "  Page 2+ (if exists)    : Table data pages for 'students' table"
echo ""
echo "First $((PAGE_SIZE / 16)) lines cover page 1 (showing first 256 bytes):"
xxd -l 256 "$DB"

echo ""
echo "============================================================"
echo " Inspection complete."
echo "============================================================"
