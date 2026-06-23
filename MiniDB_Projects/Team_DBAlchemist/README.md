# MiniDB — Advanced DBMS Capstone Project 

### Team Name: DBAlchemist

## Team Members

| Name | Email | Roll Number |
|------|-------|-------------|
| Loukik Thatte | loukik.23bcs10059@sst.scaler.com | 23BCS10059 |
| Vivek Anand Singh | vivek.23bcs10172@sst.scaler.com | 23BCS10172 |
| Indrajeet | indrajeet.23bcs10199@sst.scaler.com | 23BCS10199 |
| Sakshi | sakshi.24bcs10034@sst.scaler.com | 24BCS10034 |

**Team Name:** DBAlchemist
**Extension Track:** Track B — MVCC (Multi-Version Concurrency Control)

---

## 1. Project Overview

### Problem Statement

Build a functioning relational database engine from scratch — implementing storage, indexing, query processing, transaction management, and crash recovery — without using any external database libraries.

### Goals

- Implement page-based heap file storage with a buffer pool
- Build a B+ tree index for primary key lookups
- Support SQL queries: `SELECT` (with `WHERE`, `JOIN`), `INSERT`, `DELETE`, `CREATE TABLE`
- Build a cost-based optimizer that chooses between sequential scan and index scan
- Implement MVCC (Track B) replacing Two-Phase Locking for concurrency control
- Implement Write-Ahead Logging for crash recovery

### Chosen Extension Track: Track B — MVCC

We replaced Two-Phase Locking with Multi-Version Concurrency Control. Under MVCC, readers never acquire locks — they read from a consistent snapshot taken at transaction `BEGIN`. Writers create new row versions instead of overwriting in place. This eliminates reader-writer blocking entirely.

---

## 2. System Architecture

### ASCII Architecture Diagram

```
┌─────────────────────────────────────────────────────────┐
│                      MiniDB API                         │
│                   db.execute(sql, txn)                  │
└────────────────────────┬────────────────────────────────┘
                         │
              ┌──────────▼──────────┐
              │       Planner       │
              │  parse → DDL/DML    │
              └──────────┬──────────┘
                         │
         ┌───────────────▼───────────────┐
         │           Optimizer           │
         │  selectivity · join order ·   │
         │  seq scan vs index scan       │
         └───────────────┬───────────────┘
                         │
         ┌───────────────▼───────────────┐
         │           Executor            │
         │  SeqScan · IndexScan · Filter │
         │  NestedLoopJoin · Projection  │
         └──────┬──────────────┬─────────┘
                │              │
   ┌────────────▼───┐   ┌──────▼────────┐
   │  Buffer Pool   │   │   B+ Tree     │
   │ (Clock Sweep)  │   │   Index       │
   └────────┬───────┘   └───────────────┘
            │
   ┌────────▼───────┐
   │   Heap File    │
   │  (disk pages)  │
   └────────────────┘

   ┌─────────────────────────────────────┐
   │         Transaction Layer           │
   │  TransactionManager · MVCC ·        │
   │  Snapshot · xmin/xmax visibility    │
   └─────────────────────────────────────┘

   ┌─────────────────────────────────────┐
   │      Write-Ahead Log (WAL)          │
   │  Append-only · fsync · recovery     │
   └─────────────────────────────────────┘

   ┌─────────────────────────────────────┐
   │            Catalog                  │
   │  Table schemas persisted as JSON    │
   └─────────────────────────────────────┘
```

### Major Modules

| Module | File(s) | Responsibility |
|--------|---------|----------------|
| Storage | `storage/page.py`, `heap_file.py`, `buffer_pool.py` | Page layout, disk I/O, Clock Sweep buffer pool |
| Index | `index/bplus_tree.py` | B+ tree: insert, search, delete, range scan |
| Catalog | `catalog/catalog.py` | Table schemas, column types, persistence |
| Transactions | `txn/transaction.py`, `txn/mvcc.py` | Txn IDs, snapshots, MVCC visibility |
| Recovery | `recovery/wal.py` | Write-ahead log, crash recovery |
| SQL | `sql/parser.py`, `sql/executor.py`, `sql/planner.py` | Parse, plan, execute SQL |
| Optimizer | `optimizer/optimizer.py` | Cost model, selectivity, join ordering |
| DB Facade | `db.py` | Wires all components, public API |

