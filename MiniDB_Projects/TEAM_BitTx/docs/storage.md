# Storage Layer

## Page Format (4KB)

```mermaid
graph LR
    Header["PageHeader (32B)"] --> SlotArray["SlotArray (2B/entry)"] --> FreeSpace["Free Space"] --> Tuples["Tuples (Grows backward)"]
```

- PageHeader: pageId, numSlots, freeSpaceStart, freeSpaceEnd, lsn
- SlotArray grows from end of header (fixed offsets from page start)
- Tuples grow forward from header end; slot offset+length pointer

## Heap File

- Per-table file (e.g. `/tmp/users.tbl`)
- First page = metadata (schema, numPages)
- Subsequent pages = data
- Insert: scan slots for free space, append tuple, write slot entry
- Delete: mark slot empty (tombstone), reclaim on next insert or vacuum

## Disk Manager

- `AllocatePage()` — returns pageId, writes zeros if first allocation
- `ReadPage(pageId, buf)` / `WritePage(pageId, buf)` — seek+read/write 4KB
- `DeallocatePage(pageId)` — adds to free list for reuse
- Per-file mutex; single-writer (no intra-file concurrency)

## Free List

`unordered_map<int32_t, int32_t>` — pageId → pageId. Reused before extending file.

## Buffer Pool (32 frames)

- LRU eviction
- Dirty pages flushed on evict and on shutdown
- pin/unpin reference counting
- Lock-protected access (`mu_`)

## B+ Tree

- Leaf chain via `nextLeaf` slot
- Internal nodes route on key range
- Currently leaf-only with linear scan for `Get`; split logic stubbed