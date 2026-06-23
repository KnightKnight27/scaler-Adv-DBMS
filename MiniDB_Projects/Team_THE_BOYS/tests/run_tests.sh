#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PASS=0
FAIL=0

run_test() {
  local name="$1"
  local dbpath="$2"
  local input="$3"
  local expect="$4"

  rm -rf "$dbpath"
  local out
  out=$(./minidb "$dbpath" <<EOF
$input
EOF
)
  local status=$?

  if [[ $status -ne 0 ]]; then
    echo "FAIL: $name (exit $status)"
    echo "$out"
    FAIL=$((FAIL + 1))
    return
  fi

  if echo "$out" | grep -q "$expect"; then
    echo "PASS: $name"
    PASS=$((PASS + 1))
  else
    echo "FAIL: $name (expected pattern: $expect)"
    echo "$out"
    FAIL=$((FAIL + 1))
  fi
}

echo "=== MiniDB Test Suite ==="

run_test "create_table" "tests/tmp1" \
  "CREATE TABLE t (id INT PRIMARY KEY, v INT);" \
  "CREATE TABLE OK"

run_test "insert_select" "tests/tmp2" \
  "CREATE TABLE t (id INT PRIMARY KEY, name STRING);
INSERT INTO t (id, name) VALUES (1, 'Alice');
SELECT * FROM t;" \
  "Alice"

run_test "where_filter" "tests/tmp3" \
  "CREATE TABLE t (id INT PRIMARY KEY, age INT);
INSERT INTO t (id, age) VALUES (1, 30);
INSERT INTO t (id, age) VALUES (2, 40);
SELECT * FROM t WHERE age = 30;" \
  "(1 rows)"

run_test "delete" "tests/tmp4" \
  "CREATE TABLE t (id INT PRIMARY KEY, age INT);
INSERT INTO t (id, age) VALUES (1, 30);
INSERT INTO t (id, age) VALUES (2, 40);
DELETE FROM t WHERE id = 1;
SELECT * FROM t;" \
  "(1 rows)"

run_test "transaction" "tests/tmp5" \
  "CREATE TABLE t (id INT PRIMARY KEY, v INT);
BEGIN;
INSERT INTO t (id, v) VALUES (1, 100);
COMMIT;
SELECT * FROM t;" \
  "100"

run_test "rollback" "tests/tmp9" \
  "CREATE TABLE t (id INT PRIMARY KEY, v INT);
INSERT INTO t (id, v) VALUES (1, 1);
BEGIN;
INSERT INTO t (id, v) VALUES (2, 2);
ROLLBACK;
SELECT * FROM t;" \
  "(1 rows)"

run_test "secondary_index" "tests/tmp10" \
  "CREATE TABLE t (id INT PRIMARY KEY, dept INT INDEX);
INSERT INTO t (id, dept) VALUES (1, 10);
INSERT INTO t (id, dept) VALUES (2, 20);
SELECT * FROM t WHERE dept = 10;" \
  "10"

run_test "batch_mode" "tests/tmp6" \
  "CREATE TABLE t (id INT PRIMARY KEY, v INT);
INSERT INTO t (id, v) VALUES (1, 1);
INSERT INTO t (id, v) VALUES (2, 2);
SET EXEC_MODE BATCH;
SELECT * FROM t;" \
  "Execution mode: BATCH"

run_test "demo_script" "tests/tmp7" \
  "$(cat demo.sql | grep -v '^\.')" \
  "(2 rows)"

run_test "join" "tests/tmp8" \
  "CREATE TABLE a (id INT PRIMARY KEY, v INT);
CREATE TABLE b (id INT PRIMARY KEY, a_id INT);
INSERT INTO a (id, v) VALUES (1, 100);
INSERT INTO b (id, a_id) VALUES (10, 1);
SELECT * FROM a JOIN b ON a.id = b.a_id;" \
  "100"

run_test "count_all" "tests/tmp11" \
  "CREATE TABLE t (id INT PRIMARY KEY, v INT);
INSERT INTO t (id, v) VALUES (1, 10);
INSERT INTO t (id, v) VALUES (2, 20);
INSERT INTO t (id, v) VALUES (3, 30);
SELECT COUNT(*) FROM t;" \
  "3"

run_test "count_where" "tests/tmp12" \
  "CREATE TABLE t (id INT PRIMARY KEY, v INT);
INSERT INTO t (id, v) VALUES (1, 10);
INSERT INTO t (id, v) VALUES (2, 20);
INSERT INTO t (id, v) VALUES (3, 30);
SELECT COUNT(*) FROM t WHERE v = 20;" \
  "1"

run_test "group_by_count" "tests/tmp13" \
  "CREATE TABLE t (id INT PRIMARY KEY, dept INT, v INT);
