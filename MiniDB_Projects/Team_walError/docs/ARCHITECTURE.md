# MiniDB Architecture

A component-by-component walkthrough of the engine: what each layer owns, the key
data structures, the public interfaces, and how a query flows through them. For a
line-by-line trace of specific operations see [`DATA_FLOW.md`](DATA_FLOW.md); for a
per-module quick reference see [`MODULES.md`](MODULES.md).

## Layering

MiniDB is a strict bottom-up stack. Each layer depends only on the layers below
it and exposes a small interface upward.

```
engine.py (Database facade)  ── public API: execute(sql) -> Result
│
├─ sql.py        text -> tokens -> AST
├─ plan.py       AST -> physical operator tree (cost-based)
├─ executor.py   Volcano operators that pull tuples
│
├─ catalog.py    Table = schema + heap + indexes + stats
│   ├─ btree.py  B+ tree (key -> RID)
│   └─ heap.py   HeapFile (RID -> record bytes)
│       └─ buffer_pool.py  frames + clock-sweep
│           └─ disk_manager.py  one file of fixed pages
│               └─ page.py / types.py / constants.py  byte layout
│
├─ transaction.py + lock_manager.py   strict 2PL, deadlock detection
├─ wal.py                              write-ahead log + redo recovery
└─ lsm.py                              independent LSM engine (Track C)
```

## The byte foundation (`constants.py`, `types.py`, `page.py`)

- **`constants.py`** centralizes tunables: `PAGE_SIZE` (4096), slot size, struct
  formats, and the `TOMBSTONE` sentinel.
- **`types.py`** defines the type system and the row codec:
  - `ColumnType` (INT/FLOAT/TEXT/BOOL), `Column`, `Schema`.
  - `Schema.encode(row) -> bytes` / `decode(bytes) -> row`. Layout: a null bitmap
    (1 bit/column) followed by each non-null value (INT/FLOAT 8 bytes, BOOL 1
    byte, TEXT length-prefixed UTF-8). NULLs occupy zero value bytes.
- **`page.py`** — `Page`: a slotted page over a `PAGE_SIZE` bytearray.
  - Header: `num_slots`, `free_end`. Slot directory grows forward from the header;
    record data grows backward from the page end.
  - Interface: `insert(record)->slot|None`, `get(slot)`, `delete(slot)` (tombstone),
    `live_slots()`, `items()`, `to_bytes()`.

## Disk + cache (`disk_manager.py`, `buffer_pool.py`)

- **`DiskManager`** — the database as a flat page array in one file.
  - `allocate_page()->id`, `read_page(id)->bytes`, `write_page(id, bytes)`,
    `flush()` (`os.fsync`), `num_pages`, and `reads`/`writes` counters.
  - `":memory:"` mode uses an in-memory buffer for tests.
- **`BufferPool`** — bounded frames over the disk manager.
  - Data structures: `frames` (each holds a `Page`, `dirty`, `pin_count`,
    `ref_bit`), a `page_id -> frame` table, and a clock `hand`.
  - Interface: `new_page()`, `fetch_page(id)`, `unpin_page(id, dirty)`,
    `flush_page`, `flush_all`, `hits`/`misses`/`hit_ratio`.
  - Invariant: a pinned page (`pin_count > 0`) is never evicted. Eviction uses
    clock-sweep: skip pinned frames, give a second chance to frames whose
    `ref_bit` is set (clearing it), evict the first frame with a clear bit.

## Records + index (`heap.py`, `btree.py`)

- **`HeapFile`** — a table's rows across pages, addressed by `RID(page_id, slot)`.
  - `insert(record)->RID` (tries the tail page, else allocates a new one),
    `get(rid)`, `delete(rid)`, `update_in_place`, `scan()`.
  - Holds `page_ids`; through the buffer pool it pins a page, mutates it, and
    unpins it dirty.
