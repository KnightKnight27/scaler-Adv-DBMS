# MiniDB Architecture Notes

This document complements the architecture diagram in the README with the
key design decisions and the rationale behind them.

## Layering
MiniDB is built bottom-up in clear layers, each usable and testable on its own:

1. **Page** (4KB slotted page) — the unit of storage and I/O.
2. **DiskManager** — moves raw pages between memory and a heap file.
3. **BufferPool** — caches pages with LRU eviction; the only component that
   talks to the DiskManager during normal operation.
4. **Table** — presents a heap file as a logical relation; every mutation is
   WAL-logged before the page changes.
5. **BPlusTree** — an ordered key→RID index used for fast equality lookups.
6. **Catalog** — metadata (schemas, indexes, statistics) and the entry point
   for resolving table names.
7. **Parser → Optimizer → Operators** — the query pipeline.
8. **TransactionManager + LockManager** — isolation via strict 2PL.
9. **WAL + RecoveryManager** — durability and atomicity.
10. **MvccStore** — the concurrency extension (snapshot isolation).

## Key Design Decisions

### One heap file per table
Mixing multiple tables in a single heap file means a page cannot be
deserialized without first knowing which table it belongs to. Giving each table
its own heap file + buffer pool makes every page unambiguous and keeps the
storage code simple. Trade-off: more open files; irrelevant at this scale.

### Volcano (iterator) execution
Operators expose open/next/close and pull rows from their children. This is how
real engines compose scans, filters, and joins uniformly, and it makes the
optimizer's job simply "assemble the right operator tree".

### Cached statistics for the optimizer
The optimizer needs row counts and distinct-value estimates. Re-scanning the
table at plan time would make an "index lookup" pay a full scan during planning.
Instead the Catalog maintains these statistics incrementally on insert/delete,
mirroring how production systems keep cached stats (ANALYZE).

### Strict 2PL with wait-for deadlock detection
Holding all locks until commit gives serializable, recoverable schedules.
Deadlocks are detected (not merely timed out) via a wait-for graph cycle check,
so the demo deterministically shows a victim being aborted.

### WAL before page mutation
The write-ahead rule (log first, then change the page) is what makes recovery
possible. The log is flushed on every append so a COMMIT record is durable
before commit returns.

### MVCC as a focused store
Snapshot isolation is implemented in a dedicated MvccStore so the visibility and
conflict rules are crisp and easy to defend, rather than being entangled with
the on-disk page format. The same timestamp/transaction concepts from the core
engine carry over directly.
