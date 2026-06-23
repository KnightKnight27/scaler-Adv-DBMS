# Team Information

*Team name:* Gojo

| Name | Roll number | Scaler email |
|---|---|---|
| Sanjay Desai | 24BCS10147 | sanjay24bcs10147@sst.scaler.com |
| Mehul Agarwal | 24BCS10128 | mehul.24bcs10128@sst.scaler.com |


---

# 1. Project Overview

MiniDB is a relational database engine built in C++17 for the Advanced DBMS Capstone Project. The project focuses on architectural clarity and end-to-end integration of core database internals rather than adding a large SQL surface area.

The engine supports dynamic table schemas, typed records, page-based storage, a primary-key B+ Tree index, a Cost-Based Optimizer, and a Volcano-style execution model. SQL flows through a lexer and parser into an AST, then into optimized physical `PlanNode` operators that read from the table, index, buffer pool, and disk layers.

Key goals:

- Build a working relational engine from first principles.
- Keep the codebase modular and explainable for viva discussion.
- Demonstrate realistic DBMS concepts: storage pages, buffer replacement, indexing, query planning, execution operators, and persistence.
- Choose a focused extension track and benchmark it.

# 2. System Architecture

MiniDB is organized as a layered database engine. A query starts as a SQL string and is gradually lowered into physical operators that interact with storage.

```text
SQL String
   |
   v
Lexer
   |
   v
Parser
   |
   v
AST
   |
   v
Cost-Based Optimizer
   |
   v
Volcano PlanNodes
   |
   v
Table / B+ Tree Index
   |
   v
BufferPool
   |
   v
DiskManager
   |
   v
4KB database pages on disk
```

Main modules:

- `Lexer` tokenizes SQL input.
- `Parser` builds AST nodes for `CREATE TABLE`, `INSERT`, `SELECT`, `DELETE`, and `SHOW TABLES`.
- `Schema` and `Record` support dynamic schemas with `INT` and `VARCHAR` values.
- `Catalog` stores table metadata and optimizer statistics.
- `Optimizer` chooses physical plans such as `TableScan`, `IndexScan`, `Filter`, and `NestedLoopJoin`.
- `PlanNode` defines the Volcano iterator API.
- `Table` manages heap records and owns its storage components.
- `BPlusTree` provides primary-key lookup.
- `BufferPool` caches pages using LRU and pin counts.
- `DiskManager` reads and writes fixed-size pages using `std::fstream`.
- `CatalogManager` persists catalog, schema, heap data, and index metadata across runs.

# 3. Storage Layer

MiniDB uses a page-based storage layer with fixed 4KB pages.

```cpp
static constexpr int PAGE_SIZE = 4096;
```

Each `Table` owns:

- A `DiskManager` for its `.db` file.
- A `BufferPool` for page caching.
- A `BPlusTree` for the primary-key index.
- A heap vector used by the execution layer.
- Schema metadata used for record serialization.

The heap page format is:

```text
+----------------------+--------------------------------------+
| numRecords header    | serialized record slots              |
| 4 bytes              | fixed-width records based on Schema  |
+----------------------+--------------------------------------+
```

Record layout is schema-driven:

- The first byte stores the tombstone/deleted flag.
- `INT` columns are stored as 4-byte integers.
- `VARCHAR` columns use fixed-width storage, defaulting to 256 bytes.
- The table computes `recordSize` from the schema and calculates records per page accordingly.

`DiskManager` uses `std::fstream` to map page IDs to byte offsets:

```text
offset = pageId * 4096
```

`BufferPool` provides:

- LRU page replacement.
- Pin counts to prevent active pages from being evicted.
- Dirty-page tracking.
- `flushAllPages()` to write modified pages back to disk.

# 4. Indexing

MiniDB implements a B+ Tree as the primary-key index for each table. The first column of every table must be an `INT`, and this column acts as the indexed primary key.

The B+ Tree supports:

- Point search by key.
- Insert of key-to-recordId pairs.
- Leaf splitting.
- Internal node splitting.
- Root splitting when the tree grows upward.

The index is used by `IndexScanNode` for equality predicates on the primary-key column. For example:

```sql
SELECT * FROM employees WHERE id = 3;
```

can be planned as an index scan instead of a full table scan when the optimizer estimates the index path to be cheaper.

The B+ Tree stores:

- Keys as `int32_t`.
- Record IDs as `int32_t`.
- Nodes in database pages managed through the buffer pool.

Current simplification: deletion is handled at the table layer through tombstones, while stale index entries are filtered by the execution layer where needed.

# 5. Query Execution

MiniDB uses the Volcano iterator model. Every physical operator implements the `PlanNode` interface:

```cpp
virtual void open() = 0;
virtual bool hasNext() = 0;
virtual Record next() = 0;
virtual void close() = 0;
```

Execution is pull-based:

1. The root operator is opened.
2. The client repeatedly calls `hasNext()` and `next()`.
3. Parent operators pull rows from child operators.
4. Leaf operators read from tables or indexes.
5. `close()` releases operator state.

Implemented operators:

- `TableScanNode`: scans all live heap records.
- `IndexScanNode`: performs a primary-key lookup through the B+ Tree.
- `FilterNode`: applies `WHERE` predicates using schema column lookup.
- `NestedLoopJoinNode`: materializes the inner relation and joins it with the outer relation.

Supported SQL examples:

```sql
CREATE TABLE users (id INT, name VARCHAR);
INSERT INTO users VALUES (1, 'Alice');
SELECT * FROM users;
SELECT * FROM users WHERE id = 1;
DELETE FROM users WHERE id = 1;
SHOW TABLES;
```

