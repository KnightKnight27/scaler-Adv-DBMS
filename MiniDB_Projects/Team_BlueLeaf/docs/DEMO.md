# MiniDB Demo Guide

Build first:

```bash
cd MiniDB_Projects/Team_BlueLeaf
make            # builds ./minidb   (or ./build.sh, which uses cmake if installed)
make test       # builds + runs all unit tests
make bench      # builds the benchmark binary at build/minidb_bench
```

## 1. Storage engine (M1)

```bash
./minidb selftest
# inserts 2000 rows through an 8-frame buffer pool, scans them back, and reports
# page allocation + buffer-pool hits/misses/evictions (eviction is exercised).
```

## 2. End-to-end SQL: execution + optimizer (M2/M3)

```bash
cat > demo.sql <<'SQL'
CREATE TABLE customers (id INT PRIMARY KEY, name VARCHAR(20), country VARCHAR(20));
CREATE TABLE orders (id INT PRIMARY KEY, cust INT, amount DOUBLE);
INSERT INTO customers VALUES (1,'alice','IN'),(2,'bob','US'),(3,'carol','IN'),(4,'dan','UK');
INSERT INTO orders VALUES (10,1,100.0),(11,1,250.0),(12,2,80.0),(13,3,300.0),(14,3,50.0);
SELECT id, name FROM customers WHERE id = 3;                       -- chooses an INDEX scan
SELECT id, name, country FROM customers WHERE id >= 2;             -- chooses a SEQ scan
SELECT c.name, o.amount FROM customers c JOIN orders o ON c.id = o.cust WHERE o.amount > 100;
SELECT c.country, COUNT(*), SUM(o.amount) FROM customers c JOIN orders o ON c.id = o.cust GROUP BY c.country;
DELETE FROM orders WHERE amount < 100;
SELECT COUNT(*) FROM orders;
SQL
./minidb run shop.db demo.sql
```

Each SELECT prints a `-- plan:` line. Note the optimizer choosing **IndexScan** for the selective
`id = 3` but **SeqScan** for the non-selective `id >= 2`, and **HashJoin (build on customers)** (the
smaller table). There is also an interactive shell: `./minidb repl shop.db`.

## 3. Transactions: 2PL + deadlock (M4)

```bash
./minidb concurrency
# Scenario 1: two transactions take SHARED locks on the same row (coexist).
# Scenario 2: a writer (EXCLUSIVE) blocks a reader until it commits.
# Scenario 3: an induced deadlock; the waits-for detector aborts a victim, the other commits.
```

## 4. Crash recovery: WAL (M5)

```bash
./minidb recover-demo
# Commits two rows (WAL flushed, data NOT checkpointed), writes one UNCOMMITTED row,
# simulates a crash, reopens, and shows committed rows survived while the uncommitted one
# was rolled back.
```

Across separate processes:

```bash
./minidb exec mydb.db "CREATE TABLE t (id INT PRIMARY KEY, v INT)"
./minidb crash mydb.db "INSERT INTO t VALUES (1,10),(2,20),(3,30)"   # commits to WAL, then 'crashes'
./minidb exec mydb.db "SELECT id, v FROM t"                          # recovers all 3 committed rows
```

## 5. Extension Track C: LSM vs B+Tree benchmark (M5)

```bash
./build/minidb_bench 100000 50000     # N inserts, M random reads
# Prints write throughput, point-read latency, and space amplification for both engines,
# and writes benchmarks/results/bench.csv. Representative result (working set in cache):
#   write:  LSM ~6.5x the row store        (LSM is write-optimized)
#   reads:  B+Tree ~3.6x faster than LSM    (LSM has read amplification)
#   space:  similar; LSM ~1.12x after compaction
```
