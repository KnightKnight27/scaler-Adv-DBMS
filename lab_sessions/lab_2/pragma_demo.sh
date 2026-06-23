#!/usr/bin/env bash
# Lab 2: SQLite3 PRAGMA introspection demo

DB="lab2.db"

echo "=== Setting up database ==="
sqlite3 "$DB" < setup.sql

echo ""
echo "=== PRAGMA page_size ==="
sqlite3 "$DB" "PRAGMA page_size;"

echo ""
echo "=== PRAGMA page_count ==="
sqlite3 "$DB" "PRAGMA page_count;"

echo ""
echo "=== Total file size (page_size * page_count) ==="
PAGE_SIZE=$(sqlite3 "$DB" "PRAGMA page_size;")
PAGE_COUNT=$(sqlite3 "$DB" "PRAGMA page_count;")
echo "$((PAGE_SIZE * PAGE_COUNT)) bytes"

echo ""
echo "=== PRAGMA mmap_size (default) ==="
sqlite3 "$DB" "PRAGMA mmap_size;"

echo ""
echo "=== Enable mmap (256 MB) and verify ==="
sqlite3 "$DB" "PRAGMA mmap_size = 268435456; PRAGMA mmap_size;"

echo ""
echo "=== PRAGMA journal_mode ==="
sqlite3 "$DB" "PRAGMA journal_mode;"

echo ""
echo "=== PRAGMA cache_size ==="
sqlite3 "$DB" "PRAGMA cache_size;"

echo ""
echo "=== PRAGMA integrity_check ==="
sqlite3 "$DB" "PRAGMA integrity_check;"

echo ""
echo "=== PRAGMA database_list ==="
sqlite3 "$DB" "PRAGMA database_list;"

echo ""
echo "=== Verify no sqlite server process ==="
ps aux | grep -i sqlite | grep -v grep || echo "(no sqlite server process — it is a library, not a daemon)"

echo ""
echo "=== ldd / otool: sqlite3 is dynamically linked ==="
if command -v ldd &>/dev/null; then
    ldd "$(which sqlite3)" | grep sqlite
else
    otool -L "$(which sqlite3)" | grep sqlite
fi
