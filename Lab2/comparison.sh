#!/bin/bash
# lab2_comparison.sh
# Compares PostgreSQL and SQLite: setup, queries, performance, and features.

PSQL_DB="lab2_db"
PSQL_USER="postgres"
SQLITE_DB="lab2_db.db"
ROWS=1000

echo "============================================"
echo "   Lab 2: PostgreSQL vs SQLite Comparison"
echo "============================================"

# ──────────────────────────────────────────────
# SECTION 1: SETUP
# ──────────────────────────────────────────────
echo ""
echo ">>> SECTION 1: Setup"

echo "[PSQL] Creating database and students table..."
sudo -u $PSQL_USER psql -c "DROP DATABASE IF EXISTS $PSQL_DB;"
sudo -u $PSQL_USER psql -c "CREATE DATABASE $PSQL_DB;"
sudo -u $PSQL_USER psql -d $PSQL_DB <<EOF
CREATE TABLE students (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    age INT,
    grade CHAR(1)
);
INSERT INTO students (name, age, grade) VALUES
    ('Alice', 20, 'A'), ('Bob', 22, 'B'),
    ('Charlie', 21, 'A'), ('Diana', 23, 'C'), ('Eve', 20, 'B');
SELECT * FROM students;
EOF

echo "[SQLite] Creating database and students table..."
rm -f $SQLITE_DB
sqlite3 $SQLITE_DB <<EOF
CREATE TABLE students (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    age INTEGER,
    grade TEXT
);
INSERT INTO students (name, age, grade) VALUES
    ('Alice', 20, 'A'), ('Bob', 22, 'B'),
    ('Charlie', 21, 'A'), ('Diana', 23, 'C'), ('Eve', 20, 'B');
SELECT * FROM students;
EOF

# ──────────────────────────────────────────────
# SECTION 2: PERFORMANCE BENCHMARK
# ──────────────────────────────────────────────
echo ""
echo ">>> SECTION 2: Performance Benchmark ($ROWS rows)"

# PostgreSQL bulk insert
PSQL_VALUES=""
for i in $(seq 1 $ROWS); do
    PSQL_VALUES+="('user_$i', $RANDOM)"
    [ $i -lt $ROWS ] && PSQL_VALUES+=","
done

sudo -u $PSQL_USER psql -d $PSQL_DB -c "CREATE TABLE perf_test (id SERIAL PRIMARY KEY, name TEXT, value INT);"

echo "[PSQL] Insert time:"
time sudo -u $PSQL_USER psql -d $PSQL_DB -c "INSERT INTO perf_test (name, value) VALUES $PSQL_VALUES;"
echo "[PSQL] AVG query time:"
time sudo -u $PSQL_USER psql -d $PSQL_DB -c "SELECT AVG(value) FROM perf_test;"

# SQLite bulk insert
sqlite3 $SQLITE_DB "CREATE TABLE perf_test (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, value INTEGER);"
SQLITE_SQL="BEGIN TRANSACTION;"
for i in $(seq 1 $ROWS); do
    SQLITE_SQL+="INSERT INTO perf_test (name, value) VALUES ('user_$i', $RANDOM);"
done
SQLITE_SQL+="COMMIT;"

echo "[SQLite] Insert time:"
time echo "$SQLITE_SQL" | sqlite3 $SQLITE_DB
echo "[SQLite] AVG query time:"
time sqlite3 $SQLITE_DB "SELECT AVG(value) FROM perf_test;"

# ──────────────────────────────────────────────
# SECTION 3: FEATURE COMPARISON
# ──────────────────────────────────────────────
echo ""
echo ">>> SECTION 3: Feature Comparison (JSON & Arrays)"

echo "[PSQL] Native JSONB and ARRAY types..."
sudo -u $PSQL_USER psql -d $PSQL_DB <<EOF
CREATE TABLE products (id SERIAL PRIMARY KEY, name TEXT, metadata JSONB);
INSERT INTO products (name, metadata) VALUES
    ('Laptop', '{"brand": "Dell", "ram": 16, "tags": ["portable", "work"]}'),
    ('Phone',  '{"brand": "Apple", "storage": 128, "tags": ["mobile", "5G"]}');
SELECT name, metadata->>'brand' AS brand FROM products;

CREATE TABLE courses (id SERIAL PRIMARY KEY, title TEXT, topics TEXT[]);
INSERT INTO courses (title, topics) VALUES
    ('Databases', ARRAY['SQL', 'Indexing', 'Transactions']),
    ('Networking', ARRAY['TCP/IP', 'DNS', 'HTTP']);
SELECT title, topics FROM courses;
EOF

echo "[SQLite] JSON as TEXT + json_extract, arrays as CSV..."
sqlite3 $SQLITE_DB <<EOF
CREATE TABLE products (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, metadata TEXT);
INSERT INTO products (name, metadata) VALUES
    ('Laptop', '{"brand": "Dell", "ram": 16}'),
    ('Phone',  '{"brand": "Apple", "storage": 128}');
SELECT name, json_extract(metadata, '$.brand') AS brand FROM products;

CREATE TABLE courses (id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT, topics TEXT);
INSERT INTO courses (title, topics) VALUES
    ('Databases', 'SQL,Indexing,Transactions'),
    ('Networking', 'TCP/IP,DNS,HTTP');
SELECT title, topics FROM courses;
EOF

echo ""
echo "============================================"
echo "   All Sections Complete"
echo "============================================"
echo ""
echo "Key Takeaways:"
echo "  - SQLite: zero config, great for local/embedded apps"
echo "  - PostgreSQL: powerful types, best for production/multi-user"