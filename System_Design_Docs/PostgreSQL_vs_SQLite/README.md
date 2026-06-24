## PostgreSQL vs SQLite — System Design Discussion

This document is a scaffold to help you complete the System Design Discussion assignment for the Advanced DBMS course. It contains the required sections, a clear set of experiments you can run locally (Postgres and SQLite), exact commands to run, and instructions what output to paste back here so I can finish the analysis and prepare the GitHub PR.

## 1. Problem background

Databases exist to store, retrieve, and manage structured data reliably and efficiently. PostgreSQL and SQLite are two widely-used relational database systems with different design goals:
- PostgreSQL: a full-featured, extensible, client-server RDBMS designed for multi-user, concurrent, and high-scale deployments.
- SQLite: a lightweight, embedded SQL database engine designed for simplicity, minimal configuration, and use inside applications (mobile apps, embedded devices, single-user apps).

This topic compares their architectures, storage engines, concurrency/durability mechanisms, and trade-offs that lead to different real-world use cases.

## 2. Architecture overview

- PostgreSQL: client-server process model. Frontend clients connect over sockets to a server process (postmaster) that forks worker/back-end processes. Shared memory (shared buffers) caches pages and coordinates concurrency. WAL (Write-Ahead Logging) provides durability and crash recovery.
- SQLite: library linked into the application process (embedded). No separate server. Uses a single database file and a journal (DELETE, TRUNCATE, WAL) to ensure atomicity/durability. Concurrency is limited because writers lock the database file (WAL mode reduces reader-writer conflict).

Include a simple diagram here showing: client(s) -> PostgreSQL server -> storage (data files + WAL); and application process -> SQLite library -> single DB file (+ journal/WAL).

## 3. Internal design (high level)

- Storage structures:
  - PostgreSQL: pages (8KB default) in the heap, indexes (B-Tree, etc.) stored in separate files, WAL segments in pg_wal.
  - SQLite: pages (configurable) inside a single database file (.db), separate persistent WAL file when in WAL mode.
- Memory management:
  - PostgreSQL: shared buffers, background writer, checkpointer.
  - SQLite: page cache inside the process; memory-mapped IO optional.
- Index organization: both commonly use B-Tree for primary indexes; PostgreSQL supports more index types (GIN, GiST, BRIN).
- Transaction processing & concurrency control:
  - PostgreSQL: MVCC using tuple versions (xmin/xmax) in heap tuples; concurrent readers and writers with minimal blocking; VACUUM to reclaim space.
  - SQLite: database-level or page-level locking (depending on journaling mode); WAL enables concurrent readers with a single writer.
- Recovery: PostgreSQL uses WAL and checkpointing; SQLite uses rollback journal or WAL files for recovery.

## 4. Design trade-offs

- PostgreSQL advantages: high concurrency, advanced query planner, extensibility, rich indexing, strong consistency and durability guarantees for multi-user workloads.
- PostgreSQL limitations: more resource usage (memory, processes), operational complexity, higher latency for simple embedded use-cases.
- SQLite advantages: extremely lightweight, zero-configuration, low-latency for single-process access, excellent for mobile and small apps.
- SQLite limitations: limited concurrency (single writer), less suitable for multi-user server workloads, fewer advanced DB features.

Discuss why each design choice arose and its performance/engineering implications (e.g., client-server allows process isolation and shared buffers; embedded reduces IPC but couples DB to app lifecycle).

## 5. Experiments / Observations (what I need from you)

I will perform the analysis and write the final sections once you run a few small commands locally and paste the outputs here. You already have both PostgreSQL and sqlite3 installed — perfect. Follow the instructions below and paste the outputs exactly as requested.

Important: run the Postgres commands in a terminal where `psql` is available. For SQLite, use `sqlite3` or the small Python scripts I provide.

----------

A. PostgreSQL: EXPLAIN ANALYZE (why)
  - Goal: capture planner estimates vs actual rows, timing, and buffer usage so I can explain planner behavior and buffer manager interactions.

  1) Create a small sample dataset (run inside psql):

     -- copy and paste the following into psql
     CREATE DATABASE sd_test;
     \c sd_test
     CREATE TABLE users (id SERIAL PRIMARY KEY, name TEXT);
     CREATE TABLE products (id SERIAL PRIMARY KEY, name TEXT, price INT);
     CREATE TABLE orders (id SERIAL PRIMARY KEY, user_id INT REFERENCES users(id), product_id INT REFERENCES products(id), qty INT);
     INSERT INTO users (name) SELECT 'user_' || g FROM generate_series(1,100) g;
     INSERT INTO products (name, price) SELECT 'product_' || g, (g % 10 + 1) * 10 FROM generate_series(1,100) g;
     -- create some orders
     INSERT INTO orders (user_id, product_id, qty)
       SELECT (random()*99+1)::int, (random()*99+1)::int, (random()*5+1)::int FROM generate_series(1,2000);

  2) Run a representative EXPLAIN ANALYZE (copy the exact line below and run in psql):

     EXPLAIN (ANALYZE, BUFFERS, VERBOSE) 
     SELECT u.id, u.name, SUM(o.qty * p.price) as total_spent
     FROM users u
     JOIN orders o ON o.user_id = u.id
     JOIN products p ON p.id = o.product_id
     GROUP BY u.id, u.name
     HAVING SUM(o.qty * p.price) > 500
     ORDER BY total_spent DESC
     LIMIT 10;

  3) Paste back: the entire EXPLAIN output (all lines from the EXPLAIN command), including the Planning time and Execution time lines.