### Data Flow: INSERT

```
execute("INSERT INTO t (id, v) VALUES (1, 'a')", txn)
  → Parser produces InsertStmt
  → Executor.exec_insert():
      1. Cast values via schema
      2. mvcc.new_row(data, txn.txid)     → adds _xmin, _xmax=None
      3. serialize_row(row)               → JSON bytes
      4. BufferPool.fetch_page() or new_page()
      5. Page.insert_record(bytes)        → returns slot_id
      6. BPlusTree.insert(pk_value, (page_id, slot_id))
      7. WAL.log_insert(txid, table, page_id, slot_id, row)
      8. txn.add_undo(...)               → for rollback
```

### Data Flow: SELECT

```
execute("SELECT * FROM t WHERE id = 5")
  → Parser produces SelectStmt
  → Optimizer.optimize():
      1. Check if WHERE col is indexed
      2. Compare index_scan_cost vs seq_scan_cost
      3. Choose scan type, determine join order
  → Executor builds operator tree:
      IndexScan / SeqScan → Filter → Projection
  → Operator.scan() yields (row, page_id, slot_id)
  → MVCC.is_visible_to_txn() filters each row
  → Return list of clean dicts (system fields stripped)
```

---

## 3. Storage Layer

### Page Format

Every page is exactly **4096 bytes**. The layout uses a slotted-page design:

```
Byte offset 0:
┌──────────────────────────────────────────────────────┐
│ HEADER (8 bytes)                                     │
│   page_id   : 4 bytes  (unsigned int, big-endian)    │
│   num_slots : 2 bytes  (number of slot entries)      │
│   free_end  : 2 bytes  (offset where free space ends)│
├──────────────────────────────────────────────────────┤
│ SLOT DIRECTORY (grows forward →)                     │
│   slot 0: offset(2B) + length(2B)                   │
│   slot 1: offset(2B) + length(2B)                   │
│   ...                                                │
├──────────────────────────────────────────────────────┤
│                  FREE SPACE                          │
├──────────────────────────────────────────────────────┤
│ RECORDS (grow backward ←)                            │
│   record N  (at high offsets)                        │
│   record 1                                           │
│   record 0  (closest to end of page)                 │
└──────────────────────────────────────────────────────┘
Byte offset 4095
```

**Key constants:**
- `PAGE_SIZE = 4096`
- `HEADER_SIZE = 8`
- `SLOT_SIZE = 4` (offset 2B + length 2B)
- `TOMBSTONE = 0xFFFF` — length value marking a deleted slot

**Insert:** record placed at `free_end - len(record)`, new slot entry written at `HEADER_SIZE + num_slots * SLOT_SIZE`. Header updated atomically.

**Delete:** slot length set to `TOMBSTONE`. Space is not reclaimed (no compaction).

**Free space check:** `free_end - (HEADER_SIZE + num_slots * SLOT_SIZE) >= SLOT_SIZE + rec_len`

Records are stored as JSON-encoded bytes. This keeps row access simple and debuggable while preserving the correct page-based storage semantics.

### Heap File

`HeapFile` manages a binary file of back-to-back 4096-byte pages on disk.

- `read_page(page_id)` — seeks to `page_id * PAGE_SIZE`, reads 4096 bytes
- `write_page(page_id, data)` — seeks and overwrites
- `allocate_page()` — appends 4096 zero bytes, returns new `page_id`
- `num_pages` — derived from `file_size // PAGE_SIZE`

One heap file per table: `data/<table>.heap`

### Buffer Pool

`BufferPool` uses **Clock Sweep** replacement — the same policy used by PostgreSQL — rather than plain LRU.

**Why not LRU?**  
The OS page cache already applies LRU to file blocks. A second LRU layer in the database adds no value. Worse, full table scans (SeqScan) cause *sequential flooding*: scanning all N pages brings them all into cache, evicting hot pages (B+ tree root, frequently-joined tables) that were actually useful. LRU has no way to distinguish "hot page accessed 1000 times" from "scan page accessed once".

**Clock Sweep mechanics:**
- Each frame has a `usage_count` (0–`MAX_USAGE=5`).
- On `fetch_page`: if page is in pool, increment `usage_count` (capped at MAX). If not, load from disk at count=1.
- On eviction needed: walk a clock hand around the pool:
  - Pinned page → skip.
  - `usage_count > 0` → decrement, advance (give page another chance).
  - `usage_count == 0` and unpinned → **evict**.
