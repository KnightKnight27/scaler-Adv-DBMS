# MiniDB — Advanced DBMS Capstone Project

## Team Members
* **THRISHAL DOMA** - thrishal.24bcs10097@sst.scaler.com (Team Lead / Architecture)
* **ABHIROOP SISTU** - abhiroop.24bcs10287@sst.scaler.com (Transactions & MVCC)
* **NANDA KISHORE** - nanda.24bcs10224@sst.scaler.com (Storage & Indexing)
* **CHALLA SAHADHEEP** - challa.24bcs10276@sst.scaler.com (Execution & Optimizer)

---

## 1. Project Overview
**Problem Statement:** Modern applications require robust, reliable, and highly concurrent database systems. Building a database engine from scratch requires integrating complex components like disk management, query optimization, transaction isolation, and crash recovery.
**Goals:** To design, implement, and demonstrate a functioning relational database system (MiniDB) that incorporates the foundational concepts studied across all Advanced DBMS lab modules.
**Chosen Extension Track:** **Track B — Concurrency (MVCC)**. We replaced standard 2PL read locks with Multi-Version Concurrency Control to improve read throughput.

## 2. System Architecture
MiniDB follows a layered architecture where each module operates on top of the abstractions provided by the layer below it.
**Data Flow:** SQL Query -> Parser -> AST -> Optimizer -> Physical Plan -> Executor -> Buffer Pool -> Disk Manager -> OS File System.
**Major Modules:**
- `storage`: Manages pages, heap files, and the buffer pool.
- `index`: Manages B+ Tree structures.
- `parser`: Tokenizes and parses SQL to AST.
- `optimizer`: Generates cost-estimated execution plans.
- `execution`: Volcano-model operators.
- `transaction`: Manages 2PL locks, deadlocks, and MVCC versions.
- `recovery`: Manages Write-Ahead Logging and ARIES recovery.

## 3. Storage Layer
- **Page Format:** We implemented a Slotted Page layout. Each page has a header, a slot directory growing forwards, and raw tuple data growing backwards. This allows efficient in-page record deletion and compaction.
- **Heap Files:** Manages a collection of pages for a table. It tracks free space to optimize record insertion without scanning every page.
- **Buffer Pool:** Caches disk pages in memory to minimize I/O. Uses an LRU (Least Recently Used) replacement policy and tracks "dirty" pages that must be flushed to disk before eviction.

## 4. Indexing
- **B+ Tree Design:** We built a disk-backed B+ tree implementation used for primary and secondary indexes.
- **Node Structure:** Nodes are stored as pages via the Buffer Pool. Internal nodes store keys and page pointers, while leaf nodes store keys and record IDs (RIDs).
- **Search Path:** Searching traverses from the root to the leaf. Leaf nodes maintain a doubly-linked list to support efficient range scans (`>`,`<`,`BETWEEN`).

## 5. Query Execution
- **Parser:** We built a custom Lexer and Recursive Descent Parser that converts raw SQL strings into an Abstract Syntax Tree (AST).
- **Query Plan Generation:** The logical AST is converted into a Physical Execution Plan, mapping operations to physical operators.
- **Operator Execution:** We use the Volcano Iterator model (`open`, `next`, `close`). Operators like `SeqScan`, `IndexScan`, `NestedLoopJoin`, `Filter`, and `Project` pull tuples from their child operators to execute queries pipelined.

## 6. Optimizer
- **Cost Estimation:** Estimates I/O and CPU costs for sequential scans vs. index scans.
- **Selectivity Estimation:** Uses table statistics (row count, min/max values, distinct values) to estimate the fraction of rows returned by predicates (e.g., equality, range, AND/OR).
- **Join Ordering:** Evaluates join permutations to select the lowest-cost execution plan based on table cardinalities.

## 7. Transactions & Concurrency
- **Locking Strategy:** Strict Two-Phase Locking (2PL) is used for writes. Writers acquire an `EXCLUSIVE` row-level lock and hold it until `COMMIT` or `ROLLBACK`.
- **Deadlock Handling:** Deadlocks are detected using a Wait-For Graph. If a cycle is detected during lock acquisition, a `DeadlockError` is raised and the transaction is aborted.
- **Isolation Guarantees:** Readers use MVCC (Snapshot Isolation), ensuring they see a consistent database view without blocking. Writers use serializable-level exclusive locks to prevent lost updates.

## 8. Recovery
- **WAL Design:** All database modifications are logged sequentially to `_wal.log` using Force-Log-at-Commit to ensure durability.
- **Log Records:** Log records track `BEGIN`, `INSERT`, `UPDATE`, `DELETE`, `COMMIT`, and `ABORT` operations along with a Log Sequence Number (LSN).
- **Crash Recovery:** Uses ARIES-style recovery. On startup, the `RecoveryManager` performs an Analysis phase (to find active txns), a Redo phase (replaying committed changes), and an Undo phase (rolling back uncommitted changes).

## 9. Extension Track B (MVCC)
- **Motivation:** 2PL degrades performance under high contention because readers block writers. MVCC solves this: readers never block writers.
- **Design:** Instead of overwriting rows, MiniDB inserts a new version. Each version tracks `xmin` (creating txn) and `xmax` (deleting txn).
- **Results:** Benchmarks show that under contention with multiple read and write threads, our MVCC implementation processes transactions without deadlock timeouts, whereas a pure 2PL approach would bottleneck or deadlock.

## 10. Benchmarks
- **Experimental Setup:** Benchmarks run via `benchmarks/benchmark_runner.py` using `threading` and `matplotlib` on simulated tables with 10,000+ rows.
- **Results & Analysis:**
  - *Insert Throughput:* Throughput scales well with batch sizes up to 5,000 rows per transaction.
  - *Index vs Seq Scan:* B+ Tree index lookups are orders of magnitude faster than sequential scans for point queries (e.g., 0.05s vs 2.1s).
  - *Concurrency:* MVCC sustains high parallel throughput (e.g., thousands of reads/writes per second) without blocking.
- See detailed results and plots in [benchmark_results.md](benchmarks/benchmark_results.md).

## 11. Limitations
- **Missing Features:** No support for advanced SQL aggregations (`GROUP BY`, `HAVING`) or subqueries. String matching (`LIKE`) is basic.
- **Scalability Limits:** The current Lock Manager uses a global mutex to manage its internal tables, which may become a bottleneck at extremely high core counts.
- **Future Improvements:** Implement a columnar storage format (Track A) or distributed replication (Track D). Add a background thread for MVCC Vacuuming (garbage collection) instead of synchronous collection.

## 12. How to Run
### Build Steps & Dependencies
Requires Python 3.9+. Install the required libraries:
```bash
pip install -r requirements.txt
```

### Example Commands
Start the interactive SQL shell:
```bash
python run.py
```
Inside the REPL:
```sql
minidb> CREATE TABLE employees (id INTEGER PRIMARY KEY, name VARCHAR, salary FLOAT);
minidb> CREATE INDEX idx_salary ON employees (salary);
minidb> INSERT INTO employees (id, name, salary) VALUES (1, 'Alice', 95000);
minidb> INSERT INTO employees (id, name, salary) VALUES (2, 'Bob', 80000);
minidb> SELECT * FROM employees WHERE salary > 90000;
minidb> .explain SELECT * FROM employees WHERE salary > 90000;
minidb> .quit
```

### Tests & Benchmarks
```bash
python -m pytest tests/ -v
python benchmarks/benchmark_runner.py
```