Optional Postgres WAL listing (only if you want to include WAL observations): run `ls -lh $PGDATA/pg_wal` or find PGDATA and list `pg_wal` directory; paste the `ls -lh` result. If PGDATA is not set, skip.

----------

B. SQLite: journaling modes and concurrency test (why)
  - Goal: show how DELETE (default) vs WAL journaling affect concurrency (reader vs writer blocking).

  1) Check current journaling and synchronous settings (run in shell):
     sqlite3 test_sqlite.db "PRAGMA journal_mode;"
     sqlite3 test_sqlite.db "PRAGMA synchronous;"

     Paste back the two single-line outputs. Example:
     DELETE
     2

  2) Concurrency test (manual steps; run twice: once with DELETE mode and once with WAL mode):

     Manual (recommended if you are new):
     - Terminal A (writer):
         sqlite3 test_sqlite.db
         sqlite> PRAGMA journal_mode = DELETE; -- or WAL for the second run
         sqlite> BEGIN TRANSACTION;
         sqlite> CREATE TABLE IF NOT EXISTS t(a INTEGER);
         sqlite> INSERT INTO t(a) VALUES (1);
         sqlite> -- keep the transaction open: do NOT COMMIT yet --
         -- leave this terminal alone for ~10 seconds then COMMIT

     - Terminal B (reader): while Terminal A's transaction is open, run:
         sqlite3 test_sqlite.db "SELECT count(*) FROM t;" -- observe if it returns immediately or blocks

     Note the behavior (reader blocked or returned immediately) in DELETE and WAL modes.

  Optional automated Python test (save as `sqlite_concurrency_test.py` and run):

  ```python
  # Save as sqlite_concurrency_test.py and run: python3 sqlite_concurrency_test.py
  import sqlite3, time, threading

  DB = 'test_sqlite.db'

  def writer(mode, hold):
      conn = sqlite3.connect(DB, timeout=10)
      conn.execute(f"PRAGMA journal_mode = {mode};")
      cur = conn.cursor()
      cur.execute('CREATE TABLE IF NOT EXISTS t(a INT);')
      conn.commit()
      cur.execute('BEGIN TRANSACTION;')
      cur.execute('INSERT INTO t(a) VALUES (1);')
      time.sleep(hold)
      conn.commit()
      conn.close()

  def reader():
      conn = sqlite3.connect(DB, timeout=10)
      cur = conn.cursor()
      try:
          t0 = time.time()
          cur.execute('SELECT count(*) FROM t;')
          r = cur.fetchone()
          took = time.time() - t0
          print('reader returned, rows=', r[0], 'time=', took)
      except Exception as e:
          print('reader exception:', e)
      conn.close()

  # Example run: writer in background holds lock for 6 seconds, reader invoked after 1s
  th = threading.Thread(target=writer, args=('DELETE', 6))
  th.start()
  time.sleep(1)
  reader()
  th.join()

  ```

  Paste back: PRAGMA outputs and the observation (reader blocked? how long) for both DELETE and WAL modes. If you run the Python script, paste its printed output for both modes.

----------

What to paste here in the chat (minimum):
- Full EXPLAIN (ANALYZE, BUFFERS, VERBOSE) output from Postgres.
- PRAGMA journal_mode and PRAGMA synchronous outputs from sqlite3.
- A one-line observation for SQLite concurrency in DELETE mode and WAL mode (e.g., "DELETE: reader blocked ~5s; WAL: reader returned immediately").

If you paste those, I will:
- Analyze the EXPLAIN output and explain planner estimates vs actuals, buffer usage, and how pages flow through shared buffers.
- Explain the concurrency differences observed in SQLite and connect them to journaling internals.
- Fill the Experiments/Observations and Key Learnings sections and produce diagrams and references.
- Prepare the final commit and give you exact git commands to push your branch and create the PR with the required title format.

## 6. Key learnings (example list — I will expand after experiments)
- Client-server vs embedded trade-offs.
- How MVCC (Postgres) vs file-journal/WAL (SQLite) lead to different concurrency profiles.
- How buffer caches and WAL interact to provide durability and affect performance.

## Experiments / Observations — PostgreSQL (your run)

You provided the output of EXPLAIN (ANALYZE, BUFFERS, VERBOSE) for the sample query on the `sd_test` database. I captured the key observations and an interpretation below.

Plan summary (high-level)
- The planner chose: Limit -> Sort (top-N heapsort) -> HashAggregate -> Hash Join -> Seq Scan on `orders`.
- Execution used hash joins between `orders` and the small `users` and `products` tables, then grouped by user and applied the HAVING filter, finally sorting and limiting to top-10 results.

