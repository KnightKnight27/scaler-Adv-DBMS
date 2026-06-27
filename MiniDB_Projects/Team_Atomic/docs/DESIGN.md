# MiniDB — Design Decisions & Trade-offs

This document records *why* MiniDB is built the way it is — the engineering
choices and the trade-offs behind them. (For the what/how, see the top-level
`README.md`.)

## Module dependency order

```
common  →  storage  →  record  →  index ─┐
                                          ├─→ catalog → exec → optimizer → engine → shell
            txn ──────────────────────────┘                                  ↑
            recovery ─────────────────────────────────────────────────────────┘
            lsm  (independent storage engine, used by benchmarks)
```

Each layer depends only on the ones above it; there are no cycles. This keeps
every component unit-testable in isolation (`tests/test_*.cpp` mirror the
layers).

## Key decisions

**One 4 KB page file, accessed only through the buffer pool.** Every on-disk
structure (heap pages, B+ tree nodes, index header) is a 4 KB page in a single
file. Centralising I/O behind `BufferPoolManager` means there is exactly one
place that does eviction, dirty write-back, and pinning — the rest of the engine
never touches the disk directly. Trade-off: a uniform page size is simple but
not optimal for very large values.

**RIDs are stable; deletes are tombstones.** A row keeps its `(page_id, slot)`
for life, so index entries never dangle. The cost is space: deleted slots are
not reclaimed until (future) compaction. We accepted this because stable RIDs
make the index dramatically simpler and safer.

**Whole-node B+ tree updates.** Nodes are deserialized into vectors, modified,
and rewritten. This is slower than in-place byte surgery but is far easier to
get *correct* (splits, root creation, range scans), which matters more for a
capstone graded on correctness and explainability.

**Delete without rebalance.** Implementing merge/redistribute correctly is the
hardest part of a B+ tree and adds little to a read-mostly demo. We remove leaf
entries only and document the resulting sparsity as a limitation — a deliberate
scope cut.

**Cost-based, but deliberately simple cost model.** `seq_cost ≈ rows` vs
`idx_cost ≈ log2(rows) + matches` captures the real decision (is the predicate
selective enough to beat a scan?) without pretending to model buffer-pool hit
rates we cannot measure. The plan string surfaces the numbers so the decision is
inspectable in a viva.

**Strict 2PL, youngest-victim deadlock policy.** Strict 2PL gives
serializability with no cascading aborts. Choosing the *youngest* transaction as
the deadlock victim guarantees the oldest always eventually completes (no
starvation). Detection is exact (cycle search) rather than timeout-based, so the
demo is deterministic.

**Redo+undo WAL, idempotent by primary key.** Rather than full ARIES with page
LSNs, MiniDB logs *logical* row operations and makes both redo and undo
idempotent via a primary-key presence check. This survives partial flushes (a
dirty page may or may not have reached disk before the crash) while keeping the
recovery code short and explainable. Trade-off: logical redo needs a PK index,
so transactional tables must declare a primary key.

**LSM integrated behind a `RowStore` interface.** Track C asks us to *compare*
LSM against B+ tree storage. We kept the LSM code focused
(MemTable/SSTable/compaction) and then placed both engines behind one
`RowStore` interface (`FullScan / RangeScan / Point / Insert / Delete`). The
executor, optimizer, transaction layer, and WAL recovery all talk to
`RowStore`, so a table created `USING LSM` runs the exact same SQL paths as a
heap table — the benchmark stays a fair apples-to-apples comparison, and the
engine choice is a one-word DDL change. The seam means a third engine could be
added without touching the SQL layers.

## What we'd verify in a viva

- *Why does an INSERT check the index before writing the heap?* So a rejected
  duplicate key leaves no orphan row (a bug we caught and fixed).
- *Why is recovery idempotent?* Because eviction may have flushed some committed
  pages and not others; replay must be safe to re-apply.
- *Why is the LSM faster to write but slower to read?* Sequential batched
  flushes vs random page writes; reads must merge across runs (mitigated by
  Bloom filters and compaction).
- *Why youngest-victim?* Starvation freedom for older transactions.
