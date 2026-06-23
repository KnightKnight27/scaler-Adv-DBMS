# MiniDB Modules Reference

Per-module purpose, key classes/functions, path, dependencies, and design
trade-offs. All modules live under `src/minidb/`. Tests are in `tests/`.

---

### `constants.py`
- **Purpose:** engine-wide tunables.
- **Defines:** `PAGE_SIZE` (4096), `SLOT_SIZE`, `TOMBSTONE`, `DEFAULT_POOL_FRAMES`,
  struct format strings, `INVALID_PAGE_ID`.
- **Depends on:** nothing. **Tested by:** used throughout; `test_page.py` etc.
- **Trade-off:** one place to retune page geometry; everything downstream inherits it.

### `types.py`
- **Purpose:** type system + row ↔ bytes codec.
- **Key:** `ColumnType`, `Column`, `Schema` with `encode`/`decode`, `index_of`,
  `primary_key_index`.
- **Depends on:** `constants`. **Tested by:** `test_types.py` (11).
- **Trade-off:** null bitmap keeps NULLs free; self-describing only given the schema.

### `page.py`
- **Purpose:** fixed-size slotted page.
- **Key:** `Page` (`insert`, `get`, `delete`, `live_slots`, `items`, `to_bytes`).
- **Depends on:** `constants`. **Tested by:** `test_page.py` (8).
- **Trade-off:** tombstone deletes keep RIDs stable; space reclaimed only on full
  page rewrite (Postgres-style deferred reclamation).

### `disk_manager.py`
- **Purpose:** page-granular I/O over a single file.
- **Key:** `DiskManager` (`allocate_page`, `read_page`, `write_page`, `flush`,
  `num_pages`, `reads`/`writes`).
- **Depends on:** `constants`. **Tested by:** `test_disk_manager.py` (6).
- **Trade-off:** deliberately "dumb" (no table awareness); `fsync` is the WAL's
  durability hook.

### `buffer_pool.py`
- **Purpose:** bounded page cache with clock-sweep eviction.
- **Key:** `BufferPool` (`new_page`, `fetch_page`, `unpin_page`, `flush_all`,
  `hits`/`misses`/`hit_ratio`), `BufferPoolFullError`.
- **Depends on:** `disk_manager`, `page`, `constants`. **Tested by:** `test_buffer_pool.py` (8).
- **Trade-off:** clock-sweep approximates LRU cheaply; pinned pages never evicted.

### `heap.py`
- **Purpose:** a table's rows across pages, addressed by RID.
- **Key:** `RID` (serializable), `HeapFile` (`insert`, `get`, `delete`,
  `update_in_place`, `scan`).
- **Depends on:** `buffer_pool`, `page`. **Tested by:** `test_heap.py` (10).
- **Trade-off:** append-mostly insert (O(1) amortized) matches tombstone-only
  deletes; small gaps in non-tail pages not reclaimed.

### `btree.py`
- **Purpose:** in-memory B+ tree index, key → RID.
- **Key:** `BPlusTree` (`insert`, `search`, `range`, `delete`, `height`),
  `_Leaf`/`_Inner`, `DuplicateKeyError`.
- **Depends on:** `heap` (RID). **Tested by:** `test_btree.py` (15, incl. fuzz).
- **Trade-off:** in-memory + rebuilt from heap → durability stays heap+WAL only,
  so an index can't be crash-corrupted.

### `catalog.py`
- **Purpose:** system catalog; tables, schemas, indexes, stats.
- **Key:** `Table` (`insert_row`, `delete_by_rid/values/key`, `index_lookup`,
  `index_range`, `get_by_pk`, `scan`, `create_index`, `analyze`), `_Index`,
  `TableStats`, `Catalog`.
- **Depends on:** `btree`, `heap`, `types`, `buffer_pool`. **Tested by:** `test_catalog.py` (11).
- **Trade-off:** non-unique secondary indexes use composite `(value, page, slot)`
  keys; in-memory registry rebuilt from the WAL.

