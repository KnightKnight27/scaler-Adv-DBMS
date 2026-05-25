#!/usr/bin/env bash
# Lab 4 — Read a SQLite file with xxd
# Author: Nase Anishka (Roll No. 10075)
# Run from this directory: bash commands.sh

set -u

DB=lab4.db
DUMP=hexdump.txt
rm -f "$DB" "$DUMP"

echo "[1] Create the database and seed five book rows"
sqlite3 "$DB" <<'SQL'
CREATE TABLE books (
  id INTEGER PRIMARY KEY,
  title TEXT NOT NULL,
  author TEXT NOT NULL
);
INSERT INTO books (title, author) VALUES
 ('The Pragmatic Programmer',  'Andrew Hunt'),
 ('Designing Data-Intensive Apps', 'Martin Kleppmann'),
 ('The C Programming Language', 'Kernighan and Ritchie'),
 ('Clean Code', 'Robert C Martin'),
 ('Database Internals', 'Alex Petrov');
SQL

echo
echo "[2] Show the database file"
ls -lh "$DB"

echo
echo "[3] PRAGMA values (these are literally fields in the file header)"
sqlite3 "$DB" "PRAGMA page_size; PRAGMA page_count; PRAGMA encoding;"

echo
echo "[4] sqlite_schema row for our table"
sqlite3 "$DB" "SELECT type, name, tbl_name, rootpage FROM sqlite_schema;"

echo
echo "[5] Dump the file in hex to $DUMP"
xxd "$DB" > "$DUMP"
wc -l "$DUMP"

echo
echo "[6] First eight lines (database header)"
head -8 "$DUMP"

echo
echo "[7] Start of page 2 — the books leaf B-tree page (offset 0x1000)"
grep -E "^00001000:" "$DUMP"
grep -E "^00001010:" "$DUMP"

echo
echo "[8] Cell content area at the tail of page 2 (where the rows actually live)"
grep -E "^00001f[0-9a-f]0:" "$DUMP"

echo
echo "Done."
