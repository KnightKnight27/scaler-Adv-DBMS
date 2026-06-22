# MiniDB Architecture

## Layered overview

```
                 SQL text  ("SELECT name FROM emp WHERE id = 3;")
                                   │
   ┌───────────────────────────────▼───────────────────────────────┐
   │  QUERY LAYER  (src/query)                                       │
   │    Lexer ─▶ Parser (recursive descent) ─▶ AST                   │
   │          ─▶ Optimizer (selectivity, scan choice, join order)    │
   │          ─▶ Executor (Volcano pull operators)                   │
   │             SeqScan · IndexScan · Filter · Project · NLJoin     │
   └───────────────┬───────────────────────────────┬────────────────┘
       reads/scans  │                               │  writes (INSERT/DELETE)
                    ▼                               ▼
   ┌────────────────────────┐        ┌──────────────────────────────────┐
   │  INDEX  (src/index)    │        │  SESSION & RECOVERY (src/catalog,  │
   │  B+ tree (key ▶ RowId) │        │                       src/recovery)│
   │  leaf-linked ranges    │        │  Database: BEGIN/COMMIT/ROLLBACK   │
   └───────────┬────────────┘        │  WAL: log ─▶ recover() REDO/UNDO   │
               │ RowId               └───────────────┬────────────────────┘
               ▼                                     │ apply
   ┌─────────────────────────────────────────────────▼─────────────────┐
   │  STORAGE ENGINE  (src/storage, src/catalog)                        │
   │  Catalog ─▶ HeapFile (slotted pages) ─▶ BufferPool (clock-sweep)    │
   │                                       ─▶ DiskManager ─▶ <db>.dat    │
   └────────────────────────────────────────────────────────────────────┘

   CONCURRENCY ENGINE  (src/txn, Track B) — MVCC + 2PL TransactionManager,
   exercised by the benchmark and concurrency demo.
```

## Modules (`src/`)

| Directory     | Files                                             | Responsibility |
|---------------|---------------------------------------------------|----------------|
| `common/`     | `config`, `types`, `value`                        | `PageId`, `RowId`, `TxnId`, `Value` (INT/TEXT), `PAGE_SIZE` |
| `storage/`    | `disk_manager`, `page`, `buffer_pool`, `heap_file`| durable, cached, slotted row store |
| `index/`      | `bplus_tree`                                      | primary-key index → `RowId`, range scans |
| `catalog/`    | `schema`, `catalog`, `database`                   | table registry, row (de)serialization, session + transactions |
| `query/`      | `lexer`, `parser`, `ast`, `optimizer`, `executor` | SQL → plan → results |
| `txn/`        | `transaction`, `transaction_manager`              | MVCC + 2PL concurrency engine (Track B) |
| `recovery/`   | `wal`                                             | write-ahead log + crash recovery |
| `main.cpp`    | —                                                 | SQL REPL (and the `CRASH` command) |

## Data flow

1. **Read path.** SQL text → tokens → AST → operator tree. The optimizer picks
   each table's access method (sequential vs primary-key index) and the join
   order. Operators pull rows one at a time; base operators read tuples from
   the heap through the buffer pool, resolving `RowId`s via the B+ tree on an
   index scan.
2. **Write path.** `INSERT`/`DELETE` go through the `Database` session. Each
   change is appended to the **WAL** *before* it is applied to the heap and
   index (write-ahead). Statements run inside an explicit `BEGIN/COMMIT/
   ROLLBACK` transaction or are auto-committed individually.
3. **Recovery.** On startup the catalog (schemas + page lists) is loaded, the
   index is rebuilt from the heap, and `recover()` replays the WAL: committed
   operations are REDOne and uncommitted ("loser") operations are UNDOne, both
   idempotently keyed by primary key.
4. **Concurrency (Track B).** The `TransactionManager` is a self-contained
   multi-threaded engine over an in-memory key/value store with two modes —
   MVCC and strict 2PL — used to demonstrate and benchmark the concurrency
   trade-off head-to-head.
