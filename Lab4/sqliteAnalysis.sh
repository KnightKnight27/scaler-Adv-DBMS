#!/bin/bash
# lab4_sqlite_analysis.sh
# Analyzes the internal structure of a SQLite3 database file using xxd

DB="lab4.db"

echo "============================================"
echo "  Lab 4: SQLite3 Internal Structure Analysis"
echo "============================================"

# ── 1. DATABASE CREATION ─────────────────────────────────
echo ""
echo ">>> TASK 1: Database Creation"
echo ""

rm -f $DB

sqlite3 $DB <<EOF
CREATE TABLE students (
    id      INTEGER PRIMARY KEY,
    name    TEXT    NOT NULL,
    age     INTEGER,
    course  TEXT
);

INSERT INTO students VALUES (1, 'Alice',   20, 'DBMS');
INSERT INTO students VALUES (2, 'Bob',     22, 'OS');
INSERT INTO students VALUES (3, 'Charlie', 21, 'Networks');
INSERT INTO students VALUES (4, 'Diana',   23, 'DBMS');
INSERT INTO students VALUES (5, 'Eve',     20, 'Compilers');

SELECT * FROM students;
EOF

echo ""
echo "Database file created: $DB"
echo "File size: $(wc -c < $DB) bytes"

# ── 2. DATABASE METADATA ─────────────────────────────────
echo ""
echo ">>> TASK 2: Database Metadata"
echo ""

sqlite3 $DB <<EOF
PRAGMA page_size;
PRAGMA page_count;
PRAGMA database_list;
PRAGMA table_info(students);

-- root page of the students table
SELECT name, rootpage, sql FROM sqlite_master WHERE type='table';
EOF

# ── 3. SQLITE FILE HEADER (first 100 bytes) ──────────────
echo ""
echo ">>> TASK 3: SQLite File Header (first 100 bytes)"
echo ""

echo "-- Raw hex dump of header --"
xxd $DB | head -7

echo ""
echo "-- Header field breakdown --"
printf "  Bytes 00-15 : File signature  -> "
xxd -l 16 -p $DB | tr -d '\n' | sed 's/../& /g'
echo ""

printf "  Bytes 16-17 : Page size       -> "
hexval=$(xxd -s 16 -l 2 -p $DB)
echo "0x$hexval = $((16#$hexval)) bytes"

printf "  Bytes 18    : Write version   -> "
xxd -s 18 -l 1 -p $DB
printf "  Bytes 19    : Read version    -> "
xxd -s 19 -l 1 -p $DB

printf "  Bytes 28-31 : File change counter -> "
xxd -s 28 -l 4 -p $DB

printf "  Bytes 32-35 : Page count      -> "
hexval=$(xxd -s 28 -l 4 -p $DB)
echo "0x$hexval = $((16#$hexval)) pages"

# ── 4. B-TREE PAGE ANALYSIS (page 1) ─────────────────────
echo ""
echo ">>> TASK 4: B-Tree Page Analysis"
echo ""

echo "-- Page header starts at offset 100 (after file header) --"
xxd -s 100 -l 12 $DB

echo ""
printf "  Byte 100    : Page type       -> "
pagetype=$(xxd -s 100 -l 1 -p $DB)
echo -n "0x$pagetype "
case $pagetype in
    "02") echo "(interior index b-tree page)" ;;
    "05") echo "(interior table b-tree page)" ;;
    "0a") echo "(leaf index b-tree page)" ;;
    "0d") echo "(leaf table b-tree page)" ;;
    *)    echo "(unknown)" ;;
esac

printf "  Bytes 101-102: First freeblock -> "
xxd -s 101 -l 2 -p $DB
printf "  Bytes 103-104: Cell count      -> "
hexval=$(xxd -s 103 -l 2 -p $DB)
echo "0x$hexval = $((16#$hexval)) cells"

printf "  Bytes 105-106: Cell content area offset -> "
xxd -s 105 -l 2 -p $DB

# ── 5. CELL POINTER ARRAY ────────────────────────────────
echo ""
echo ">>> TASK 5: Cell Pointer Array"
echo ""

cellcount=$(xxd -s 103 -l 2 -p $DB)
cellcount=$((16#$cellcount))
echo "  Number of cells: $cellcount"
echo "  Cell pointers start at offset 108 (after 8-byte page header)"
echo ""

for ((i=0; i<cellcount; i++)); do
    offset=$((108 + i * 2))
    hexval=$(xxd -s $offset -l 2 -p $DB)
    echo "  Cell $((i+1)) pointer at offset $offset -> 0x$hexval = $((16#$hexval))"
done

# ── 6. RECORD STORAGE ANALYSIS ───────────────────────────
echo ""
echo ">>> TASK 6: Record Storage (last 256 bytes of file)"
echo ""

echo "-- Hex dump of record area --"
filesize=$(wc -c < $DB)
recordstart=$((filesize - 256))
xxd -s $recordstart $DB

echo ""
echo "-- Readable strings found in record area --"
strings $DB | grep -v "^.\{1\}$"   # filter out single-char noise

# ── 7. SCHEMA STORAGE ────────────────────────────────────
echo ""
echo ">>> TASK 7: Schema Storage"
echo ""

echo "-- sqlite_master contents --"
sqlite3 $DB "SELECT type, name, tbl_name, rootpage, sql FROM sqlite_master;"

echo ""
echo "-- Schema stored as raw text in file --"
echo "  Searching for CREATE TABLE in raw bytes:"
strings $DB | grep "CREATE TABLE"

# ── 8. PHYSICAL FILE LAYOUT ──────────────────────────────
echo ""
echo ">>> TASK 8: Physical File Layout"
echo ""

filesize=$(wc -c < $DB)
pagesize=$(xxd -s 16 -l 2 -p $DB)
pagesize=$((16#$pagesize))
pagecount=$((filesize / pagesize))

echo "  File size    : $filesize bytes"
echo "  Page size    : $pagesize bytes"
echo "  Total pages  : $pagecount"
echo ""
echo "  Layout:"
echo "  [Page 1: offset 0]"
echo "    Bytes 0-99   -> SQLite file header (100 bytes)"
echo "    Bytes 100+   -> B-tree page header + cell pointer array + records"
if [ $pagecount -gt 1 ]; then
    for ((p=2; p<=pagecount; p++)); do
        offset=$(( (p-1) * pagesize ))
        echo "  [Page $p: offset $offset]"
        echo "    -> additional data pages"
    done
fi

echo ""
echo "============================================"
echo "  Analysis Complete"
echo "============================================"
echo ""
echo "Key Takeaways:"
echo "  - SQLite file starts with a 16-byte magic string 'SQLite format 3'"
echo "  - Page 1 holds both the file header and the root b-tree page"
echo "  - Each page has an 8-byte header describing its type and cell count"
echo "  - Cell pointers are 2-byte offsets pointing to records within the page"
echo "  - Schema (CREATE TABLE ...) is stored as plain text inside the file"
echo "  - All data lives in a single .db file — no separate catalog files"