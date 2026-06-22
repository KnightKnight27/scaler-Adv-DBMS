# MiniDB Architecture

## Module Map

```
┌─────────────────────────────────────────┐
│                  Client                  │
│           (REPL / batch .sql)            │
└─────────────────┬───────────────────────┘
                  │ SQL string
                  ▼
┌─────────────────────────────────────────┐
│         TransactionManager              │
│  ┌────────────┐  ┌────────────────────┐ │
│  │LockManager │  │    MvccStore       │ │
│  │  (2PL)     │  │(version chains)    │ │
│  └────────────┘  └────────────────────┘ │
│              WAL (write-ahead log)       │
└─────────────────┬───────────────────────┘
                  │ parsed Statement
                  ▼
┌─────────────────────────────────────────┐
│              Executor                    │
│  ┌──────────┐   ┌────────────────────┐  │
│  │ Parser   │   │   Optimizer        │  │
│  │(rec-desc)│   │(cost-based, table/ │  │
│  └──────────┘   │ index scan choice) │  │
│                 └────────────────────┘  │
└───────────────────┬─────────────────────┘
                    │
          ┌─────────┴─────────┐
          ▼                   ▼
┌─────────────────┐  ┌─────────────────────┐
│   HeapTable     │  │    BPlusTree         │
│(slotted pages)  │  │ (in-memory, order 4) │
└────────┬────────┘  └─────────────────────┘
         │
┌────────┴────────┐
│   BufferPool    │  LRU, 64 frames
│   (LRU cache)   │
└────────┬────────┘
         │
┌────────┴────────┐
│   DiskManager   │  one file per table (.db)
│  (file I/O)     │  + WAL file (.wal)
└─────────────────┘
```

## Data Flow: INSERT

1. Client sends `INSERT INTO t VALUES (1, 'a')`
2. TransactionManager logs `BEGIN` + acquires write lock (2PL) or records snapshot ts (MVCC)
3. Parser produces `InsertStmt{table="t", values=[1,"a"]}`
4. Executor calls `HeapTable::insert_row([1,"a"])`
5. HeapTable serialises row → bytes; BufferPool fetches write page; Page::insert writes slot
6. BufferPool marks page dirty; DiskManager writes page on unpin
7. BPlusTree index updated: key=1 → RID{page_id, slot_id}
8. WAL appends INSERT record; on COMMIT, WAL flushes to disk and locks released

## Data Flow: SELECT with index

1. `SELECT * FROM t WHERE id = 5`
2. Optimizer sees `id` is the primary key column → chooses INDEX_POINT
3. BPlusTree::search(5) returns RID in O(log n)
4. HeapTable::read_row(rid) fetches page from BufferPool and decodes the row
5. ResultSet returned to client and printed as ASCII table

## Extension Track B: MVCC

Each INSERT creates a RowVersion `{data, begin_ts=commit_ts, end_ts=∞}`.
Each DELETE seals the current version: `end_ts = current_ts`.

A SELECT inside a transaction with `snapshot_ts = T` sees only versions where:

```
  version.begin_ts ≤ T  AND  T < version.end_ts
```

This means:
- Readers see a consistent point-in-time view of the database
- Writers never block readers (no shared lock needed)
- Readers never block writers (no reader holds an exclusive lock)

Contrast with 2PL where a writer's exclusive lock forces all readers to wait.
