#!/bin/bash
# Lab 1 — SQLite3 vs PostgreSQL storage exploration
# Reproduces every experiment from README.md.
# Run from the repo root after creating sample.db (see README) and a PG db named `labdb`.

echo "========================================="
echo "         SQLITE3 EXPERIMENTS"
echo "========================================="

echo
echo "--- Step 1: file size on disk ---"
ls -lh sample.db

echo
echo "--- Step 2: page size ---"
sqlite3 sample.db "PRAGMA page_size;"

echo
echo "--- Step 3: page count ---"
sqlite3 sample.db "PRAGMA page_count;"

echo
echo "--- Step 4: default mmap_size ---"
sqlite3 sample.db "PRAGMA mmap_size;"

echo
echo "--- Step 5: enable mmap (30 MB) ---"
sqlite3 sample.db "PRAGMA mmap_size = 31457280;"

echo
echo "--- Step 6: confirm mmap_size is set (per connection) ---"
sqlite3 sample.db "PRAGMA mmap_size = 31457280; PRAGMA mmap_size;"

echo
echo "--- Step 7: SELECT * FROM users WITHOUT mmap (3 runs) ---"
for i in 1 2 3; do
  echo "Run $i:"
  time sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM users;" > /dev/null
done

echo
echo "--- Step 8: SELECT * FROM users WITH mmap=30MB (3 runs) ---"
for i in 1 2 3; do
  echo "Run $i:"
  time sqlite3 sample.db "PRAGMA mmap_size=31457280; SELECT * FROM users;" > /dev/null
done

echo
echo "--- Step 9: inode of sample.db ---"
ls -i sample.db

echo
echo "--- Step 10: sqlite3 process list ---"
ps aux | grep '[s]qlite3' || echo "(no sqlite3 processes — embedded DB only runs while the CLI is open)"

echo
echo "--- Step 11: prove all tables share one file ---"
sqlite3 sample.db "CREATE TABLE IF NOT EXISTS products (id INTEGER PRIMARY KEY, name TEXT, price REAL);"
sqlite3 sample.db "INSERT INTO products(name, price) VALUES ('Pen', 10.5), ('Notebook', 55.0);"
ls -lh sample.db
ls -i sample.db

echo
echo "========================================="
echo "       POSTGRESQL EXPERIMENTS"
echo "========================================="

# Adjust connection flags if your cluster uses a TCP port instead of /tmp socket
PSQL="psql -h /tmp -d labdb"

echo
echo "--- Step 1: block (page) size ---"
$PSQL -c "SHOW block_size;"

echo
echo "--- Step 2: heap size + approx page count for users ---"
$PSQL -c "SELECT pg_relation_size('users')                                    AS heap_bytes,
                 pg_relation_size('users') / 8192                              AS approx_pages,
                 pg_size_pretty(pg_relation_size('users'))                     AS heap,
                 pg_size_pretty(pg_total_relation_size('users'))               AS heap_plus_indexes;"

echo
echo "--- Step 3: shared_buffers (PG's in-process cache) ---"
$PSQL -c "SHOW shared_buffers;"

echo
echo "--- Step 4: time SELECT * FROM users with \\timing ---"
$PSQL -c "\timing on" -c "SELECT * FROM users;" > /dev/null

echo
echo "--- Step 5: file path of users table ---"
$PSQL -c "SELECT pg_relation_filepath('users');"

echo
echo "--- Step 6: each table is a separate file ---"
$PSQL -c "CREATE TABLE IF NOT EXISTS products (id SERIAL PRIMARY KEY, name TEXT, price REAL);"
$PSQL -c "SELECT pg_relation_filepath('products');"

echo
echo "--- Step 7: postgres processes (running even with no clients connected) ---"
ps aux | grep '[p]ostgres'

echo
echo "========================================="
echo "          EXPERIMENTS COMPLETE"
echo "========================================="