- Scan pages accessed once → count=1 → evicted after a single clock pass.
- Hot pages accumulate count up to MAX_USAGE → survive many sweeps.

**Key operations:**
- `fetch_page(page_id)` — load from disk if not cached; increment pin count + usage count
- `unpin_page(page_id, dirty)` — decrement pin count; mark dirty if modified
- `new_page()` — allocates on disk via `HeapFile`, loads into pool at usage_count=1
- `flush_all()` — write all dirty pages to disk (called on commit/close)

**Capacity:** 128 pages per table by default (~512 KB per table in memory).

---

## 4. Indexing

### B+ Tree Design

We implement a classic B+ tree with `ORDER = 64` (maximum keys per node).

**Two node types:**

```
LeafNode:
  keys:   [k0, k1, ..., kN]          sorted list of keys
  values: [(page_id, slot_id), ...]   pointer to heap record for each key
  next:   → next LeafNode             linked list for range scans

InternalNode:
  keys:     [k0, k1, ..., kN-1]      N-1 separator keys
  children: [c0, c1, ..., cN]        N child pointers (nodes)
```

**Invariants:**
- Leaf: `len(keys) <= ORDER`; split when `len(keys) == ORDER`
- Internal: `len(children) = len(keys) + 1`
- All keys in `children[i]` are `< keys[i]` and `>= keys[i-1]`

### Search Path

```
search(key):
  node = root
  while node is InternalNode:
      i = binary_search(node.keys, key)   # find child index
      node = node.children[i]
  # now at LeafNode
  i = binary_search(leaf.keys, key)
  return leaf.values[i] if leaf.keys[i] == key else None
```

Binary search on keys at each level — `O(log N)` leaf accesses.

### Insert and Split

1. Recursively descend to the correct leaf
2. Insert `(key, value)` into leaf (sorted)
3. If leaf is full (`len(keys) == ORDER`):
   - Split at midpoint: left keeps `[:mid]`, right gets `[mid:]`
   - Promote `right.keys[0]` to parent
   - If parent is full, split parent recursively
   - If root splits, create new root with two children

### Delete

Recursive descent to leaf, remove key/value pair. No rebalancing (underflow not enforced) — simplification appropriate for this scope.

### Range Scan

```
range_scan(low, high):
  leaf = find_leaf(low)           # descend to starting leaf
  while leaf is not None:
      for k, v in zip(leaf.keys, leaf.values):
          if k > high: return
          if k >= low: yield (k, v)
      leaf = leaf.next            # follow linked list
```

Efficient because leaves are linked — no tree traversal needed after finding the start.

### Persistence

The tree is serialized to JSON (`data/<table>.idx`) on commit and loaded on startup. Production would use page-aligned binary storage; JSON is used here for transparency and debuggability.

---

## 5. Query Execution

### Parser

`sql/parser.py` implements a hand-written recursive descent parser — no external libraries.

**Tokenizer:** single regex matches string literals, floats, integers, operators (`<=`, `>=`, `!=`, etc.), punctuation, and identifiers.

```python
TOKEN_RE = re.compile(
    r"'[^']*'"       # string literal
    r'|\d+\.\d+'     # float
    r'|\d+'          # integer
    r'|[<>!=]=?'     # operators
    r'|[(),.*]'      # punctuation
    r'|[A-Za-z_]\w*' # identifiers
)
```

**Parser class:** `Parser(tokens)` with `consume(expected)`, `peek()`, `match(*keywords)`. Each SQL construct has a dedicated method:
- `parse_select()` → `SelectStmt(columns, table, joins, where, order_by)`
- `parse_insert()` → `InsertStmt(table, columns, values)`
- `parse_delete()` → `DeleteStmt(table, where)`
- `parse_create_table()` → `CreateTableStmt(table, col_defs)`

WHERE conditions are parsed as a list of `Condition(left, op, right)` objects AND-ed together.

### Query Plan Generation

`Planner.plan_and_execute(sql, txn)`:
1. Parse SQL string → AST node
2. Dispatch DDL (`CREATE TABLE`, `DROP TABLE`) directly to `MiniDB`
3. Dispatch transaction control (`BEGIN`, `COMMIT`, `ROLLBACK`)
4. Pass DML to `Executor.execute(stmt, txn)`
5. Auto-commit single-statement DML if no explicit transaction provided

