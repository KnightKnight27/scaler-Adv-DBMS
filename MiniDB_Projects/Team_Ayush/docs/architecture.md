# MiniDB — Architecture Notes

This complements the README with on-disk formats and the request lifecycle.

## On-disk layout (`minidb.db`, 4096-byte pages)

- **Page 0** — serialized catalog: magic, table count, then per table
  `{name, heap_first_page, pk_index_header_page, row_count, pk_index, columns[]}`.
- **Heap page** — `[next_page:i32][record_size:i16][num_used:i16]` then fixed slots
  `[status:u8][payload:record_size]`.
- **B+Tree node page** — `[is_leaf:u8][_:1][count:i16][next:i32]`, then a key array
  (`i32` each) and either a RID array (leaf, `page:i32 + slot:i16`) or a child array
  (internal, `i32` each). A separate **header page** stores the root page id.
- **WAL (`minidb.wal`)** — fixed 17-byte records: `[type:u8][txn:i32][idx:i32][before:i32][after:i32]`.

All multi-byte fields are written **little-endian, field by field** — never as a padded
struct — so formats are stable across builds.

## Lifecycle of a `SELECT`

1. `Tokenize` → `Parse` → `SelectStmt` AST (`src/sql`).
2. Executor resolves the table(s) in the catalog and splits `WHERE` predicates per table.
3. `Optimizer::ChooseAccess` computes selectivity and `seq_cost` vs `index_cost`, returning
   either a sequential scan or an index scan with `[low, high]` key bounds.
4. The executor builds a Volcano tree, e.g. `Project → Filter → IndexScan`, or for a join
   `Project → NestedLoopJoin(outer, inner-factory)`.
5. `Open/Next/Close` pulls rows; scans read pages through the **buffer pool**, which loads
   them from the **disk manager**, evicting with CLOCK and writing back dirty pages.

## Concurrency control abstraction

Operators are concurrency-control agnostic. Two strategies coexist:

- **2PL** (`lock_manager`) — shared/exclusive locks, strict release at commit/abort,
  wait-for-graph deadlock detection (abort the youngest).
- **MVCC** (`mvcc`) — version chains with snapshot visibility; reads are lock-free.

The mode is selected for demonstrations/benchmarks; the storage and execution layers are
unchanged between them, which is what makes the head-to-head comparison fair.

## Recovery invariant

Write-ahead logging + force-log-at-commit guarantees: after a crash, **redo** re-applies
all logged after-images (repeating history) and **undo** reverts the updates of
transactions without a `COMMIT` record. Committed work survives; uncommitted work
disappears.
