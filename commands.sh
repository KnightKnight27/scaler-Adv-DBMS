#!/bin/bash

echo "========================================="
echo "         SQLITE3 EXPERIMENTS"
echo "========================================="

echo
echo "--- Step 1: Check file size after creating DB ---"
ls -lh sample.db

echo
echo "--- Step 2: Check page size ---"
sqlite3 sample.db "PRAGMA page_size;"

echo
echo "--- Step 3: Check page count ---"
sqlite3 sample.db "PRAGMA page_count;"

echo
echo "--- Step 4: Check default mmap_size ---"
sqlite3 sample.db "PRAGMA mmap_size;"

echo
echo "--- Step 5: Set mmap_size to 30MB ---"
sqlite3 sample.db "PRAGMA mmap_size = 31457280;"

echo
echo "--- Step 6: Confirm mmap_size is now set ---"
sqlite3 sample.db "PRAGMA mmap_size;"

echo
echo "--- Step 7: Time SELECT query WITHOUT mmap ---"
time sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM users;" > /dev/null

echo
echo "--- Step 8: Time SELECT query WITH mmap (30MB) ---"
time sqlite3 sample.db "PRAGMA mmap_size=31457280; SELECT * FROM users;" > /dev/null

echo
echo "--- Step 9: Check inode number of sample.db ---"
ls -i sample.db

echo
echo "--- Step 10: Check sqlite3 process ---"
ps aux | grep sqlite

echo
echo "--- Step 11: Create a second table to test embedded nature ---"
sqlite3 sample.db "CREATE TABLE IF NOT EXISTS products (id INTEGER PRIMARY KEY, name TEXT, price REAL);"
sqlite3 sample.db "INSERT INTO products (name, price) VALUES ('Pen', 10.5), ('Notebook', 55.0);"
ls -lh sample.db
ls -i sample.db

echo
echo "========================================="
echo "       POSTGRESQL EXPERIMENTS"
echo "========================================="

echo
echo "--- Step 1: Check PostgreSQL block size ---"
psql -d mydb -c "SHOW block_size;"

echo
echo "--- Step 2: Approximate page count for users table ---"
psql -d mydb -c "SELECT pg_relation_size('users') / 8192 AS approx_pages;"

echo
echo "--- Step 3: Check shared_buffers ---"
psql -d mydb -c "SHOW shared_buffers;"

echo
echo "--- Step 4: Time SELECT query ---"
psql -d mydb -c "\timing on" -c "SELECT * FROM users;" > /dev/null

echo
echo "--- Step 5: Check file path of users table ---"
psql -d mydb -c "SELECT pg_relation_filepath('users');"

echo
echo "--- Step 6: Create products table and check its file path ---"
psql -d mydb -c "CREATE TABLE IF NOT EXISTS products (id SERIAL PRIMARY KEY, name TEXT, price REAL);"
psql -d mydb -c "SELECT pg_relation_filepath('products');"

echo
echo "--- Step 7: Check postgres processes (while connected) ---"
ps aux | grep postgres

echo
echo "--- Step 8: Check postgres processes (after exiting psql) ---"
echo "(run this manually after exiting psql to observe background processes still running)"

echo
echo "========================================="
echo "          EXPERIMENTS COMPLETE"
echo "========================================="