### Operator Execution (Volcano Model)

Each operator exposes a `scan()` generator that yields `(row_dict, page_id, slot_id)` tuples. Operators are composed into a pipeline:

```
Projection
    └── NestedLoopJoin (for JOINs)
            ├── Filter (outer)
            │       └── SeqScan / IndexScan (outer table)
            └── Filter (inner)  [re-evaluated per outer row]
                    └── SeqScan / IndexScan (inner table)
```

**SeqScan:** iterates all pages `0..num_pages-1`, reads all live slots, passes each row through `MVCCManager.is_visible_to_txn()`.

**IndexScan:** calls `BPlusTree.search(key)` (exact) or `range_scan(low, high)` to get `(page_id, slot_id)` pointers, fetches those specific records from the buffer pool.

**Filter:** wraps a scan source, evaluates each `Condition` against the row dict. Supports `=`, `!=`, `<`, `>`, `<=`, `>=` with type coercion.

**NestedLoopJoin:** for each outer row, instantiates a fresh inner scan and tests the join predicate (`left_col = right_col`). Merges matching row dicts.

**Projection:** strips MVCC system fields (`_xmin`, `_xmax`), selects requested columns (or all for `SELECT *`).

---

## 6. Optimizer

### Cost Model

```
seq_scan_cost(table)  = num_pages
index_scan_cost(table, selectivity) = log2(num_rows) + selectivity * num_rows
```

The optimizer picks index scan only when `index_scan_cost < seq_scan_cost` AND the WHERE clause contains an equality predicate on an indexed column.

### Selectivity Estimation

```
selectivity(col, op):
  if op == '=':
      return 1.0 / ndistinct(col)   # uniform distribution assumption
  if op in ('<', '>', '<=', '>='):
      return 0.3                     # heuristic: 30% of range
  if op == '!=':
      return 1.0 - (1.0 / ndistinct(col))
  default: 0.5
```

`ndistinct` is tracked in `TableStats.col_ndistinct` and refreshed by `db.refresh_stats(table)` which does a full scan counting distinct values per column.

**Estimated output rows:** multiply selectivities of all AND conditions:
```
estimated_output = num_rows * product(selectivity(cond) for cond in WHERE)
```

### Join Ordering

Greedy strategy: sort tables by estimated output size (ascending). The table expected to produce the fewest rows after filtering becomes the outer (driving) table in the nested-loop join. This minimizes the number of inner scans.

```python
plan.join_order = sorted(tables, key=lambda t: stats[t].estimated_output(conds[t]))
```

### Scan Type Decision

For each table:
1. Check if any WHERE equality condition is on an indexed column
2. If yes, compute both costs and compare
3. Choose the lower-cost option

---

## 7. Transactions & Concurrency

### MVCC Design

MiniDB implements **Multi-Version Concurrency Control** (Track B extension, replaces Two-Phase Locking).

Every row stored on disk contains two hidden system fields:

```python
{
  '_xmin': 5,       # transaction ID that created this row version
  '_xmax': None,    # transaction ID that deleted this row (None = alive)
  'id': 1,          # user columns ...
  'name': 'Alice'
}
```

### Transaction Lifecycle

```
txid = monotonically increasing integer (global counter)

BEGIN:
  1. Allocate new txid
  2. Take snapshot: record current set of committed txids as frozenset
  3. Record snapshot_xid = max(committed) at this moment

COMMIT:
  1. WAL.log_commit(txid)
  2. Add txid to committed set
  3. Flush buffer pool to disk

ABORT/ROLLBACK:
  1. Apply undo log in reverse (remove inserted rows, restore deleted rows)
  2. WAL.log_abort(txid)
  3. txid never added to committed set
```

### Snapshot Isolation — Visibility Rule

A row is **visible** to transaction T (with snapshot S) if and only if:

```
visible(row, S) ↔
    row._xmin ∈ S.committed          # creator committed before our BEGIN
    AND (
        row._xmax is None            # row is alive (not deleted)
        OR row._xmax ∉ S.committed   # deleter had not committed at our BEGIN
    )
```