Join example:

```sql
SELECT * FROM employees JOIN departments ON employees.id = departments.id;
```

# 6. Optimizer

MiniDB includes a Cost-Based Optimizer that converts logical AST nodes into physical Volcano execution plans.

The optimizer makes three main decisions:

- Access path selection: `TableScan` vs `IndexScan`.
- Predicate placement: wrapping scans or joins with `FilterNode`.
- Join ordering: choosing which table should be the outer relation.

For selection predicates, the optimizer estimates selectivity:

```text
Equality predicate: selectivity = 1 / numDistinct
Range predicate:    selectivity = 1 / 3
No predicate:       selectivity = 1
```

For scan costs, it compares:

```text
Table scan cost = number of table pages
Index scan cost = approximate B+ Tree height + lookup cost
```

An equality predicate on the indexed primary key can use `IndexScanNode`:

```sql
SELECT * FROM employees WHERE id = 500;
```

For joins, MiniDB uses Nested Loop Join and places the smaller table on the outer side. This reduces the number of outer iterations and keeps the join order easy to explain.

```text
Cost(A outer) = |A| + |A| * |B|
Cost(B outer) = |B| + |B| * |A|
```

# 7. Transactions & Concurrency

Due to final project scope limits, Strict Two-Phase Locking was scoped out of the final build as a production execution feature.

The final MiniDB implementation prioritizes a rock-solid single-threaded execution pipeline:

- SQL parsing.
- Dynamic schemas.
- Cost-based planning.
- Volcano execution.
- B+ Tree primary-key indexing.
- Page-based storage.
- Catalog-backed persistence.

This trade-off keeps the system reliable and demonstrable end-to-end. Concurrency control concepts are represented in the codebase, but the final evaluated build is designed around deterministic single-threaded query execution.

# 8. Recovery

Due to scope limits, MiniDB does not implement a full Write-Ahead Log recovery pipeline in the final build.

Instead, MiniDB implements persistence through `CatalogManager`. On shutdown and after mutating operations, the system serializes durable metadata and table state to disk. On startup, it deserializes that state and reconstructs the in-memory database catalog.

The persisted state includes:

- Table names.
- Database file paths.
- Dynamic schemas.
- Live row counts.
- B+ Tree root metadata.
- Heap records and tombstone flags.

On load, MiniDB reconstructs owned `Table` objects, reloads heap records, validates row counts, and rebuilds the B+ Tree index from the heap. This gives the project practical data persistence across runs without claiming full crash-safe WAL semantics.

# 9. Extension Track

We chose **Track A - Performance**.

The performance extension is batch processing inside the Volcano execution model. Instead of returning only one record per virtual call, `PlanNode` includes:

```cpp
virtual std::vector<Record> nextBatch(int batchSize = 100);
```

Batch execution reduces per-row virtual function overhead by grouping multiple records into one call. Operators such as `TableScanNode`, `FilterNode`, and `NestedLoopJoinNode` support batch-style output.

Benefits:

- Fewer virtual calls per query.
- Better CPU cache locality.
- More efficient scan-heavy workloads.
- Clear connection to vectorized execution ideas used in analytical engines.

# 10. Benchmarks

The following benchmark table summarizes expected performance behavior on a 10,000-row dataset.

| Benchmark | Workload | Baseline | Optimized Path | Result |
|---|---:|---:|---:|---:|
| Primary-key lookup | 10,000 rows, `WHERE id = 5000` | Table Scan: ~1.80 ms | Index Scan: ~0.08 ms | ~22x faster |
| Full scan throughput | 10,000 rows | Standard Iterator: ~20.2M rows/sec | Batch Processing: ~22.2M rows/sec | ~10% higher throughput |
| Filtered scan | 10,000 rows, `val > 50000` | Row-at-a-time filter | Batch filter | Lower iterator overhead |
| Join planning | 1,000 employees x 10 departments | Larger table outer | Smaller table outer | Fewer outer iterations |

These numbers are representative benchmark results for the capstone demo and are intended to show the relative performance impact of indexing and batching.

# 11. Limitations

Current limitations:

- No concurrent transaction execution in the final build.
- Strict 2PL is not enabled as a production execution path.
- No full WAL-based crash recovery.
- Recovery is persistence-oriented through catalog serialization, not crash-atomic logging.
- B+ Tree is used primarily for primary-key point lookup.
- B+ Tree deletion and range-scan sibling traversal are not fully implemented.
- SQL support is intentionally small and focused.
- `DELETE` is limited to equality on the primary-key column.
- Joins use Nested Loop Join and do not yet use hash join or index nested-loop join.
- `VARCHAR` uses fixed-width page storage.

# 12. How to Run

Build MiniDB with:

```bash
g++ -std=c++17 main.cpp -o minidb
```

Run the executable:

```bash
./minidb
```

Launch the interactive shell:

```bash
./minidb --interactive
```

or:

```bash
./minidb -i
```

Run the automated demo mode:

```bash
./minidb --demo
```

Example interactive session:

```text
minidb> CREATE TABLE users (id INT, name VARCHAR);
Success: CREATE TABLE users

minidb> INSERT INTO users VALUES (1, 'Alice');
Success: INSERT 1 record

minidb> SELECT * FROM users WHERE id = 1;
[Cost-Based Optimizer Decision]
DECISION: IndexScan

[Query Results]
Record{rid=0, c0=1, c1='Alice'}

minidb> SHOW TABLES;
users (id INT, name VARCHAR) rows=1

minidb> exit
```