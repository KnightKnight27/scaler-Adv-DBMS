# MiniDB — Architecture

This document walks the system top-to-bottom and traces the three paths a request can take:
**read**, **write**, and **recovery**.

## Layered view

```
                                   SQL text
                                      │
   QUERY LAYER  (src/query)           ▼
     Lexer ──▶ Parser ──▶ AST ──▶ Optimizer ──▶ Executor (Volcano operators)
                                                  │            │
                                   reads ─────────┘            └───────── writes
                                      │                                     │
   INDEX (src/index)                  ▼                                     ▼
     B+ tree  key ──▶ RowID                            TRANSACTIONS & RECOVERY
       (in-memory, leaf-linked)                          (src/catalog/database, src/recovery,
                                      │                    src/txn)
                                      │ RowID               Database: BEGIN/COMMIT/ROLLBACK
                                      ▼                     WAL: log before page flush
   STORAGE (src/storage, src/catalog)                       recover(): REDO + UNDO
     Catalog ──▶ HeapFile (slotted pages)                   MVCC + 2PL engine (Track B)
       ──▶ BufferPool (clock-sweep, pin/dirty)                       │
       ──▶ DiskManager ──▶ <table>.db                                │ apply (heap + index + WAL)
                                      ◀──────────────────────────────┘
```

## Module responsibilities

| Module | Files | Responsibility |
|--------|-------|----------------|
| Common | `common/{types,value,config}.hpp` | `PageID`, `RowID{page,slot}`, `Value`(int\|string), `PAGE_SIZE=4096` |
| Disk manager | `storage/disk_manager.*` | the only file I/O; read/write/allocate whole pages by id |
| Slotted page | `storage/page.hpp` | tuple layout within a 4 KB page (slot directory + tuple region) |
| Buffer pool | `storage/buffer_pool.*` | cache pages; clock-sweep eviction; pin count; dirty write-back |
| Heap file | `storage/heap_file.*` | row store: `insert`→`RowID`, `get`/`erase`/`scan` |
| B+ tree | `index/bplus_tree.*` | `key`→`RowID`, split-on-insert, leaf-linked range scans |
| Schema | `catalog/schema.*` | column list + row (de)serialization (INT=4B, TEXT=len-prefixed) |
| Catalog | `catalog/catalog.*` | `name`→`Table{schema, heap, index, row_count}`; rebuilds index on open |
| Database | `catalog/database.*` | session: catalog + WAL + current transaction; `recover()` |
| Query | `query/{lexer,parser,ast,optimizer,executor}.*` | SQL → plan → results |
| Transactions | `txn/{transaction,transaction_manager}.*` | MVCC + 2PL + deadlock detection (Track B) |
| WAL | `recovery/wal.*` | append-only write-ahead log + reader |

## Key types

- `PageID` — page index in the file; `offset = id × PAGE_SIZE`.
- `RowID = {PageID page, uint16 slot}` — a tuple's permanent address; what the index stores.
- `Value = variant<int, string>`; a row is `vector<Value>`, serialized by its `Schema`.
- `TxID` — transaction id; `0` is the genesis transaction that owns rows loaded from disk.

## Read path — `SELECT name FROM emp WHERE id = 3`

1. **Lex/parse** → AST: projection `[name]`, table `emp`, predicate `id = 3`.
2. **Optimize** → `id` is the PK and `= 3` is selective → choose `IndexScan over [3,3]`; wrap a
   residual `Filter(id=3)`; wrap `Project(name)`.
3. **Execute (pull):** `Project.next()` ← `Filter.next()` ← `IndexScan.next()`.
   - `IndexScan.open()` asks the **B+ tree** for `RowID`s in `[3,3]`.
   - For each `RowID`, the **HeapFile** fetches the page via the **BufferPool** (loading from
     the **DiskManager** on a miss), and the **SlottedPage** returns the tuple bytes.
   - The **Schema** deserializes bytes → a row; `Project` keeps the `name` column.

## Write path — `INSERT INTO emp VALUES (3, 'Sandip', 30, 10)`

1. Parse → `InsertStmt`. The **Database** checks types and primary-key uniqueness (via the
   index).
2. It serializes the row, **appends an `INSERT` record to the WAL (write-ahead)**, then applies
   the change: `HeapFile::insert` → `RowID`, `BPlusTree::insert(pk, RowID)`, `row_count++`.
3. The change is bracketed in a transaction (an explicit `BEGIN…COMMIT`, or an implicit
   auto-commit). **`COMMIT` flushes the WAL** (durability point); `ROLLBACK` reverses the
   applied writes using the recorded before/after info.

## Recovery path — restart after a crash

1. The buffer pool's dirty pages may not have reached disk, but every committed transaction's
   WAL records were flushed at commit.
2. On `RECOVER`, the **Database** reads the WAL, marks transactions with a `COMMIT` record as
   committed, then **REDOes** committed `INSERT`/`DELETE` (idempotently) and **UNDOes** any
   loser op that reached the heap — leaving the database reflecting exactly the committed work.

## Concurrency (Track B)

The `TransactionManager` is a standalone, thread-safe engine demonstrating MVCC + Strict 2PL +
deadlock detection. `benchmarks/bench_mvcc_vs_2pl.cpp` drives it from many threads to compare
read throughput in MVCC vs 2PL mode under contention. (The single-connection SQL REPL uses the
WAL-backed `Database` for atomicity/durability; the engine is where concurrent isolation is
exercised and measured.)
