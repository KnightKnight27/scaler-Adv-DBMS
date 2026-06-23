# MiniDB Architecture

## Layered overview

```
            SQL text
               │
        ┌──────▼───────┐   parser/        Lexer -> tokens -> recursive-descent Parser -> AST
        │   Parser     │
        └──────┬───────┘
               │ Statement (AST)
        ┌──────▼───────┐   planner/        cost-based: scan choice, join order
        │  Optimizer   │   execution/      Volcano operators (open/next/close)
        └──────┬───────┘
               │ operator tree
        ┌──────▼───────┐   execution/executor.cpp
        │   Executor   │   pulls tuples; runs DML directly; logs to WAL
        └──────┬───────┘
               │ put / get / erase / scan / range
        ┌──────▼─────────────────────────────────┐   engine/storage_engine.h
        │            StorageEngine                │   one interface, two engines
        │   RowStoreEngine  │   LsmEngine          │
        │  (heap + B+Tree)  │ (MemTable+SSTables)  │
        └──────┬────────────┴───────────┬──────────┘
               │ pages                   │ files
        ┌──────▼───────┐                 │
        │  BufferPool  │ clock-sweep, pin/dirty, STEAL+NO-FORCE
        └──────┬───────┘                 │
        ┌──────▼───────┐          ┌───────▼────────┐
        │ DiskManager  │          │ SSTable files  │
        │ 1 file, 4KiB │          │ + Bloom filters│
        │ pages, CRC32 │          └────────────────┘
        └──────────────┘

  cross-cutting:  txn/      LockManager (Strict 2PL, S/X) + waits-for deadlock detection
                  recovery/ LogManager (WAL) + RecoveryManager (redo committed)
```

Everything above the `StorageEngine` line works in **tuples/records**; the buffer pool and below
work in fixed **4 KiB pages**. The slotted page is the translation point.

## Modules (`src/`)

| Dir | Contents |
|---|---|
| `common/` | `types.h` (Value, RID, PageId, sizes), `exception.h`, `config.h` |
| `storage/` | `disk_manager` (paged file I/O + CRC32), `slotted_page`, `buffer_pool` (clock-sweep), `heap_file` |
| `index/` | `bplus_page` (node accessor), `bplus_tree` (page-backed B+Tree) |
| `catalog/` | `schema`, `record` (row codec), `catalog` (persisted table/index metadata) |
| `parser/` | `token`, `lexer`, `ast`, `parser` (recursive descent) |
| `engine/` | `storage_engine.h` (interface), `rowstore_engine`, `lsm/` (memtable, sstable, bloom_filter, lsm_engine) |
| `execution/` | `iterator`, `expr_eval`, `operators` (scan/filter/project/join/aggregate), `executor` |
| `planner/` | `statistics`, `optimizer` |
| `txn/` | `transaction`, `lock_manager`, `txn_manager` |
| `recovery/` | `log_manager`, `recovery_manager` |

## Data flow of a query

1. **Parse** SQL into an AST (`parser/`).
2. **Plan** (`planner/optimizer`): pick an access method per table (table scan vs index scan, from a
   primary-key predicate and a selectivity estimate), a join algorithm (hash vs nested-loop) and
   build side, then layer filter/aggregate/project.
3. **Execute** (`execution/`): the executor opens the root operator and pulls tuples one at a time
   (Volcano model). DML (INSERT/DELETE) runs against the engine and is logged to the WAL.
4. **Storage** (`engine/` → `storage/`): the engine maps keys to rows. The row store uses a heap
   (rows by RID) + a B+Tree (key → RID); the LSM engine uses a MemTable + SSTables. Pages are cached
   in the clock-sweep buffer pool and backed by the single database file.

## On-disk formats

- **Page (4 KiB):** byte 0–3 CRC32 (stamped on write, verified on read). Slotted heap/B+Tree pages
  use the remaining bytes for a header + slot directory / node entries.
- **Heap page:** `[checksum | page_lsn | next_page_id | slot_count | free_ptr]`, slot directory
  growing up, records growing down. RID = `(page_id, slot)`.
- **B+Tree node:** `[checksum | is_leaf | key_count | next_leaf]` then compact key/RID (leaf) or
  key/child-page-id (internal) arrays. Leaves are singly linked for range scans.
- **SSTable:** sorted `[key | tombstone | row_len | row]` records, with an in-memory key→offset index
  and a Bloom filter per file.
- **Catalog:** a text sidecar (`<db>.cat`) of table schemas, heap first-page, and index roots.
- **WAL:** a text log (`<db>.wal`) of `PUT/ERASE/COMMIT` records for crash recovery.
