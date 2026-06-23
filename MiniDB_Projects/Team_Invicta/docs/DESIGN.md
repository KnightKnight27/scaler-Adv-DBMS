# MiniDB — Design Notes

Deeper internals behind the README. Read top to bottom; each layer builds on
the one before it.

## Layering

```
SQL text → Parser → Optimizer → Executor → RowStore → (BufferPool → DiskManager) | (LSMTree)
                                              ↑                ↑
                                         cross-cutting:  LockManager / TransactionManager / WAL
```

Strict rule: **every heap page access goes through the buffer pool**, and the
buffer pool is the only thing above the disk manager. The two storage engines
(`HeapRowStore`, `LSMRowStore`) are hidden behind one `RowStore` interface so
everything above them is storage-agnostic.

## Storage engine

- **DiskManager** (`storage/disk_manager.*`): the file is an array of 4 KB
  pages; page `p` is at byte offset `p*4096`. `AllocatePage()` hands out ids.
  Writes are write-through (flush each write) — a deliberate
  durability-over-throughput choice that keeps the crash-recovery story simple.
- **BufferPoolManager** (`storage/buffer_pool_manager.*`): fixed frames,
  `page_table_` maps page id → frame, a free list, and an **LRU replacer**.
  `FetchPage` pins; `UnpinPage` releases and (when pin count hits 0) makes the
  frame an eviction candidate. A dirty victim is flushed before its frame is
  reused. This is the single I/O choke point.
- **Slotted page** (`storage/table_page.h`): header `(next_page_id, num_slots,
  free_ptr)`, slots grow forward, tuples grow backward. Delete sets a slot's
  length to 0 (tombstone) so the RID stays stable for the index.
- **TableHeap** (`storage/table_heap.*`): a linked list of slotted pages with a
  forward iterator for sequential scans.

## Record layer

`Schema` is an ordered column list with one optional primary key. `Tuple`
serializes positionally: INTEGER → 8 bytes, VARCHAR → 4-byte length + bytes.
Variable-length tuples live in the slotted pages.

## B+ tree (`index/bplus_tree.*`)

Maps `int64` PK → `RID`. A durable **header page** stores the root page id (so
the tree survives restart and root splits). Leaves chain via `next_leaf` for
range scans. Node capacity 250 keys/page. Insert records the descent path so a
split propagates a separator upward, creating a new root when the root splits.
Deletion removes the entry from the leaf (no node merging — a documented
simplification; searches/scans stay correct, nodes may be under-full).

## SQL front end

`Parser` (`sql/parser.*`) is a hand-written lexer + recursive-descent parser for
the supported subset (CREATE/INSERT/SELECT/DELETE/BEGIN/COMMIT/ROLLBACK),
producing a `Statement` AST. Qualified names (`t.col`) lex as one token.

## Execution — Volcano model (`exec/executor.*`)

Every operator implements `Init()` + `Next(Tuple*)`; the root is pulled until
`Next` returns false. `Init()` may be re-called to rewind (the join relies on
this). Operators: `SeqScan`, `IndexScan`, `Filter`, `NestedLoopJoin`,
`Projection`, `Count`. Predicates are *bound* to column positions against the
operator's output schema (qualified `table.col` names keep joins unambiguous);
unknown/ambiguous references throw.

## Optimizer (`optimizer/optimizer.*`)

Cost-based. Statistics per table: row count + observed `[min,max]` PK. For a PK
predicate it derives a `[low,high]` range, estimates selectivity
`frac = (hi-lo+1)/(max-min+1)`, and compares:

```
seqCost = N
idxCost = log2(N) + frac*N
```

choosing the index scan only when `idxCost < seqCost`. So a selective predicate
(`id = 42`) uses the index; a non-selective one (`id >= 1` on dense keys) falls
back to a sequential scan. A scan always sits under a `Filter` re-checking all
predicates, so results are correct regardless of the access path. Join order
for the 2-table nested-loop join drives with the smaller relation. The chosen
plan is emitted as an EXPLAIN string (`.explain on` in the shell).

## Transactions (`txn/*`)

- **LockManager**: row-level shared/exclusive locks (`RowId = table + key`).
  Incompatible requests block on a per-resource condition variable. **Strict
  2PL**: locks held until commit/abort. **Deadlock detection** builds a
  waits-for graph at block time; a cycle aborts the **youngest** transaction
  (largest id). Supports S→X upgrade.
- **TransactionManager**: assigns ids; commit releases locks; abort first
  replays the transaction's undo log (inverse ops, reverse order) then releases.

## Recovery (`recovery/wal.*` + `engine/Database::Recover`)

Write-ahead logging. Before a page is changed, an INSERT/DELETE record (with the
row image) is appended and flushed. BEGIN/COMMIT/ABORT bracket transactions. On
startup:

1. Scan the log; a transaction's effects count only if it has a COMMIT record.
2. **REDO** (forward): reapply committed INSERT/DELETE, idempotently
   (insert-if-absent, delete-if-present) so partially-flushed states converge.
3. **UNDO** (backward): revert uncommitted INSERT/DELETE the same way.
4. Checkpoint: flush the now-consistent data file and truncate the log.

DDL (`CREATE TABLE`) flushes its initial pages immediately, so even a crash
before any data flush leaves a well-formed heap/index to recover into.

## Track C — LSM tree (`lsm/*`)

- **MemTable**: sorted `std::map` of key → entry (value or tombstone). Writes
  land here; flushed to an SSTable when it passes a byte threshold.
- **SSTable**: immutable sorted run on disk: `count`, sorted
  `key|deleted|vlen|value` records, then a **Bloom filter** blob and a trailer.
  On open the key index + Bloom filter load into memory; values are read from
  disk on demand. A point lookup checks the Bloom filter first to skip files
  that cannot contain the key.
- **LSMTree**: read path = MemTable, then SSTables newest→oldest, first hit
  wins (a tombstone hit ⇒ "absent"). **Size-tiered compaction** merges all
  SSTables into one when the count hits a trigger, keeping the newest version
  per key and dropping tombstones.
- **LSMRowStore**: implements `RowStore` over `LSMTree`, so SQL runs unchanged.
  Selected per table with `CREATE TABLE ... USING LSM`.

## Known simplifications (also in README §11)

- B+ tree deletes don't merge under-full nodes.
- Engine-level locking covers writes (X locks); full predicate/scan locking is
  not implemented — concurrency is demonstrated at the LockManager layer.
- LSM uses a dense in-memory index per SSTable (real systems use sparse).
- Single data file per database; no page-level checksums.
