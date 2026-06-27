# Storage

Storage uses fixed-size pages and a slotted-page layout. Heap tables store variable-length encoded rows in page slots. The disk manager persists pages in a single database file, while the buffer pool caches pages with pin counts, dirty flags, and LRU victim selection.

## M1 Coverage

- Page allocation through `PageManager` and `DiskManager`.
- Page reads and writes through file-backed fixed-size pages.
- Buffer pool usage through fetch, pin, unpin, dirty tracking, flush, and eviction.
- Heap-file behavior through record insert, fetch, delete, and sequential scan.

## M1 Test Scenarios

- Records survive database close/reopen.
- Dirty pages are flushed during buffer eviction.
- Deleted heap slots are skipped by scans.
