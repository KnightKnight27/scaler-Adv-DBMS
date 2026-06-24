#!/bin/bash
# ============================================================================
# Name: Patel Jash
# Batch: B
# Roll: 24BCS10632
# Lab: 02
# Title: Storage_Engine_2
# ============================================================================

echo "========================================="
echo "     SQLITE3 STORAGE ENGINE LAB"
echo "========================================="

echo
echo "Step 1 -> Display database file details"
ls -lh sample.db

echo
echo "Step 2 -> Fetch the page size configured in SQLite"
sqlite3 sample.db "PRAGMA page_size;"

echo
echo "Step 3 -> Fetch total number of pages in the database"
sqlite3 sample.db "PRAGMA page_count;"

echo
echo "Step 4 -> Retrieve current memory-mapped I/O size"
sqlite3 sample.db "PRAGMA mmap_size;"

echo
echo "Step 5 -> Assign mmap_size as 30MB"
sqlite3 sample.db "PRAGMA mmap_size = 30000000;"

echo
echo "Step 6 -> Confirm updated mmap value"
sqlite3 sample.db "PRAGMA mmap_size;"

echo
echo "Step 7 -> Measure execution time of SELECT query"
time sqlite3 sample.db "SELECT * FROM users;" > /dev/null

echo
echo "Step 8 -> Look up the inode of the database file"
ls -i sample.db

echo
echo "Step 9 -> List any active sqlite3 processes"
ps aux | grep sqlite

echo
echo "========================================="
echo "    POSTGRESQL STORAGE ENGINE LAB"
echo "========================================="

echo
echo "Step 1 -> Display PostgreSQL block size setting"
sudo -u postgres psql -d labdb -c "SHOW block_size;"

echo
echo "Step 2 -> Estimate page count for users table"
sudo -u postgres psql -d labdb -c \
"SELECT pg_relation_size('users') / 8192 AS approx_pages;"

echo
echo "Step 3 -> Show shared_buffers configuration"
sudo -u postgres psql -d labdb -c "SHOW shared_buffers;"

echo
echo "Step 4 -> Measure SELECT query execution time"
sudo -u postgres psql -d labdb -c "\\timing on" \
-c "SELECT * FROM users;"

echo
echo "Step 5 -> Locate physical file path for users table"
sudo -u postgres psql -d labdb -c \
"SELECT pg_relation_filepath('users');"

echo
echo "Step 6 -> Locate physical file path for products table"
sudo -u postgres psql -d labdb -c \
"SELECT pg_relation_filepath('products');"

echo
echo "Step 7 -> List running PostgreSQL processes"
ps aux | grep postgres

echo
echo "========================================="
echo "         ALL STEPS FINISHED"
echo "========================================="