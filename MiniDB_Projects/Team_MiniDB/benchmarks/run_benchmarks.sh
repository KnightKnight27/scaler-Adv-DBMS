#!/bin/bash
# MiniDB benchmark script — compares row-at-a-time vs batch filtering (Track A)

set -e
cd "$(dirname "$0")/.."

if [ ! -f ./minidb ]; then
  make
fi

DB=test_bench
rm -rf minidb_data

run_queries() {
  local mode=$1
  printf "CREATE TABLE t (id INT PRIMARY KEY, val INT);\n" 
  for i in $(seq 1 200); do
    printf "INSERT INTO t VALUES (%d, %d);\n" "$i" "$((i * 2))"
  done
  if [ "$mode" = "batch" ]; then
    printf "SET BATCH ON;\n"
  else
    printf "SET BATCH OFF;\n"
  fi
  printf "SELECT * FROM t WHERE val > 100;\n"
  printf "exit\n"
}

echo "=== MiniDB Track A Benchmark ==="
echo ""
echo "Row-at-a-time mode:"
START=$(python3 -c "import time; print(time.time())")
run_queries row | ./minidb > /dev/null
END=$(python3 -c "import time; print(time.time())")
ROW_TIME=$(python3 -c "print(round($END - $START, 4))")
echo "  Time: ${ROW_TIME}s"

rm -rf minidb_data

echo ""
echo "Batch mode (batch size 64):"
START=$(python3 -c "import time; print(time.time())")
run_queries batch | ./minidb > /dev/null
END=$(python3 -c "import time; print(time.time())")
BATCH_TIME=$(python3 -c "print(round($END - $START, 4))")
echo "  Time: ${BATCH_TIME}s"

echo ""
echo "Analysis:"
echo "  Batch processing groups rows into chunks of 64 for better CPU cache use."
echo "  On small datasets the difference may be tiny; on larger scans batch wins."
echo "  Row mode:  ${ROW_TIME}s | Batch mode: ${BATCH_TIME}s"
