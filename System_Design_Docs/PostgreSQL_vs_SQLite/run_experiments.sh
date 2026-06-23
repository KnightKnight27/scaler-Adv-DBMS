#!/usr/bin/env bash
# SQLite experiments for System Design submission
set -euo pipefail

WORKDIR="$(cd "$(dirname "$0")" && pwd)/experiment_data"
mkdir -p "$WORKDIR"
DB="$WORKDIR/test.db"
rm -f "$DB" "$DB-wal" "$DB-shm"

echo "=== SQLite version ==="
sqlite3 --version

echo ""
echo "=== Setup: create tables and insert 100,000 rows ==="
sqlite3 "$DB" <<'SQL'
PRAGMA journal_mode=WAL;
PRAGMA synchronous=FULL;

CREATE TABLE users (
    id   INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    age  INTEGER
);

CREATE TABLE orders (
    id      INTEGER PRIMARY KEY,
    user_id INTEGER NOT NULL,
    amount  REAL NOT NULL
);

CREATE INDEX idx_users_age ON users(age);
CREATE INDEX idx_orders_user_id ON orders(user_id);
SQL

# Insert users
for i in $(seq 1 100000); do
  echo "INSERT INTO users VALUES ($i, 'user_$i', $((18 + i % 60)));"
done | sqlite3 "$DB"

# Insert 50,000 orders
for i in $(seq 1 50000); do
  uid=$((1 + i % 100000))
  echo "INSERT INTO orders VALUES ($i, $uid, $((i % 5000 + 10)).99);"
done | sqlite3 "$DB"

ANALYZE=$(sqlite3 "$DB" "ANALYZE;")

echo ""
echo "=== Experiment 1: Point lookup (warm cache, 100 runs) ==="
sqlite3 "$DB" <<'SQL'
.timer on
SELECT COUNT(*) FROM users WHERE id = 50000;
SQL

echo ""
echo "=== Experiment 1b: Timing 1000 point lookups ==="
/usr/bin/time -p sqlite3 "$DB" "SELECT COUNT(*) FROM (SELECT * FROM users WHERE id = 50000);" >/dev/null 2>&1 || true
START=$(python3 -c "import time; print(time.perf_counter())")
for i in $(seq 1 1000); do
  sqlite3 "$DB" "SELECT * FROM users WHERE id = 50000;" >/dev/null
done
END=$(python3 -c "import time; print(time.perf_counter())")
python3 -c "start=$START; end=$END; print(f'1000 lookups: {(end-start)*1000:.2f} ms total, {(end-start):.4f} s, avg {(end-start)*1000/1000:.3f} ms per lookup')"

echo ""
echo "=== Experiment 2: EXPLAIN QUERY PLAN (join) ==="
sqlite3 "$DB" <<'SQL'
EXPLAIN QUERY PLAN
SELECT u.name, o.amount
FROM users u
JOIN orders o ON u.id = o.user_id
WHERE u.age > 30;
SQL

echo ""
echo "=== Experiment 3: Join query timing ==="
START=$(python3 -c "import time; print(time.perf_counter())")
sqlite3 "$DB" "SELECT COUNT(*) FROM users u JOIN orders o ON u.id = o.user_id WHERE u.age > 30;" >/dev/null
END=$(python3 -c "import time; print(time.perf_counter())")
python3 -c "start=$START; end=$END; print(f'Join count query: {(end-start)*1000:.2f} ms')"

echo ""
echo "=== Experiment 4: File sizes ==="
ls -lh "$DB" "$DB-wal" "$DB-shm" 2>/dev/null || ls -lh "$DB"
du -h "$DB"

echo ""
echo "=== Experiment 5: Concurrent writes (10 processes x 100 inserts) ==="
rm -f "$WORKDIR/concurrent.db" "$WORKDIR/concurrent.db-wal" "$WORKDIR/concurrent.db-shm"
sqlite3 "$WORKDIR/concurrent.db" "PRAGMA journal_mode=WAL; CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT);"

START=$(python3 -c "import time; print(time.perf_counter())")
BUSY=0
for p in $(seq 1 10); do
  (
    for i in $(seq 1 100); do
      id=$((p * 1000 + i))
      if ! sqlite3 "$WORKDIR/concurrent.db" "INSERT INTO t VALUES ($id, 'p${p}_$i');" 2>/dev/null; then
        echo busy >> "$WORKDIR/busy_count.txt"
      fi
    done
  ) &
done
wait
END=$(python3 -c "import time; print(time.perf_counter())")
BUSY=$(wc -l < "$WORKDIR/busy_count.txt" 2>/dev/null || echo 0)
ROWS=$(sqlite3 "$WORKDIR/concurrent.db" "SELECT COUNT(*) FROM t;")
python3 -c "start=$START; end=$END; print(f'Concurrent write test: {(end-start)*1000:.0f} ms, rows inserted: $ROWS, SQLITE_BUSY count: $BUSY')"

echo ""
echo "=== Experiment 6: Durability (commit then verify) ==="
rm -f "$WORKDIR/durability.db" "$WORKDIR/durability.db-wal"
sqlite3 "$WORKDIR/durability.db" "PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; CREATE TABLE d (id INTEGER PRIMARY KEY); BEGIN; INSERT INTO d VALUES (1); COMMIT;"
COUNT=$(sqlite3 "$WORKDIR/durability.db" "SELECT COUNT(*) FROM d;")
echo "Rows after commit: $COUNT"

echo ""
echo "Done. Database files in $WORKDIR"
