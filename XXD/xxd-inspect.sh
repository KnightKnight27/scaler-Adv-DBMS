#!/usr/bin/env bash
#
# xxd-inspect.sh
# ---------------
# Build a tiny SQLite database (cars.db) with 10 rows in a `cars` table,
# then dump and annotate the raw bytes so we can walk through:
#   - the 100-byte SQLite database header
#   - the page-1 B-tree page header (sqlite_schema)
#   - the cell-pointer array and the cells (schema rows)
#   - the data B-tree page that holds the actual `cars` rows
#
# Run from inside the XXD/ directory:
#   chmod +x xxd-inspect.sh
#   ./xxd-inspect.sh
#
# Output files produced next to this script:
#   cars.db              - the SQLite database itself
#   cars.full.hex        - hex dump of the entire file
#   cars.page1.hex       - hex dump of page 1 (schema page)
#   cars.page2.hex       - hex dump of page 2 (data page for `cars`)
#   cars.header.hex      - just the first 100 bytes (database header)
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

DB="cars.db"

echo "[1/5] cleaning previous run"
rm -f "$DB" cars.full.hex cars.page1.hex cars.page2.hex cars.header.hex

echo "[2/5] creating $DB with a cars(id, make, model) table"
sqlite3 "$DB" <<'SQL'
PRAGMA page_size = 4096;
CREATE TABLE cars (
    id    INTEGER PRIMARY KEY,
    make  TEXT,
    model TEXT
);
INSERT INTO cars (id, make, model) VALUES
    (1,  'Toyota',     'Corolla'),
    (2,  'Honda',      'Civic'),
    (3,  'Ford',       'Mustang'),
    (4,  'Chevrolet',  'Camaro'),
    (5,  'Tesla',      'Model 3'),
    (6,  'BMW',        'M3'),
    (7,  'Audi',       'A4'),
    (8,  'Nissan',     'Altima'),
    (9,  'Hyundai',    'Elantra'),
    (10, 'Mazda',      'CX-5');
SQL

PAGE_SIZE=$(sqlite3 "$DB" "PRAGMA page_size;")
PAGE_COUNT=$(sqlite3 "$DB" "PRAGMA page_count;")
FILE_SIZE=$(wc -c < "$DB" | tr -d ' ')

echo "    page_size  = $PAGE_SIZE bytes"
echo "    page_count = $PAGE_COUNT"
echo "    file_size  = $FILE_SIZE bytes"

echo "[3/5] writing hex dumps"
xxd "$DB"                                      > cars.full.hex
xxd -l 100 "$DB"                               > cars.header.hex
xxd -s 0          -l "$PAGE_SIZE" "$DB"        > cars.page1.hex
if [ "$PAGE_COUNT" -ge 2 ]; then
    xxd -s "$PAGE_SIZE" -l "$PAGE_SIZE" "$DB"  > cars.page2.hex
fi

echo "[4/5] annotated walk-through"
echo
echo "----- bytes 0..15  (SQLite magic string) -----"
xxd -s 0  -l 16 "$DB"
echo "  bytes 0..15  : 'SQLite format 3\\000'  (16-byte magic header)"

echo
echo "----- bytes 16..17 (page size, big-endian uint16) -----"
xxd -s 16 -l 2  "$DB"
echo "  page size in bytes = $PAGE_SIZE"

echo
echo "----- bytes 28..31 (in-header database size in pages, BE uint32) -----"
xxd -s 28 -l 4  "$DB"
echo "  page count = $PAGE_COUNT"

echo
echo "----- bytes 100..107 (page-1 B-tree header) -----"
# page-1 starts at offset 0, but its B-tree header starts AFTER the 100-byte
# database header. So the page-1 B-tree header lives at file offsets 100..111
# (12 bytes because page 1 is a table interior/leaf root: leaf=8, interior=12;
# for sqlite_schema the root is usually a leaf -> 8 bytes).
xxd -s 100 -l 12 "$DB"
echo "  byte 100      = page type"
echo "                  0x0d = leaf table b-tree"
echo "                  0x05 = interior table b-tree"
echo "                  0x0a = leaf index b-tree"
echo "                  0x02 = interior index b-tree"
echo "  bytes 101..102 = first freeblock offset (0 = none)"
echo "  bytes 103..104 = number of cells on this page"
echo "  bytes 105..106 = start of cell content area"
echo "  byte  107      = number of fragmented free bytes"

echo
echo "----- page 2 B-tree header (offset = $PAGE_SIZE) -----"
xxd -s "$PAGE_SIZE" -l 12 "$DB"
echo "  Same 8/12-byte layout as above, but at the start of page 2."
echo "  Cell-pointer array follows immediately after the header."

echo
echo "[5/5] done. See README.md for the full navigation guide."
echo "      Key files:"
echo "        - $DB"
echo "        - cars.full.hex      (entire file)"
echo "        - cars.header.hex    (first 100 bytes)"
echo "        - cars.page1.hex     (schema page)"
echo "        - cars.page2.hex     (data page for cars)"