**What this means:**
- A row inserted by an uncommitted transaction is invisible (its xmin is not in `S.committed`)
- A row deleted by an uncommitted transaction is still visible (its xmax is not in `S.committed`)
- Every reader sees a frozen snapshot of the database as it existed at `BEGIN`

### Write Conflict Detection

Before deleting a row, we check whether another active (uncommitted) transaction already holds an `_xmax` on it:

```python
def can_write(row, txn):
    xmax = row['_xmax']
    if xmax is None: return True           # row unmodified
    if xmax == txn.txid: return True       # we modified it ourselves
    if xmax in active_txids: return False  # concurrent writer — conflict!
    return True
```

If a conflict is detected, the operation raises `RuntimeError("Write conflict")` and the caller should abort the transaction.

### No Read Locks

Readers never acquire any locks. A `SELECT` simply takes a snapshot and reads — concurrent `INSERT`/`DELETE` operations by other transactions are invisible (if uncommitted) or fully visible (if committed before the snapshot). This is the key advantage of MVCC over 2PL.

---

## 8. Recovery

### WAL Design

MiniDB uses **Write-Ahead Logging**: every modification is written to the log file *before* the page is modified in the buffer pool.

**Log file:** `data/wal.log` — one JSON object per line, line-buffered with `os.fsync()` after every write.

```json
{"lsn": 1, "txid": 3, "op": "BEGIN"}
{"lsn": 2, "txid": 3, "op": "INSERT", "table": "users", "page_id": 0, "slot_id": 1, "row": {"_xmin": 3, "_xmax": null, "id": 1, "name": "Alice"}}
{"lsn": 3, "txid": 3, "op": "COMMIT"}
{"lsn": 4, "txid": 4, "op": "BEGIN"}
{"lsn": 5, "txid": 4, "op": "INSERT", "table": "users", "page_id": 0, "slot_id": 2, "row": {"_xmin": 4, "_xmax": null, "id": 2, "name": "Bob"}}
{"lsn": 6, "txid": 4, "op": "ABORT"}
```

### Log Record Types

| `op` | When written | Fields |
|------|-------------|--------|
| `BEGIN` | Transaction start | `txid` |
| `INSERT` | Before page write | `txid`, `table`, `page_id`, `slot_id`, `row` |
| `DELETE` | Before marking xmax | `txid`, `table`, `page_id`, `slot_id`, `row` |
| `COMMIT` | After all writes | `txid` |
| `ABORT` | On rollback | `txid` |

### Crash Recovery Procedure

On `MiniDB.__init__()`, before opening any user sessions:

```
1. Read wal.log from start to end
2. Collect committed_txids = {txid for records with op='COMMIT'}
3. Collect redo_records = all INSERT/DELETE records where txid in committed_txids
4. For each redo record in LSN order:
     - Re-apply the INSERT or DELETE to the heap pages
     - Update the B+ tree index accordingly
5. Restore TransactionManager state:
     - committed = committed_txids
     - next_xid  = max(committed_txids) + 1
6. Flush all pages to disk
```

**REDO only — no UNDO needed.** This is a key MVCC property: uncommitted rows (those with `_xmin` not in `committed`) are *automatically invisible* to all future transactions due to the visibility rule. We do not need to physically remove them — they simply never satisfy `is_visible()`. We only redo work for committed transactions that may not have been flushed to disk before the crash.

---

## 9. Extension Track — Track B: MVCC

### Motivation

Two-Phase Locking (2PL) serializes transactions by acquiring read and write locks. Under high read load, readers block writers waiting to update data, and writers block readers. This limits concurrency on read-heavy workloads.

**MVCC eliminates reader-writer blocking:**
- Readers read from their snapshot — no locks acquired
- Writers create new row versions — they don't overwrite what readers are seeing
- A writer only conflicts with another *concurrent writer* on the same row

### Design

**`Snapshot` class:**
```python
class Snapshot:
    snapshot_xid: int        # max committed txid at BEGIN time
    committed: frozenset     # all committed txids at BEGIN time
```

Using a `frozenset` ensures the snapshot is immutable — a transaction's view of committed transactions cannot change mid-execution.

**`Transaction` class:**
```python
class Transaction:
    txid: int
    snapshot: Snapshot
    state: 'ACTIVE' | 'COMMITTED' | 'ABORTED'
    _undo_log: list          # for rollback
```

