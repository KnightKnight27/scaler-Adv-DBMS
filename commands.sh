#!/bin/bash

echo "========================================="
echo "   LAB 2: DATABASE PERFORMANCE ANALYSIS"
echo "========================================="
echo

# ========== SQLITE3 EXPERIMENTS ==========
echo "PART 1: SQLITE3 EXPLORATION"
echo "========================================="
echo

# Setup
echo "[1] Creating SQLite3 database and table..."
sqlite3 test.db << EOF
CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT, email TEXT);
INSERT INTO users (name, email) VALUES 
  ('Alice', 'alice@example.com'),
  ('Bob', 'bob@example.com'),
  ('Charlie', 'charlie@example.com');
EOF
echo "✓ Database created"
echo

# File size
echo "[2] Checking database file size:"
ls -lh test.db | awk '{print $9, "(" $5 ")"}'
echo

# Page size
echo "[3] Checking SQLite page size:"
sqlite3 test.db "PRAGMA page_size;"
echo

# Page count
echo "[4] Checking SQLite page count:"
sqlite3 test.db "PRAGMA page_count;"
echo

# mmap status
echo "[5] Current mmap_size status:"
sqlite3 test.db "PRAGMA mmap_size;"
echo

# Enable mmap
echo "[6] Enabling mmap (30MB):"
sqlite3 test.db "PRAGMA mmap_size = 30000000;"
sqlite3 test.db "PRAGMA mmap_size;"
echo

# Query timing without mmap
echo "[7] Query timing (mmap disabled):"
sqlite3 test.db "PRAGMA mmap_size = 0;"
time sqlite3 test.db "SELECT * FROM users;" > /dev/null
echo

# Query timing with mmap
echo "[8] Query timing (mmap enabled - 30MB):"
sqlite3 test.db "PRAGMA mmap_size = 30000000;"
time sqlite3 test.db "SELECT * FROM users;" > /dev/null
echo

# Process check
echo "[9] SQLite process inspection:"
echo "Starting sqlite3 in background..."
sqlite3 test.db << EOF
SELECT COUNT(*) FROM users;
EOF
ps aux | grep -E "sqlite3|postgres" | grep -v grep
echo

echo
echo "========================================="
echo "   PART 2: POSTGRESQL SETUP & EXPERIMENTS"
echo "========================================="
echo

# PostgreSQL setup
echo "[1] Creating PostgreSQL database and table..."
sudo -u postgres psql << EOF > /dev/null 2>&1
DROP DATABASE IF EXISTS testdb;
CREATE DATABASE testdb;
EOF

sudo -u postgres psql -d testdb << EOF > /dev/null 2>&1
CREATE TABLE users (id SERIAL PRIMARY KEY, name TEXT, email TEXT);
INSERT INTO users (name, email) VALUES 
  ('Alice', 'alice@example.com'),
  ('Bob', 'bob@example.com'),
  ('Charlie', 'charlie@example.com');
EOF
echo "✓ PostgreSQL database created"
echo

# Block size
echo "[2] PostgreSQL block size:"
sudo -u postgres psql -d testdb -c "SHOW block_size;"
echo

# Table size
echo "[3] Table size in blocks:"
sudo -u postgres psql -d testdb -c "SELECT pg_relation_size('users') / 8192 AS blocks_used;"
echo

# Shared buffers
echo "[4] Shared buffers (cache):"
sudo -u postgres psql -d testdb -c "SHOW shared_buffers;"
echo

# Query timing
echo "[5] Query timing:"
sudo -u postgres psql -d testdb << EOF
\timing on
SELECT COUNT(*) FROM users;
EOF
echo

# Process check
echo "[6] PostgreSQL processes:"
ps aux | grep postgres | grep -v grep | head -3
echo

echo
echo "========================================="
echo "            COMPARISON SUMMARY"
echo "========================================="
echo
echo "SQLite3 Page Size:"
sqlite3 test.db "PRAGMA page_size;"
echo
echo "PostgreSQL Block Size:"
sudo -u postgres psql -d testdb -c "SHOW block_size;"
echo
echo "✓ Lab experiments completed"