INSERT INTO t (id, dept, v) VALUES (1, 10, 100);
INSERT INTO t (id, dept, v) VALUES (2, 10, 200);
INSERT INTO t (id, dept, v) VALUES (3, 20, 300);
SELECT dept, COUNT(*) FROM t GROUP BY dept;" \
  "10 | 2"

run_test "index_scan_plan" "tests/tmp14" \
  "CREATE TABLE t (id INT PRIMARY KEY, v INT);
INSERT INTO t (id, v) VALUES (1, 100);
SELECT * FROM t WHERE id = 1;" \
  "IndexScan"

echo ""
echo "=== Core demos (locking / deadlock) ==="
if make core_demos >/dev/null 2>&1 && ./core_demos 2>&1 | grep -q "PASS: deadlock detection"; then
  echo "PASS: core_demos"
  PASS=$((PASS + 1))
else
  echo "FAIL: core_demos"
  make core_demos 2>&1 || true
  ./core_demos 2>&1 || true
  FAIL=$((FAIL + 1))
fi

echo ""
echo "=== WAL crash recovery ==="
rm -rf tests/recover
./minidb tests/recover <<'EOF' >/dev/null
CREATE TABLE t (id INT PRIMARY KEY, v INT);
BEGIN;
INSERT INTO t (id, v) VALUES (1, 99);
INSERT INTO t (id, v) VALUES (2, 88);
COMMIT;
EOF
rm -f tests/recover/minidb.dat
out=$(./minidb tests/recover <<'EOF'
.recover
SELECT * FROM t;
.quit
EOF
)
if echo "$out" | grep -q "99" && echo "$out" | grep -q "88"; then
  echo "PASS: wal_recovery"
  PASS=$((PASS + 1))
else
  echo "FAIL: wal_recovery"
  echo "$out"
  FAIL=$((FAIL + 1))
fi

echo ""
echo "=== Join order selection ==="
rm -rf tests/joinorder
{
  echo "CREATE TABLE a (id INT PRIMARY KEY, v INT);"
  echo "CREATE TABLE b (id INT PRIMARY KEY, a_id INT);"
  echo "INSERT INTO a (id, v) VALUES (1, 1);"
  echo "BEGIN;"
  for i in $(seq 2 120); do
    echo "INSERT INTO b (id, a_id) VALUES ($i, 1);"
  done
  echo "COMMIT;"
} | ./minidb tests/joinorder >/dev/null 2>&1
out=$(./minidb tests/joinorder <<'EOF'
SELECT * FROM b JOIN a ON b.a_id = a.id;
.quit
EOF
)
if echo "$out" | grep -q "NestedLoopJoin(a,b)"; then
  echo "PASS: join_order"
  PASS=$((PASS + 1))
else
  echo "FAIL: join_order (expected NestedLoopJoin(a,b))"
  echo "$out"
  FAIL=$((FAIL + 1))
fi

echo ""
echo "=== B+ tree split (50 keys) ==="
rm -rf tests/split
./minidb tests/split <<'EOF' >/dev/null
CREATE TABLE big (id INT PRIMARY KEY);
EOF
for i in $(seq 0 49); do
  echo "INSERT INTO big (id) VALUES ($i);"
done | ./minidb tests/split >/dev/null 2>&1
out=$(./minidb tests/split <<'EOF'
SELECT * FROM big WHERE id = 25;
SELECT * FROM big;
.quit
EOF
)
if echo "$out" | grep -q "25" && echo "$out" | grep -q "(50 rows)"; then
  echo "PASS: btree_split"
  PASS=$((PASS + 1))
else
  echo "FAIL: btree_split"
  echo "$out"
  FAIL=$((FAIL + 1))
fi

echo ""
echo "=== Catalog persistence ==="
rm -rf tests/persist
./minidb tests/persist <<'EOF' >/dev/null
CREATE TABLE t (id INT PRIMARY KEY, v INT);
INSERT INTO t (id, v) VALUES (1, 42);
EOF
out=$(./minidb tests/persist <<'EOF'
SELECT * FROM t WHERE id = 1;
.quit
EOF
)
if echo "$out" | grep -q "42"; then
  echo "PASS: catalog_persist"
  PASS=$((PASS + 1))
else
  echo "FAIL: catalog_persist"
  echo "$out"
  FAIL=$((FAIL + 1))
fi

echo ""
echo "=== Benchmark smoke test ==="
rm -rf benchmark_data_*
if ./benchmark 2>/dev/null | grep -q "Track A Benchmark complete"; then
  echo "PASS: benchmark"
  PASS=$((PASS + 1))
else
  echo "FAIL: benchmark"
  ./benchmark 2>&1 || true
  FAIL=$((FAIL + 1))
fi

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
[[ $FAIL -eq 0 ]]