Important metrics from your run
- Dataset sizes: users=100, products=100, orders=2000 (these are reflected in the Seq Scan counts).
- Buffers: "shared hit=13" indicates data was served from the shared buffer cache (no significant physical reads for this run).
- Timing: Planning time ~0.243 ms, Execution time ~1.442 ms — very fast because the dataset is small and fits in memory.
- Planner estimates vs actuals: planner estimated ~33 rows at the aggregate stage but actual rows were 100 — an underestimate of grouping cardinality.

What this tells us
- Why hash joins: with two small dimension tables (100 rows each) and a larger `orders` table (2k rows), hash joins are efficient — build a hash on the small table(s) and probe with the larger input.
- Seq scan on `orders`: no useful index exists on `orders.user_id` or `orders.product_id` for this workload. For 2k rows a seq scan is cheaper than indexed access.
- Planner estimate mismatch: under-estimating the number of groups suggests the statistics used by the planner are coarse or missing for the grouping columns; running `ANALYZE` might improve estimates.
- Buffer hits: shared buffer hits show the query was cached; to analyze disk IO impact you'd want cold-cache experiments (restart Postgres or evict caches) to observe physical reads.

Suggested quick follow-ups you can run (I can analyze results)
1. Run `ANALYZE;` in `sd_test` and re-run the EXPLAIN to see if estimates improve.
2. Create indexes: `CREATE INDEX ON orders(user_id); CREATE INDEX ON orders(product_id);` and compare EXPLAIN outputs to see when the planner picks index scans vs hash joins.
3. Simulate cold-cache: restart the Postgres server and re-run the EXPLAIN capturing `BUFFERS` to see physical I/O values.

I will integrate these insights into the final README content and diagrams once you confirm the text and indicate whether you want the entire raw EXPLAIN output included verbatim or summarized.

## Experiments / Observations — SQLite (your run)

I ran the concurrency tests you executed and added a small forced-lock test to demonstrate the difference between DELETE (rollback journal) and WAL modes. Below are the exact outputs you produced and the interpretation.

Your forced-test output (produced by running `sqlite_block_test_forced.py`):

--- TEST MODE DELETE ---
[writer-DELETE] started, holding tx for 6s
[writer-DELETE] committed
[reader] returned rows=1 time=5.215s
--- TEST MODE WAL ---
[writer-WAL] started, holding tx for 6s
[reader] returned rows=0 time=0.000s
[writer-WAL] committed

What happened (interpretation)
- DELETE (rollback journal) test: the writer used `BEGIN EXCLUSIVE` which acquires an exclusive lock early; the reader blocked while the writer held the exclusive lock and therefore returned only after the writer committed. The reader observed a delay ~5.2s which matches the writer hold of 6s (minus the test timing offsets). This demonstrates that DELETE mode can block readers during an exclusive write.
- WAL test: the writer was using WAL mode; the reader returned immediately (time 0.000s) and saw the pre-existing row count (0) because WAL allows concurrent readers while a writer appends to the WAL. The writer later committed and the database state changed, but concurrent readers are not blocked by WAL writers.

Why this matters (architectural link)
- DELETE mode implements atomicity via a rollback journal and file locking that can block concurrent reads while a writer holds an exclusive lock. This is simple and robust for single-writer workloads but limits concurrency for mixed read-write loads.
- WAL mode decouples writers (append-only to WAL) from readers (reading original database file), allowing multiple readers concurrently with a single writer. This increases read concurrency and is why SQLite's WAL mode is preferred for read-heavy or multi-threaded read scenarios on single-host applications.

Suggested wording for README observations
- Include both the raw outputs (as shown above) and a short explanation linking locks, journaling mode, and observed behavior. The experiments demonstrate why SQLite is a good fit for mobile/single-process workloads (low overhead) but why WAL mode is needed when concurrent readers are required.

Next steps I can do for you
1. Finalize formatting of the README and include small diagrams (SVG) illustrating client-server vs embedded & WAL vs DELETE flow.
2. Prepare the commit on branch `feature/SCALER_23bcs10071` and give you exact git commands to push and open the PR titled `SCALER_23bcs10071`.
3. Optionally expand the experiments with cold-cache tests or index comparisons for Postgres if you want extra marks.

## 7. Next steps for you (quick checklist)
1) Run the Postgres EXPLAIN commands above and paste output here.
2) Run the SQLite PRAGMA and concurrency test and paste outputs/observations.
3) Tell me the roll number string to use for the PR title (format: SCALER_<ROLL_NUMBER>), and confirm your fork and that you want me to prepare the PR steps for your GitHub username `Princekumar7999`.

When you paste the outputs I will finish the README content, create the branch and give you the exact git push + PR commands.

---

References and further reading (I will expand after experiments): PostgreSQL docs (MVCC, buffer manager, WAL), SQLite docs (WAL, journaling modes), academic papers on LRU buffers and transaction processing.

---

If you want I can also generate a short set of diagrams (PNG / SVG files) and add them to the directory before the PR — tell me if you prefer SVG or PNG.
