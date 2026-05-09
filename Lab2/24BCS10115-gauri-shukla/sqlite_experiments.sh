#!/usr/bin/env bash
# Lab2 - SQLite3 exploration
# Roll: 24BCS10115  Name: Gauri Shukla
#
# Run each block manually (don't pipe the whole script) so you can read
# the output for each step. Each block prints what it's about to do.
#
# Prereq: brew install sqlite     (or use the system sqlite3)
# Sample DB: Chinook (small, free, well-known)
#   curl -L -o chinook.db https://github.com/lerocha/chinook-database/raw/master/ChinookDatabase/DataSources/Chinook_Sqlite.sqlite

set -u
DB="chinook.db"

echo "=========================================="
echo "[0] Versions and DB file"
echo "=========================================="
sqlite3 --version
ls -lh "$DB"
file "$DB"
echo

echo "=========================================="
echo "[1] PRAGMA: page_size, page_count, freelist, encoding"
echo "=========================================="
sqlite3 "$DB" <<'SQL'
.headers on
.mode column
PRAGMA page_size;
PRAGMA page_count;
PRAGMA freelist_count;
PRAGMA encoding;
PRAGMA journal_mode;
PRAGMA mmap_size;        -- default mmap_size (often 0 = disabled)
SQL
echo
echo "Sanity check: page_size * page_count should ~= file size"
echo

echo "=========================================="
echo "[2] Tables and row counts"
echo "=========================================="
sqlite3 "$DB" <<'SQL'
.headers on
.mode column
.tables
SELECT 'Album'     AS tbl, COUNT(*) FROM Album
UNION ALL SELECT 'Artist',     COUNT(*) FROM Artist
UNION ALL SELECT 'Customer',   COUNT(*) FROM Customer
UNION ALL SELECT 'Invoice',    COUNT(*) FROM Invoice
UNION ALL SELECT 'InvoiceLine',COUNT(*) FROM InvoiceLine
UNION ALL SELECT 'Track',      COUNT(*) FROM Track;
SQL
echo

echo "=========================================="
echo "[3] Time a SELECT * WITHOUT mmap"
echo "=========================================="
# .timer reports user/sys/wall time inside sqlite3.
# We also wrap with `time` to see total wall time of the process.
time sqlite3 "$DB" <<'SQL' > /tmp/sqlite_no_mmap.out
PRAGMA mmap_size = 0;     -- disable mmap explicitly
.timer on
SELECT * FROM Track;
SQL
wc -l /tmp/sqlite_no_mmap.out
echo

echo "=========================================="
echo "[4] Time the same SELECT * WITH mmap (256 MB)"
echo "=========================================="
time sqlite3 "$DB" <<'SQL' > /tmp/sqlite_mmap.out
PRAGMA mmap_size = 268435456;   -- 256 MB
.timer on
SELECT * FROM Track;
SQL
wc -l /tmp/sqlite_mmap.out
echo

echo "=========================================="
echo "[5] Heavier query (join) — mmap off vs on"
echo "=========================================="
echo "--- mmap_size = 0 ---"
time sqlite3 "$DB" <<'SQL' > /dev/null
PRAGMA mmap_size = 0;
.timer on
SELECT t.Name, ar.Name AS Artist, al.Title AS Album
FROM Track t
JOIN Album al  ON t.AlbumId  = al.AlbumId
JOIN Artist ar ON al.ArtistId = ar.ArtistId
ORDER BY ar.Name, al.Title, t.TrackId;
SQL

echo "--- mmap_size = 256 MB ---"
time sqlite3 "$DB" <<'SQL' > /dev/null
PRAGMA mmap_size = 268435456;
.timer on
SELECT t.Name, ar.Name AS Artist, al.Title AS Album
FROM Track t
JOIN Album al  ON t.AlbumId  = al.AlbumId
JOIN Artist ar ON al.ArtistId = ar.ArtistId
ORDER BY ar.Name, al.Title, t.TrackId;
SQL
echo

echo "=========================================="
echo "[6] Process inspection while a long query runs"
echo "=========================================="
# Start a slow-ish query in the background, then snapshot ps.
sqlite3 "$DB" "PRAGMA mmap_size=268435456; \
  SELECT COUNT(*) FROM Track t1, Track t2;" >/dev/null &
PID=$!
sleep 1
ps aux | grep -v grep | grep -E "sqlite3|PID"
wait $PID 2>/dev/null
echo

echo "=========================================="
echo "[7] EXPLAIN QUERY PLAN sanity"
echo "=========================================="
sqlite3 "$DB" <<'SQL'
EXPLAIN QUERY PLAN
SELECT t.Name, ar.Name FROM Track t
JOIN Album al  ON t.AlbumId  = al.AlbumId
JOIN Artist ar ON al.ArtistId = ar.ArtistId;
SQL

echo
echo "Done. Capture the timings above into README.md."
