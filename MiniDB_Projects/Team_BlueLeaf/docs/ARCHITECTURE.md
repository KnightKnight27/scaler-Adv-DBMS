# MiniDB Architecture

## Layered overview

```
   SQL text
     |
   Parser          parser/      lexer makes tokens, recursive-descent parser builds an AST
     |  (AST)
   Optimizer       planner/     cost-based: scan choice and join order
     |  (operator tree)
   Executor        execution/   Volcano operators (open/next/close); runs DML; logs to the WAL
     |  put / get / erase / scan / range
   StorageEngine   engine/storage_engine.h   one interface, two engines:
     |                 RowStoreEngine = heap file + B+Tree
     |                 LsmEngine      = MemTable + SSTables
   BufferPool      storage/     clock-sweep, pin/dirty, STEAL + NO-FORCE   (row store path)
     |
   DiskManager     storage/     one file, 4 KiB pages, CRC32 per page
                                (the LSM engine writes its own SSTable files instead)

   Alongside these:
     txn/        LockManager (Strict 2PL, shared/exclusive) + waits-for deadlock detection
     recovery/   LogManager (WAL) + RecoveryManager (redo committed transactions)
```

Everything above the StorageEngine line works in tuples and records. The buffer pool and below work
in fixed 4 KiB pages. The slotted page is the translation point.

## Modules (src/)

| Dir | Contents |
|---|---|
| common/ | types.h (Value, RID, PageId, sizes), exception.h, config.h |
| storage/ | disk_manager (paged file I/O plus CRC32), slotted_page, buffer_pool (clock-sweep), heap_file |
| index/ | bplus_page (node accessor), bplus_tree (page-backed B+Tree) |
| catalog/ | schema, record (row codec), catalog (persisted table/index metadata) |
| parser/ | token, lexer, ast, parser (recursive descent) |
| engine/ | storage_engine.h (interface), rowstore_engine, lsm/ (memtable, sstable, bloom_filter, lsm_engine) |
| execution/ | iterator, expr_eval, operators (scan/filter/project/join/aggregate), executor |
| planner/ | statistics, optimizer |
| txn/ | transaction, lock_manager, txn_manager |
| recovery/ | log_manager, recovery_manager |

## Data flow of a query

1. Parse SQL into an AST (parser/).
2. Plan (planner/optimizer): pick an access method per table (table scan vs index scan, from a
   primary-key predicate and a selectivity estimate), a join algorithm (hash vs nested-loop) and the
   build side, then layer filter, aggregate, and project on top.
3. Execute (execution/): the executor opens the root operator and pulls tuples one at a time (the
   Volcano model). DML (INSERT/DELETE) runs against the engine and is logged to the WAL.
4. Storage (engine/ then storage/): the engine maps keys to rows. The row store uses a heap (rows by
   RID) plus a B+Tree (key to RID); the LSM engine uses a MemTable plus SSTables. Pages are cached in
   the clock-sweep buffer pool and backed by the single database file.

## On-disk formats

- Page (4 KiB): bytes 0 to 3 hold a CRC32 (stamped on write, verified on read). Slotted heap and
  B+Tree pages use the remaining bytes for a header plus the slot directory or node entries.
- Heap page: [checksum | page_lsn | next_page_id | slot_count | free_ptr], with the slot directory
  growing up and records growing down. RID = (page_id, slot).
- B+Tree node: [checksum | is_leaf | key_count | next_leaf] then compact key/RID arrays (leaf) or
  key/child-page-id arrays (internal). Leaves are singly linked for range scans.
- SSTable: sorted [key | tombstone | row_len | row] records, with an in-memory key-to-offset index
  and a Bloom filter per file.
- Catalog: a text sidecar (<db>.cat) of table schemas, heap first-page, and index roots.
- WAL: a text log (<db>.wal) of PUT/ERASE/COMMIT records for crash recovery.