### `sql.py`
- **Purpose:** tokenizer, AST, recursive-descent parser.
- **Key:** `tokenize`, `Parser`, `parse`; AST nodes (`CreateTable`, `Insert`,
  `Select`, `Delete`, `Explain`, `Begin/Commit/Rollback`, `Comparison`, …),
  `ParseError`.
- **Depends on:** `engine` (MiniDBError), `types`. **Tested by:** `test_sql.py` (23).
- **Trade-off:** teaching subset; precedence encoded structurally in the parser.

### `executor.py`
- **Purpose:** Volcano operators + DDL/DML execution.
- **Key:** `Row`, `evaluate`, operators (`SeqScan`, `IndexScan`, `Filter`,
  `NestedLoopJoin`, `Project`), `build_naive_plan`, `materialize`,
  `exec_create_table/index`, `exec_insert`, `exec_delete`, `ExecutionError`.
- **Depends on:** `catalog`, `sql`, `heap`, `types`, `engine`. **Tested by:** `test_executor.py` (15).
- **Trade-off:** iterator model streams tuples (bounded memory); nested-loop join
  is simple and benefits directly from an inner-side index.

### `plan.py`
- **Purpose:** cost-based optimizer.
- **Key:** `optimize`, `choose_access_path`, `eq_selectivity`, `conjuncts`,
  `format_plan`.
- **Depends on:** `executor`, `catalog`, `sql`. **Tested by:** `test_plan.py` (12).
- **Trade-off:** greedy left-deep join order + simple selectivity heuristics;
  loose IndexScan bounds + residual Filter guarantee correctness.

### `lock_manager.py`
- **Purpose:** 2PL lock table + deadlock detection.
- **Key:** `LockMode`, `LockManager` (`acquire`, `release`, `release_all`,
  `wait_for_graph`), `DeadlockError`.
- **Depends on:** stdlib `threading`. **Tested by:** `test_concurrency.py` (10).
- **Trade-off:** detection-at-wait-time (no timeouts); requester is the victim.

### `transaction.py`
- **Purpose:** transactions with strict 2PL.
- **Key:** `Transaction` (`lock_shared/exclusive`, `commit`, `abort`), `TxnState`,
  `TransactionManager`.
- **Depends on:** `lock_manager`. **Tested by:** `test_concurrency.py`.
- **Trade-off:** strict 2PL → serializable, no cascading aborts; table-level locks.

### `wal.py`
- **Purpose:** write-ahead log + redo recovery.
- **Key:** `WriteAheadLog` (`append`, `flush`, `read_all`, `close`), `replay`.
- **Depends on:** `catalog`, `types`. **Tested by:** `test_recovery.py` (8).
- **Trade-off:** WAL-as-truth + logical logging → trivial REDO, no UNDO; full
  replay on open (no checkpoint).

### `lsm.py` — Track C extension
- **Purpose:** log-structured merge-tree storage engine.
- **Key:** `LSMTree` (`put`, `delete`, `get`, `flush`, `compact`, `items`,
  `stats`), `SSTable` (`create`, `get`, `items`), `BloomFilter`, `TOMBSTONE`.
- **Depends on:** stdlib only. **Tested by:** `test_lsm.py` (15).
- **Trade-off:** great write throughput + bounded space via compaction; read- and
  write-amplification, mitigated by Bloom filters.

### `engine.py`
- **Purpose:** the `Database` facade wiring everything.
- **Key:** `Database` (`execute`, `close`, `crash`, `_rebuild_state`,
  transaction/lock/WAL orchestration), `Result`, `MiniDBError`.
- **Depends on:** all of the above (lazy imports avoid cycles). **Tested by:**
  `test_engine_skeleton.py`, `test_executor.py`, `test_plan.py`, `test_recovery.py`.
- **Trade-off:** single public entrypoint keeps demos/tests stable; autocommit
  unless inside `BEGIN`.

### `cli.py`
- **Purpose:** REPL over the facade.
- **Key:** `run_repl`, `main`; meta-commands `.help`, `.exit`.
- **Depends on:** `engine`. **Run:** `python -m minidb.cli [path]`.
