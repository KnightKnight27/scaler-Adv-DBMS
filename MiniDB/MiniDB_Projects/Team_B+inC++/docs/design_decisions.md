# MiniDB — Design Decisions & Trade-offs

Every non-trivial choice, as **what we chose → why → cost accepted**. The guiding principle was
a minimal, fully-understood system: implement what each component genuinely needs, and make
every trade-off explicit rather than accidental.

## Storage
- **4 KB pages.** Matches the OS block, so one page read ≈ one physical I/O (and the future
  B-tree node size). *Cost:* more pages to track for large tables.
- **Slotted page, variable-length tuples.** Real tuples vary in size, and the slot indirection
  keeps a tuple's `RowID` stable if its bytes move. *Cost:* 4 bytes/slot overhead.
- **Tombstone delete (no compaction, no slot reuse).** O(1) and simple. *Cost:* dead space until
  a (future) vacuum.
- **First-fit insert.** Simplest correct policy. *Cost:* O(#pages) scan; a free-space map would
  fix it.
- **Clock-sweep eviction.** O(1), scan-resistant via a usage cap; no linked-list upkeep like
  LRU. *Cost:* approximate recency.
- **`pin_count` + sticky `dirty` flag.** A page in use can't be evicted; a modified page is
  always written back. *Cost:* a few bytes/frame.
- **`page_id → frame` via `unordered_map`.** A hash table is the right structure; node-based
  locality is irrelevant because a miss costs a disk I/O that dwarfs the lookup, and we're
  single-threaded here. *Cost:* poor locality (a flat map / pointer-swizzling would be faster at
  scale).

## Indexing
- **B+ tree (not a plain B-tree).** Data only in leaves + a leaf chain → higher fan-out and
  O(matches) range scans. *Cost:* separator keys are duplicated.
- **In-memory, rebuilt from the heap on open.** No node serialization/page format; fully
  supports search/insert/delete + index scans. *Cost:* O(N) rebuild at startup; not a durable
  on-disk structure.
- **Integer PK keys only; lazy delete (no rebalancing).** Matches the rubric's primary index and
  keeps delete simple/correct. *Cost:* no secondary indexes; the tree can become under-full.

## Query execution
- **Volcano (pull) iterator model.** Operators compose uniformly and pipeline. *Cost:* a virtual
  call per row (negligible); base scans and the join's inner side are materialized.
- **Recursive-descent parser.** Readable, full control, yields a real AST. *Cost:* hand-coded
  precedence.
- **Nested-loop equi-join, inner materialized.** The simplest obviously-correct join. *Cost:*
  O(n·m); no hash/merge join.
- **Row serialization: values-only, fixed schema order** (INT raw, TEXT length-prefixed).
  Compact; the schema gives bytes meaning. *Cost:* not portable across architectures.

## Optimizer
- **Heuristic selectivity refined by PK uniqueness** (`pk = const` → `1/N`). No statistics to
  collect. *Cost:* inaccurate for skew/ranges — but a residual filter keeps results correct, so
  only performance is affected.
- **Cost-based scan choice** (`SeqScan = N` vs `IndexScan = matches + log₂N`). *Cost:* index
  scans only for the PK (our only index).
- **Join order = smaller relation as the materialized inner.** Minimizes the inner's memory.
  *Cost:* no predicate pushdown into joins; two tables only.

## Transactions & concurrency (Track B)
- **One engine, MVCC/2PL mode switch, differing only in `read()`.** Guarantees the benchmark
  compares the same code path. *Cost:* a branch in `read()`.
- **Snapshot = own txid (snapshot isolation).** Simple; gives the headline MVCC behaviour.
  *Cost:* MVCC mode is SI, not serializable (write-skew possible); the 2PL mode is serializable.
- **Abort via visibility (no heap undo).** An aborted txn is never "committed", so its versions
  become invisible automatically. *Cost:* old/aborted versions linger (no GC).
- **Single lock-manager mutex + CV.** One obviously-correct lock order, no latch-deadlock. *Cost:*
  a serialization point a production engine would partition.

## Recovery
- **Row-level logical WAL** (after-image for INSERT, before-image for DELETE). Simple,
  schema-agnostic, idempotent REDO via the index. *Cost:* not page-precise like ARIES.
- **Flush at COMMIT; REDO committed + UNDO losers (steal/no-force).** Matches our stealing,
  non-forcing buffer pool. *Cost:* both passes to implement.
- **`std::_Exit` to simulate a crash.** Deterministic, in-process. *Cost:* a simulation
  (equivalent to `kill -9`).
- **OS `flush()`, not `fsync`; no checkpoints; catalog not persisted.** Keeps the code minimal.
  *Cost:* not power-loss durable; unbounded log replay; `RECOVER` needs the tables re-declared.

## Build
- **Static linking (`-static`).** Sidesteps a libstdc++/libgcc DLL resolution failure on one
  MSYS2 toolchain and yields a portable binary. *Cost:* larger binary (irrelevant on Linux).
