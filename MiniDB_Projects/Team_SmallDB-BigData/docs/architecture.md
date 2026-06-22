# MiniDB — Architecture

This file is the guided tour. The README says *what* each part does; here we follow a request all
the way through the system, three different ways: a **read**, a **write**, and a **recovery**.

## The layers, top to bottom

```
                              SQL text
                                 |
   QUERY LAYER  (src/query)      v
     Lexer  ->  Parser  ->  AST  ->  Optimizer  ->  Executor (pull operators)
                                              |              |
                               reads ---------+              +--------- writes
                                  |                                       |
   INDEX (src/index)              v                                       v
     B+ tree:  key -> RowID                          TRANSACTIONS & RECOVERY
       (in memory, leaves linked)                      (src/catalog/database, src/recovery, src/txn)
                                  |                       Database: BEGIN / COMMIT / ROLLBACK
                                  | RowID                 WAL: log the change before applying it
                                  v                       recover(): REDO winners + UNDO losers
   STORAGE (src/storage, src/catalog)                     MVCC + 2PL engine (Track B)
     Catalog -> HeapFile (slotted pages)                          |
       -> BufferPool (clock-sweep, pin/dirty)                     | apply: heap + index + WAL
       -> DiskManager -> <table>.db   <------------------------- -+
```

## Who does what

| Part | Files | Responsibility |
|------|-------|----------------|
| Common | `common/{types,value,config}.hpp` | shared types: `PageID`, `RowID{page,slot}`, `Value` (int or text), `PAGE_SIZE = 4096` |
| Disk manager | `storage/disk_manager.*` | the only code that touches the file; read/write/grow by whole pages |
| Slotted page | `storage/page.hpp` | how rows are laid out inside one 4 KB page |
| Buffer pool | `storage/buffer_pool.*` | keep pages in memory; clock-sweep eviction; pin count; write dirty pages back |
| Heap file | `storage/heap_file.*` | the row store: `insert` -> `RowID`, plus `get`/`erase`/`scan` |
| B+ tree | `index/bplus_tree.*` | `key` -> `RowID`, split on insert, range scans along the leaf chain |
| Schema | `catalog/schema.*` | the column list and how a row is turned into bytes and back |
| Catalog | `catalog/catalog.*` | `name` -> `Table`; rebuilds the index from the heap when a table opens |
| Database | `catalog/database.*` | the session: catalog + WAL + the current transaction; `recover()` |
| Query | `query/{lexer,parser,ast,optimizer,executor}.*` | SQL -> plan -> results |
| Transactions | `txn/{transaction,transaction_manager}.*` | the MVCC + 2PL + deadlock engine (Track B) |
| WAL | `recovery/wal.*` | the write-ahead log: append records, read them back for recovery |

## A few core types

- `PageID` — which page in the file. The byte offset is just `id × 4096`.
- `RowID = {page, slot}` — a row's permanent address. This is what the index stores.
- `Value` — an `int` or a `string`. A row is a list of `Value`s, turned into bytes by its `Schema`.
- `TxID` — a transaction id. `0` is special: it's the "genesis" transaction that owns rows loaded
  from disk, so everyone can see them.

## Read path — `SELECT name FROM emp WHERE id = 3`

1. **Lex and parse** the text into an AST: pick column `name`, from table `emp`, where `id = 3`.
2. **Optimize:** `id` is the primary key and `= 3` matches one row, so the index is cheaper than a
   full scan -> choose an **index scan** over the range `[3, 3]`. We still put a `Filter(id = 3)` on
   top (so the answer is correct no matter what), then a `Project(name)` to keep just that column.
3. **Execute, pulling one row at a time:** `Project` asks `Filter`, which asks `IndexScan`.
   - The index scan asks the **B+ tree** for the `RowID`s with key in `[3, 3]`.
   - For each `RowID`, the **heap file** loads that page through the **buffer pool** (reading from
     the **disk manager** if it isn't cached), and the **slotted page** hands back the row's bytes.
   - The **schema** turns those bytes into a row, and `Project` keeps the `name` column.

## Write path — `INSERT INTO emp VALUES (3, 'Krritin', 30, 10)`

1. Parse into an `InsertStmt`. The **Database** checks the value types and that the primary key
   isn't already used (it asks the index).
2. It turns the row into bytes, **writes an `INSERT` record to the WAL first**, and only then
   applies the change: `HeapFile::insert` gives a `RowID`, `BPlusTree::insert(3, RowID)` adds it to
   the index, and `row_count` goes up by one.
3. The change lives inside a transaction — either an explicit `BEGIN ... COMMIT`, or an automatic
   one around the single statement. **`COMMIT` flushes the WAL** (that's the durability moment);
   **`ROLLBACK`** walks back the writes it made.

## Recovery path — restarting after a crash

1. When the program crashed, some changed pages may not have reached disk yet — but every
   *committed* transaction had its WAL records flushed at commit time.
2. On `RECOVER`, the **Database** reads the whole log, marks every transaction that has a `COMMIT`
   record as a winner, then **REDOes** the winners' inserts/deletes (safely, by checking the index
   first) and **UNDOes** any loser change that managed to reach the data. What's left is exactly the
   committed work.

## Concurrency (Track B)

The `TransactionManager` is a separate, thread-safe engine that shows MVCC and Strict 2PL side by
side, plus deadlock detection. `benchmarks/bench_mvcc_vs_2pl.cpp` runs it from many threads to
compare read throughput in the two modes under contention. The SQL prompt itself is a single
connection, so it uses the WAL-backed `Database` for atomicity and durability — the engine is where
real concurrency is built and measured.
