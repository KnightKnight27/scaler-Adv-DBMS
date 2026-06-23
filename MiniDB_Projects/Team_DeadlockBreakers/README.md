# MiniDB - Advanced DBMS Capstone

MiniDB is a small relational database written in C++17. It implements the
required storage, indexing, SQL, optimization, transaction, concurrency, and
recovery concepts. The chosen extension is **Track A: Batch Execution**.

## 1. Project and team

**Team name:** `Deadlock Breakers`

| Name | Roll number | Scaler email |
|---|---|---|
| Apurv Dugar | 24BCS10107 | apurv.24bcs10107@sst.scaler.com |
| Akshat Sipany | 24BCS10059 | akshat.24bcs10059@sst.scaler.com |
| Harshita Hirawat | 24BCS10044 | harshita.24bcs10044@sst.scaler.com |

The goal is correctness and clear database internals, not a large SQL feature
set. MiniDB supports `CREATE TABLE`, `INSERT`, `SELECT`, `WHERE`, `JOIN`, and
`DELETE`.

## 2. Architecture

```text
SQL -> Parser -> Optimizer -> Execution operators
                                |        |
                             B+ tree  Heap file
                                         |
                                  Buffer pool -> Disk

Executor -> Locks -> Transaction -> WAL -> Commit / Recovery
```

Each module has one responsibility and the implementation is split into short
files under `src/`.

For a query, the parser first creates a statement object. The optimizer chooses
a scan and join order, the executor runs the operator tree, and the storage
layer reads records through the buffer pool. For a write, the executor also
acquires a lock and adds a WAL record before the transaction commits.

## 3. Storage layer

- Data is stored in fixed 4 KB slotted pages.
- The 8-byte page header stores record count, free position, and next-page ID.
- Each slot stores a record's offset, length, and valid/deleted flag.
- Heap files link multiple pages and allocate a new page when the last is full.
- The buffer pool uses pin counts, dirty flags, and LRU replacement.
- Existing heap files can be reopened after restarting MiniDB.

Rows use a simple pipe-separated text format so page contents are easy to
understand and demonstrate.

## 4. B+ tree index

The first integer column is treated as the primary key. The in-memory B+ tree
supports search, insert, delete, and range search. Leaf nodes are linked for
range queries. Search, insert, and delete are normally `O(log n)`. The index is
rebuilt by scanning the heap file after restart, keeping disk storage simple.

## 5. SQL execution

The parser creates a small statement object. The executor builds sequential
scan, index scan, filter, projection, or nested-loop join operators.
Operators follow `Open -> Next -> Close`; `Next` returns one batch of rows.

```sql
INSERT INTO students VALUES (1, 'Alice', 22, 3.8)
SELECT name FROM students WHERE id = 1
SELECT * FROM students JOIN courses ON students.id = courses.student_id
DELETE FROM students WHERE id = 1
```

Predicates support `=`, `!=`, `<`, `<=`, `>`, `>=`, `AND`, and `OR`.

## 6. Optimizer

The optimizer deliberately uses simple estimates:

- primary-key equality: `1 / number of rows`;
- range condition: about one-third of rows;
- use an index for selective primary-key conditions;
- place the smaller table first in a two-table nested-loop join.

The demo prints whether a sequential scan or index scan was selected.

## 7. Transactions and concurrency

MiniDB uses strict two-phase locking with table-level shared and exclusive
locks. Locks are released only at commit or abort. A one-second timeout handles
deadlocks simply. The demo creates a circular wait between two transactions so
both timeout paths can be seen.

Insert and delete operations are kept in a transaction's pending-write list.
Commit writes the commit log record, applies the pending writes, flushes pages,
and releases locks. Abort discards the list and releases locks. This keeps undo
logic small and understandable.

## 8. WAL and crash recovery

The WAL contains `BEGIN`, `INSERT`, `DELETE`, `COMMIT`, and `ABORT` records.
Recovery reads the log, identifies committed transactions, and repeats only
their inserts and deletes. Aborted or incomplete transactions are ignored.

Recovery therefore has two simple phases: find committed transaction IDs, then
replay their changes in log order. The demo recovers into fresh files and
prints the number of restored rows.

## 9. Extension Track A - batch execution

Normal operators pass up to 512 rows at a time. Filters reuse the same batch
buffer. Row-at-a-time operators are retained for comparison.

For 10K-200K rows, batch mode was about `1.01x-1.09x` faster in the latest
run. It uses fewer operator calls while keeping the implementation simple.
Detailed measurements are in [benchmarks/README.md](benchmarks/README.md).

## 10. Benchmarks and tests

The benchmark runs a three-filter in-memory query 50 times for 10K-200K rows.
Disk loading is excluded so it compares the execution models directly. It
reports wall time, CPU time, and approximate operator-buffer memory, and also
verifies that both modes return the same row count. Tests cover:

- multi-page storage and restart;
- B+ tree search, range search, and delete;
- SQL, joins, commit, and abort;
- lock timeout/deadlock behavior;
- recovery of committed changes only.

## 11. Limitations

- Table schemas are registered by the application, not stored on disk.
- The B+ tree is rebuilt after restart.
- A transaction cannot read its own insert until that transaction commits.
- Locks are table-level and query execution is single-threaded.
- No `UPDATE`, aggregates, sorting, grouping, or secondary index.
- B+ tree delete does not merge underfull nodes.
- The SQL and WAL text formats do not support every quoted special character.

## 12. Build and run

Requirements: CMake 3.16+ and a C++17 compiler. No external libraries are used.

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
./build/Release/minidb
./build/Release/benchmark
```

Choose `demo` to show SQL, index use, commit/abort, deadlock timeout, and crash
recovery. Choose `manual` to reopen the existing database and enter SQL.
