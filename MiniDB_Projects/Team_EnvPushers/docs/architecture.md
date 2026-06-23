# MiniDB — Architecture & Design Notes

A component-by-component deep dive, plus the trade-offs

---

## 1. Layering and dependencies

```
database.cpp  (orchestrator)
   ├── sql/        parse SQL -> AST
   ├── optimizer/  AST  -> physical operator tree
   ├── execution/  pull rows through operators
   ├── catalog/    schemas, tuple <-> bytes, table metadata, stats
   ├── index/      B+ tree (PK)
   ├── transaction/lock manager + txn lifecycle (strict 2PL)
   ├── recovery/   WAL append/parse
   └── storage/    heap file -> buffer pool -> disk manager -> file
common/types.hpp is shared by everyone (Value, RID, TypeId).
```

Dependencies point **downward** only. The storage stack has no knowledge of SQL;
the SQL stack speaks to storage exclusively through `Database` helpers and the
`HeapFile` / `BPlusTree` interfaces.

---

## 2. Storage engine

### 2.1 Page (`storage/page.hpp`)
Slotted page, 4 KiB. Key methods: `insert`, `get`, `erase` (tombstone),
`update_inplace`. The header is `memcpy`-ed in and out of the raw buffer, so the
page *is* its on-disk representation — no separate serialization step.

**Invariant:** `free_ptr` is the boundary between the slot directory (growing up
from the header) and the record area (growing down from `PAGE_SIZE`).
`free_space = free_ptr - (HEADER + num_slots*SLOT)`.

### 2.2 Disk manager (`storage/disk_manager.cpp`)
`page_id ↔ offset = page_id * PAGE_SIZE`. `allocate_page()` extends the file and
**writes a valid empty page header** (not zeros) so an allocated-but-unflushed
page still reads back as a chain terminator (`next_page_id = -1`) after a crash —
this is what prevents an infinite loop when scanning a torn heap.

### 2.3 Buffer pool (`storage/buffer_pool.cpp`)
`fetch_page` pins and returns a frame, loading from disk on miss; `new_page`
allocates. LRU eviction (`lru_` list, front = least recent) skips pinned frames
and flushes dirty victims. `before_flush` is the WAL hook. Counters feed the
`.stats()` shown in the demo.

**Invariant (write-ahead):** a dirty page is never written to disk before
`before_flush(page_id)` (which flushes the WAL) has run.

---

## 3. B+ Tree (`index/bplus_tree.hpp`)

- `ORDER = 64`. Min occupancy `(ORDER+1)/2 - 1`.
- **Insert** recursion returns a pushed-up separator + new right sibling when a
  child splits; the root split adds a level.
- **Delete** recursion repairs underflow via `borrow_from_left/right` or
  `merge`, then collapses an empty root.
- Leaves linked via `next` for ordered range scans.

**Why in-memory + rebuilt on open?** It isolates the *algorithmic* content of
indexing (splits/merges/range walk) from the orthogonal problem of paging a tree
to disk with its own WAL. We rebuild the tree by scanning the heap in
`Database::rebuild_indexes`. Trade-off documented in README §11.

Tested directly in `tests/run_tests.cpp::test_btree` (insert/search/range/delete
with rebalancing) and stress-tested separately to 5k random keys.

---

## 4. Catalog & tuples (`catalog/`)

- `Schema` = ordered `Column{name, type, is_primary_key}`.
- `serialize_tuple` / `deserialize_tuple`: per column `[null-flag][payload]`,
  where INT is 8 bytes and TEXT is `[u32 len][bytes]`.
- `TableInfo` holds the schema, the heap's `first_page_id`, the PK `BPlusTree`,
  and stats (`row_count`). The catalog persists as a small text sidecar
  (`minidb.catalog`) so it is human-inspectable.

---

## 5. SQL front-end (`sql/`)

`Lexer` → tokens; `Parser` (recursive descent, precedence-climbing for boolean
expressions) → AST nodes in `ast.hpp`. Keywords are matched case-insensitively.
The grammar is documented at the top of `parser.hpp`.

---

## 6. Execution (`execution/executor.{hpp,cpp}`)

Volcano model. Each operator carries `out_schema_` (a list of
`{table, name, type}`), which is how `eval_expr` resolves a column reference —
qualified (`u.id`) or unqualified (resolved if unambiguous). `SeqScan`/`IndexScan`
materialize their rows in `open()` for simplicity; `Filter`/`Projection` stream;
`NestedLoopJoin` materializes the inner side once; `Aggregate` and `Sort`
buffer-then-emit.

---

## 7. Optimizer (`optimizer/optimizer.cpp`)

1. Collect conjuncts from `WHERE` + every `JOIN ... ON`.
2. Split into single-table (push-down filters) vs. multi-table (join preds).
3. Estimate each table's cardinality after local filters (selectivity model in
   README §6).
4. Greedily order tables smallest-first; build a left-deep nested-loop tree,
   attaching each join predicate once both of its tables are in the plan.
5. Add `Aggregate`/`Projection`, then `Sort`.
6. Emit an `EXPLAIN` string alongside the operator tree.

---

## 8. Transactions (`transaction/`)

`LockManager` keeps, per `LockId`, a FIFO request queue + a condition variable.
`acquire` grants if compatible with all granted requests, else builds the
wait-for graph and runs DFS cycle detection before blocking; a cycle makes the
caller the victim (`DeadlockError`). `release_all` drops a txn's locks and
notifies waiters. `TransactionManager` issues ids and runs commit (release locks)
/ abort (run undo closures LIFO, then release).

**Invariant (strict 2PL):** a transaction acquires locks during execution and
releases *all* of them only at commit/abort — never earlier.

---

## 9. Recovery (`recovery/` + `Database::recover`)

Logical, PK-keyed, idempotent redo + before-image undo. See README §8 for the
procedure. The crucial property: because redo is `upsert`-by-key and undo is
`upsert`/`delete`-by-key, replaying the log over a *partially* flushed data file
converges to exactly "committed data present, in-flight data absent."

---

## 10. Extension: LSM-tree (`lsm/`)

`MemTable` (sorted map, tombstones) → flush → `SSTable` (immutable sorted run) →
`compact()` (k-way merge, newest-wins, drop tombstones). Read path: MemTable,
then SSTables newest→oldest, first hit wins. Benchmarked vs. the B+ Tree store in
`benchmarks/bench_lsm.cpp`.