- **`BPlusTree`** — in-memory B+ tree, `key -> RID`.
  - `_Leaf` (sorted `keys`, parallel `values`, `next` sibling) and `_Inner`
    (routing `keys`, `children`).
  - `insert` (split overflowing nodes), `search`, `range(low, high)` (walks the
    leaf chain), `delete` (borrow/merge on underflow). `unique=True` enforces a
    primary key; `DuplicateKeyError` on violation.

## Catalog (`catalog.py`)

- **`Table`** ties a relation together: `schema`, `heap`, `indexes` (a dict of
  `_Index`), and `stats` (`TableStats`: `row_count`, `ndv`).
  - `insert_row(values)` / `delete_by_rid` / `delete_by_values` / `delete_by_key`
    keep the heap and **all** indexes consistent and update stats.
  - `index_lookup(col, value)` / `index_range(col, lo, hi)` resolve RIDs;
    `get_by_pk`, `scan`, `create_index`, `analyze`.
  - `_Index`: a unique index stores `value -> RID`; a non-unique index uses a
    composite `(value, page, slot)` key so duplicates coexist and equality is a
    prefix range.
- **`Catalog`**: registry of `Table`s — `create_table`, `get_table`, `drop_table`,
  `table_names`.

## SQL frontend (`sql.py`)

- `tokenize(sql) -> [Token]` then `Parser` (recursive descent) → AST dataclasses:
  `CreateTable`, `CreateIndex`, `Insert`, `Select`, `Delete`, `Explain`,
  `Begin`/`Commit`/`Rollback`, with expression nodes `ColumnRef`, `Literal`,
  `Comparison`, `And`, `Or`.
- Expression precedence is encoded in the call chain `_or → _and → _cmp →
  _primary`, so `OR < AND < comparison`.

## Planning + execution (`plan.py`, `executor.py`)

- **`plan.optimize(select, catalog) -> Project`** builds the physical tree:
  classify `WHERE` conjuncts (local / join / constant), choose each relation's
  access path (`choose_access_path`), order joins greedily by estimated
  cardinality, and annotate `est_rows`/`est_cost`. `format_plan` renders EXPLAIN.
- **`executor`** operators implement `open()/next()/close()` over `Row` objects
  (`values` + `rids`); `materialize(project) -> Result`. It also holds the DDL/DML
  executors and a naive plan builder used as a baseline in tests.

## Concurrency + durability (`lock_manager.py`, `transaction.py`, `wal.py`)

- **`LockManager`**: `acquire(txn, resource, mode)` / `release` / `release_all`;
  a `Condition` guards `_granted` and `_waiting`; `_build_wait_for` + a DFS detect
  cycles and raise `DeadlockError`.
- **`Transaction`** (strict 2PL) + **`TransactionManager`** (ids, active set).
- **`WriteAheadLog`**: `append(record)`, `flush()` (fsync), `read_all()`;
  `replay(records, catalog)` re-applies committed transactions on recovery.

## The facade (`engine.py`)

`Database.execute(sql)` parses, dispatches, and orchestrates locks + WAL:

- Reads take shared locks (transient txn in autocommit); writes take exclusive
  locks, apply to the catalog, and log to the WAL.
- `BEGIN`/`COMMIT`/`ROLLBACK` manage `current_txn`; `COMMIT` fsyncs the WAL.
- On open, `_rebuild_state()` truncates the heap file and replays committed WAL
  records. `crash()` simulates a crash (no flush/commit). On deadlock or rollback,
  the victim/aborted transaction's effects are discarded by rebuilding state.

## LSM engine (`lsm.py`) — Track C

Independent of the heap stack: `LSMTree` (MemTable + WAL → SSTables, newest→oldest
reads, size-tiered compaction), `SSTable` (immutable sorted run + index + Bloom
filter, lazy reads), `BloomFilter` (double hashing). See
[`MODULES.md`](MODULES.md) for its interface.