**Version chain:** MiniDB stores one version per row (the current version with updated `_xmax`). Full multi-version chains (keeping all historical versions) are a production extension beyond this scope.

**Garbage collection:** deleted rows (where `_xmax` is committed and `_xmax < min(active_txids)`) are not yet reclaimed. This is a known limitation.

### Results

Under concurrent load (4 reader threads + 1 writer thread):

- **Readers are never blocked** by the writer — each reader holds its own snapshot
- The writer proceeds without waiting for readers to release locks
- Each reader sees a consistent point-in-time view: rows inserted by the writer after the reader's `BEGIN` are invisible to that reader

Benchmark results are in the `benchmarks/` directory.

---

## 10. Benchmarks

Full results: see `benchmarks/results.json` and `benchmarks/report.txt`.

### Experimental Setup

- **Hardware:** single machine, macOS, NVMe SSD
- **Dataset:** 200–2000 rows, integer and text columns, batch size 100
- **Python:** 3.11+, no external libraries
- **Buffer pool capacity:** 128 pages (normal), 8 pages (index benchmark)

### Test Results

**1. Insert Throughput**

| Rows | Time | Throughput |
|------|------|------------|
| 100  | 0.007s | ~13,000 rows/sec |
| 500  | 0.049s | ~10,000 rows/sec |
| 1000 | 0.21s  | ~4,700 rows/sec  |
| 2000 | 1.47s  | ~1,350 rows/sec  |

Throughput drops at large N due to fsync on every commit and free-page scan cost. The free-page hint optimization cut early-access cost, but compaction of deleted slots remains a gap.

**2. MVCC Concurrent Read Throughput**

4 reader threads running `SELECT` queries simultaneously with 1 writer committing inserts:

| Readers | Avg q/s per reader | Total q/s |
|---------|--------------------|-----------|
| 1       | ~470               | 470       |
| 2       | ~174               | 349       |
| 4       | ~128               | 510       |

Readers are never blocked by the writer — MVCC snapshot isolation eliminates reader-writer contention. Total throughput scales with readers because the bottleneck shifts to CPU/JSON deserialization, not locking.

**3. MVCC Snapshot Isolation (correctness)**

T1 begins, T2 inserts 50 rows and commits. T1's snapshot still shows original row count — T1 is unaffected by T2's commit. Fresh transaction after T2 sees all rows. Verified correct in all runs.

**4. Index Scan vs Sequential Scan (cold reads, buffer pool = 8 pages)**

Methodology: buffer pool flushed and evicted before each query repetition. Buffer pool set to 8 pages — smaller than table size — so OS page cache is the only cache. SeqScan must load all N pages; IndexScan loads 1-2 pages via B+ tree pointer.

| Rows | SeqScan | IndexScan | Speedup |
|------|---------|-----------|---------|
| 200  | 0.83ms  | 0.77ms    | 1.1x    |
| 500  | 2.13ms  | 1.93ms    | 1.1x    |
| 1000 | 5.90ms  | 0.30ms    | **20x** |
| 2000 | 9.70ms  | 0.09ms    | **109x**|

At small N, the table fits within the 8-page buffer pool — both paths are equivalent. Once the table exceeds the buffer pool, index benefit is dramatic: SeqScan cost grows linearly with pages; IndexScan cost stays O(log N).

**5. WAL Recovery**

200 committed rows + 1 uncommitted insert (simulated crash). Recovery replays 200 log records in ~4ms. Uncommitted row never appears after recovery (MVCC: xmin of aborted txn is not in committed set).

### Running the Benchmarks

```bash
python benchmark_full.py   # full suite → benchmarks/results.json
```

---

## 11. Limitations

| Limitation | Detail |
|-----------|--------|
| No slot compaction | Deleted slots (TOMBSTONE) are not reclaimed; pages gradually waste space |
| In-memory B+ tree | Index lives in RAM during runtime; serialized to JSON on commit. Not page-aligned. |
| Primary key index only | One B+ tree per table on the PK column; no secondary indexes |
| No GROUP BY / aggregation | `SUM`, `COUNT`, `GROUP BY`, `HAVING` not implemented |
| No UPDATE statement | Updates require `DELETE` + `INSERT` |
| No OR in WHERE | Only AND-connected predicates supported |
| Single machine | No replication, no distributed transactions |
| No version GC | Old row versions with committed `_xmax` are never physically removed |
| No type enforcement at storage | Values stored as-is in JSON; type validation is schema-level only |
| No nested queries | Subqueries in WHERE/FROM not supported |

