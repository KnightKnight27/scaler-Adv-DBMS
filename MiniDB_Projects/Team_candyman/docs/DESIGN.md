# MiniDB — Design Notes & Trade-offs

Companion to the README, focused on *why* each design decision was made. These
are the talking points for the viva.

## The central abstraction: `storage.StorageEngine`

Both the heap engine and the LSM engine implement one narrow, primary-key-keyed
interface (`CreateTable / Put / Get / Delete / Scan / RangeScan / Sync / Close`).
Everything above it — SQL, planner, executor, transactions, recovery — is written
once and runs unchanged on either engine. This is what makes the Track C
extension a *drop-in* and the heap-vs-LSM benchmark an apples-to-apples
comparison. `Put` is an **upsert**, which is what keeps WAL redo idempotent.

## Storage

- **4 KiB slotted pages.** Standard layout for variable-length records. Deletes
  are tombstones so RIDs are stable — the B+Tree can safely store `key → RID`.
- **Separate data and index files.** Lets recovery rebuild indexes by truncating
  the index file without disturbing table data.
- **Buffer pool with LRU + pin/unpin.** Pinning guarantees a page in use is never
  evicted mid-operation; the write-ahead hook fsyncs the WAL before flushing any
  dirty data page.

## Indexing

- **Disk-backed B+Tree via the buffer pool**, so it exercises paging and splits
  rather than being a toy in-memory map.
- **Lazy delete (no merge).** B+Tree node merging is the most bug-prone part of
  the structure; we skip it. Because indexes are **rebuilt from the heap on
  startup**, under-full nodes never accumulate. This also removes index
  durability from the recovery critical path: indexes are *derived data*.

## Optimizer

- Keep it honest and explainable: real selectivity constants, a real
  index-vs-seq decision (`IndexScan` for PK equality), and a greedy
  smallest-first join order. `EXPLAIN` surfaces the estimates so the choice is
  visible and defensible.

## Transactions

- **Strict 2PL** for serializable isolation. **Table-granularity** locks at the
  SQL layer: simple and provably correct (per table: many readers or one
  writer), and it keeps the shared B+Tree/heap structures safe under concurrency.
  The lock manager itself is resource-name-generic, so finer granularity is a
  small change.
- **Wait-for-graph deadlock detection** with victim abort, rather than timeouts —
  deterministic and demonstrable.

## Recovery — why redo *and* undo

We use **immediate apply (steal) + no-force**: changes hit the engine as they
happen (so reads see your own writes naturally), and committed pages are not
force-flushed. That policy requires both:

- **redo** committed transactions whose pages weren't flushed before the crash,
- **undo** uncommitted transactions whose pages *were* flushed (stolen).

Logical, row-level logging with before/after images supplies both, and both are
idempotent (upsert / delete-if-present), so recovery can itself be interrupted
and re-run. The main simplification vs ARIES is that we don't physically log heap
*structural* changes or use per-page LSNs/CLRs; the scan defends against torn
pages, and DDL force-syncs the heap root so a table's chain is always rooted in a
valid page.

The alternative — deferred apply (no-steal) + redo-only — is simpler (no undo)
but breaks read-your-own-writes inside a transaction, so we did not use it.

## Extension: LSM-tree

- **Map-based MemTable** gives O(1) writes — the whole point of the structure.
  Sorting is deferred to flush/scan time.
- **Bloom filter + sorted index per SSTable** so a negative point lookup usually
  avoids touching the file at all.
- **Size-tiered compaction** (merge all runs once ≥ 4) is the simplest policy
  that still demonstrates merging, newest-wins resolution, tombstone collection,
  and space reclamation.
- Durability reuses the engine-agnostic WAL plus persisted SSTables, so the LSM
  engine needs no second logging mechanism.
