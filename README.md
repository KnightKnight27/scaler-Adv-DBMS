# Lab 4 - SQLite On-Disk Format Analysis

## Files

- library.db
- library.hex
- create_library.sql

## Schema

Table: books

Columns:
- id (INTEGER PRIMARY KEY)
- title (TEXT)
- author (TEXT)
- year_published (INTEGER)

Index:
- idx_books_year(year_published)

## Build

sqlite3 library.db < create_library.sql
xxd -g 1 -c 16 library.db > library.hex

## Verification

sqlite3 library.db ".dbinfo"
sqlite3 library.db "SELECT * FROM books;"
sqlite3 library.db "EXPLAIN QUERY PLAN SELECT * FROM books WHERE year_published=1965;"

## Notes

This database demonstrates:
1. SQLite file headers
2. Table B-tree pages
3. Index B-tree pages
4. Record storage
5. Index lookups using year_published
