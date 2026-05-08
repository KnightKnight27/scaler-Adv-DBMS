#!/bin/bash
# =============================================================================
#  psql_explore.sh
#  PostgreSQL page size, page count, shared buffers, query timing exploration
#
#  Author : Anushka Jain | 24BCS10193
#  Course : Advanced DBMS (Scaler)
# =============================================================================

DB="anushka_lab2"
PG_USER="${PGUSER:-postgres}"

echo ""
echo "============================================================"
echo "  PostgreSQL Exploration — Anushka Jain | 24BCS10193"
echo "============================================================"
echo ""

# -----------------------------------------------------------------------------
#  Setup: drop & recreate database
# -----------------------------------------------------------------------------
echo "[+] Setting up database '$DB'..."

psql -U "$PG_USER" -c "DROP DATABASE IF EXISTS $DB;" 2>/dev/null
psql -U "$PG_USER" -c "CREATE DATABASE $DB;"

psql -U "$PG_USER" -d "$DB" <<'EOF'
-- Create a students table mirroring the SQLite schema for fair comparison
CREATE TABLE IF NOT EXISTS students (
    id      SERIAL PRIMARY KEY,
    name    TEXT    NOT NULL,
    email   TEXT    NOT NULL,
    score   NUMERIC(5,1),
    dept    TEXT
);

-- Bulk insert 1000 rows using generate_series
INSERT INTO students (name, email, score, dept)
SELECT
    'student_' || g,
    'student_' || g || '@college.edu',
    round((50 + ((g * 17 + 13) % 50))::numeric, 1),
    CASE g % 4
        WHEN 0 THEN 'CSE'
        WHEN 1 THEN 'ECE'
        WHEN 2 THEN 'MECH'
        ELSE 'CIVIL'
    END
FROM generate_series(1, 1000) AS g;
EOF

echo ""
echo "--- PostgreSQL block (page) size ---"
psql -U "$PG_USER" -d "$DB" -c "SHOW block_size;"

echo ""
echo "--- Relation size and page count for 'students' ---"
psql -U "$PG_USER" -d "$DB" -c "
SELECT
    pg_relation_size('students')                                          AS relation_bytes,
    pg_size_pretty(pg_relation_size('students'))                          AS relation_pretty,
    pg_relation_size('students') / current_setting('block_size')::int    AS page_count;
"

echo ""
echo "--- Total size including indexes and TOAST ---"
psql -U "$PG_USER" -d "$DB" -c "
SELECT pg_size_pretty(pg_total_relation_size('students')) AS total_size;
"

echo ""
echo "--- Shared buffers (PostgreSQL in-memory cache) ---"
psql -U "$PG_USER" -d "$DB" -c "SHOW shared_buffers;"

echo ""
echo "--- Work mem (per-operation memory) ---"
psql -U "$PG_USER" -d "$DB" -c "SHOW work_mem;"

echo ""
echo "============================================================"
echo "  QUERY TIMING"
echo "============================================================"

echo ""
echo "--- Sequential scan with EXPLAIN ANALYZE ---"
psql -U "$PG_USER" -d "$DB" -c "EXPLAIN ANALYZE SELECT * FROM students;"

echo ""
echo "--- Aggregate query with EXPLAIN ANALYZE ---"
psql -U "$PG_USER" -d "$DB" -c "
EXPLAIN ANALYZE
SELECT dept, COUNT(*), AVG(score)
FROM students
GROUP BY dept
ORDER BY dept;
"

echo ""
echo "--- Buffer hit ratio (cache efficiency) ---"
psql -U "$PG_USER" -d "$DB" -c "
SELECT
    heap_blks_read   AS disk_reads,
    heap_blks_hit    AS cache_hits,
    round(heap_blks_hit::numeric /
          NULLIF(heap_blks_hit + heap_blks_read, 0) * 100, 2) AS hit_ratio_pct
FROM pg_statio_user_tables
WHERE relname = 'students';
"

echo ""
echo "--- Row count verification ---"
psql -U "$PG_USER" -d "$DB" -c "SELECT COUNT(*) AS total_rows FROM students;"

# -----------------------------------------------------------------------------
#  Cleanup
# -----------------------------------------------------------------------------
echo ""
echo "--- Cleaning up ---"
psql -U "$PG_USER" -c "DROP DATABASE IF EXISTS $DB;"
echo "Done."
echo ""
