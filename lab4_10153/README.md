# Lab 4 - SQLite Page Layout Walkthrough

## Student Details
- Name: Siddhant
- Roll Number: 10153

## Objective
Inspect how SQLite stores table and index data on disk using fixed-size pages.

## Files
- campus.sql
- campus.db
- campus.hex
- README.md

## Commands Used

```bash
sqlite3 campus.db ".read campus.sql"
xxd -g 1 -c 16 campus.db > campus.hex
sqlite3 campus.db "PRAGMA page_size;"
sqlite3 campus.db "PRAGMA page_count;"