---

## 12. How to Run

### Requirements

- Python 3.11 or later
- No external dependencies (stdlib only: `json`, `os`, `struct`, `threading`, `re`, `collections`)

### Setup

```bash
git clone <repo-url>
cd MiniDB_Projects/Team_DBAlchemist
```

### Run Smoke Tests

```bash
python3 tests/test_basic.py
```

Expected output: `6/6 passed`

### Run Benchmarks

```bash
# Quick benchmark (insert throughput, MVCC reads, index vs scan)
python3 benchmarks/benchmark.py

# Full benchmark suite → writes results to benchmarks/results.json and benchmarks/report.txt
python3 benchmarks/benchmark_full.py
```

### Interactive Usage

```python
import sys
sys.path.insert(0, 'src')

from db import MiniDB

db = MiniDB('data/')

# DDL
db.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT)")

# Auto-commit INSERT
db.execute("INSERT INTO users (id, name, age) VALUES (1, 'Alice', 30)")

# Explicit transaction
txn = db.begin()
db.execute("INSERT INTO users (id, name, age) VALUES (2, 'Bob', 25)", txn)
db.execute("INSERT INTO users (id, name, age) VALUES (3, 'Carol', 35)", txn)
db.commit(txn)

# SELECT
rows = db.execute("SELECT * FROM users WHERE age > 28")
print(rows)
# [{'id': 1, 'name': 'Alice', 'age': 30}, {'id': 3, 'name': 'Carol', 'age': 35}]

# JOIN
rows = db.execute("""
    SELECT name, amount
    FROM orders
    JOIN users ON orders.user_id = users.id
    WHERE amount > 100
""")

# Rollback
txn = db.begin()
db.execute("INSERT INTO users (id, name, age) VALUES (99, 'Temp', 0)", txn)
db.rollback(txn)   # row 99 never visible

# Refresh optimizer statistics
db.refresh_stats('users')

db.close()
```

### Directory Structure After Running

```
data/
  catalog.json          # table schemas
  wal.log               # write-ahead log
  users.heap            # heap file (pages)
  users.idx             # B+ tree index (JSON)
```

---

## Design Decisions & Trade-offs

| Decision | Alternative | Reason |
|----------|------------|--------|
| JSON row encoding | Binary struct packing | Simpler, debuggable, avoids variable-length binary complexity |
| MVCC over 2PL | Two-Phase Locking | No reader-writer blocking; cleaner isolation model |
| REDO-only WAL recovery | UNDO+REDO (ARIES) | MVCC makes uncommitted rows automatically invisible; no physical undo needed |
| In-memory B+ tree | Page-based B+ tree | Simpler implementation; correct semantics; persisted to JSON on commit |
| Greedy join ordering | Dynamic programming | Sufficient for small join counts; DP overkill for ≤4 tables |
| Nested-loop join | Hash join / sort-merge | Simplest correct implementation; hash join would be better for large tables |
| **Clock Sweep buffer replacement** | Plain LRU | OS page cache already does LRU on file blocks — a second LRU layer adds no value. Clock Sweep prevents sequential scan flooding: scan pages (accessed once) are evicted quickly while hot pages accumulate usage count and survive. Same policy as PostgreSQL. |
| **Skip WAL for read-only (SELECT) transactions** | Log all transactions | SELECT never modifies data, so it needs no recovery record. Logging BEGIN/ABORT for reads added an fsync on every query, dominating benchmark latency. Read-only txns still get a snapshot for MVCC visibility, but no disk write occurs. |
| **Cold-read index benchmark** (small buffer pool=8) | Warm-cache repeated scan | With warm cache, both SeqScan and IndexScan hit buffer pool equally — speedup is hidden. Real index benefit appears when dataset >> buffer pool: SeqScan fetches all N pages, IndexScan fetches 1-2 pages. Benchmark uses `buffer_capacity=8` to force this condition. |
| **Free-page hint for inserts** | Scan from page 0 each time | Without the hint, each insert scans pages from 0 to find one with space — O(n) scan cost on bulk inserts. Tracking the last page with free space converts this to O(1) amortized. |
