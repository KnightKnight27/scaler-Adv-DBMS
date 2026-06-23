# MiniDB — Design Notes & Viva Prep

Concise companion to the README: the *why* behind key decisions and a quick map
for explaining any part of the system during the viva.

## Module map (where to look)

| Subsystem | Files | One-line summary |
|-----------|-------|------------------|
| Config / types | `src/common/*` | page size, id aliases, `Value`/`Tuple` |
| Disk I/O | `src/storage/disk_manager.*` | `pread`/`pwrite` of 4 KB pages |
| Buffer pool | `src/storage/buffer_pool.*`, `lru_replacer.h` | cache, pin/dirty, LRU eviction |
| Heap | `src/storage/table_heap.*` | slotted pages, O(1) tail append, tombstone delete |
| B+Tree | `src/index/btree.*` | page-resident, int key → RID, splits, range scan |
| Catalog | `src/catalog/*` | schemas, per-table heap + PK index, `row_count` |
| SQL | `src/sql/*` | lexer + recursive-descent parser → AST |
| Optimizer | `src/optimizer/*` | selectivity, scan choice, join order |
| Executor | `src/exec/*` | seq/index scan, filter, nested-loop join, insert/delete |
| Transactions | `src/txn/*` | strict 2PL lock manager, wait-for deadlock detection, undo |
| Recovery | `src/recovery/*` | append-only WAL, logical redo |
| Engine | `src/engine/*` | wires everything; `execute()`, begin/commit/abort, recover |
| Extension (LSM) | `src/lsm/*` | MemTable + SSTable + compaction |

## Key design decisions & trade-offs

1. **POSIX `pread`/`pwrite` over `std::fstream`.** Mixing read/write with seeks on
   a stream forces buffer flushes; switching to offset-addressed syscalls made
   the engine ~1000× faster (50k B+Tree ops: minutes → 0.2 s).
2. **WAL is the source of truth; data file is scratch.** On startup we truncate
   the data file and rebuild by replaying committed transactions. This makes
   recovery obviously correct (redo from empty is idempotent) at the cost of full
   log replay — a deliberate simplicity-vs-efficiency trade. Page-LSN redo +
   checkpoint truncation is the documented next step.
3. **Logical, schema-serialized log records.** Redo re-applies inserts/deletes by
   content, so it does not depend on physical page layout matching.
4. **Strict 2PL, table-level.** Coarse but provably serializable and easy to
   demonstrate (deadlock victim selection). No lock upgrade → writers take `X`
   up front.
5. **O(1) heap append.** A tail-page pointer avoids the O(n²) "walk the chain
   every insert" trap (we hit and fixed exactly this: 100k load 83 s → 1.4 s).
6. **Index rebuilt on open, not persisted.** Simpler and always consistent with
   the base table after recovery.

## Likely viva questions → where the answer lives

- *"Walk a SELECT end-to-end."* `Database::execute` → `ParseSQL` →
  `Optimizer::plan_select` → `Executor::run_select` → `TableHeap::scan` /
  `BPlusTree::search` → buffer pool → disk.
- *"How does a B+Tree split propagate?"* `BPlusTree::insert` records the
  root-to-leaf path, splits the leaf, then `insert_into_parent` recurses upward,
  growing a new root when the split reaches the top.
- *"How is serializability guaranteed?"* Strict 2PL: `LockManager::acquire`
  during execution, `release_all` only at commit/abort.
- *"Show a deadlock."* `tests/test_concurrency.cpp` / wait-for cycle detection in
  `LockManager::detects_cycle`.
- *"Prove committed data survives a crash."* `demos/demo_recovery.sh` (kill -9)
  and `Database::recover`.
- *"Why is the LSM faster at writes?"* In-memory MemTable + sequential SSTable
  flush vs random B+Tree page writes/splits (see `benchmarks/results.txt`).
