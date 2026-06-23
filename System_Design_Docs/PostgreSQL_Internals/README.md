# PostgreSQL Internal Architecture

**Name:** Tirth Shah
**Roll Number:** 24BCS10347
**Course:** Advanced DBMS — System Design Discussion

---

## 1. Problem Background

PostgreSQL descends from the **POSTGRES** project started by **Michael Stonebraker** at UC Berkeley in 1986, itself a successor to the earlier Ingres relational system. POSTGRES set out to extend the relational model with richer types, rules, and — crucially for this document — a **no-overwrite storage manager** in which updates produce new tuple versions rather than mutating data in place. That design decision, taken for time-travel queries and crash resilience, is the direct ancestor of today's **Multi-Version Concurrency Control (MVCC)**. The SQL front-end was bolted on in 1994 ("Postgres95"), and the project became PostgreSQL in 1996.

The hard problem any general-purpose RDBMS must solve is this:

> **How do you serve many concurrent transactions safely and durably on disk-backed, fixed-size pages — without readers blocking writers, without losing committed data on a crash, and without re-reading the disk for every tuple?**

That single sentence decomposes into four subsystems, each answering one clause:

| Clause of the problem | Subsystem | Core job |
|---|---|---|
| *"…without re-reading the disk for every tuple"* | **Buffer Manager** | Cache 8 KB pages in shared memory; decide what to evict. |
| *"…fixed-size pages … efficient lookup"* | **B-Tree (nbtree)** | Ordered, logarithmic-depth, concurrent index over heap tuples. |
| *"…readers don't block writers, many concurrent transactions"* | **MVCC** | Per-tuple version visibility driven by transaction snapshots. |
| *"…durably … without losing committed data on a crash"* | **WAL** | Write-ahead logging + checkpoints + crash recovery. |

These four are not independent features layered on top of a database — they are the database. The buffer manager is where the WAL rule is *enforced* (a dirty page cannot be flushed before its log record), MVCC is what fills heap pages with multiple tuple versions that the buffer manager caches and the B-tree points at, and WAL is what makes both of them survive a power failure. The rest of this document follows the data through these layers.

---

## 2. Architecture Overview

### 2.1 Process model

PostgreSQL is a **process-per-connection** system (not threads). A single supervisor, the **postmaster**, listens on the socket, forks a dedicated **backend** for each client, and supervises a fixed set of background processes. All of them coordinate through one block of **shared memory**.

```
                         ┌───────────────────────────┐
        client ─────────▶│  postmaster (supervisor)  │  listen(), fork()
        client ─────────▶│  - accepts connections    │
        client ─────────▶│  - spawns & reaps children│
                         └─────────────┬─────────────┘
                                       │ fork()
        ┌──────────────┬───────────────┼───────────────┬──────────────────┐
        ▼              ▼               ▼               ▼                  ▼
  ┌───────────┐ ┌───────────┐  ┌──────────────┐ ┌──────────────┐ ┌──────────────────┐
  │ backend 1 │ │ backend 2 │  │ bgwriter     │ │ checkpointer │ │ autovacuum       │
  │ (parse/   │ │ ...       │  │ (drip dirty  │ │ (full flush, │ │ launcher+workers │
  │  plan/exec)│ │           │  │  buffers out)│ │  redo ptr)   │ │ (reclaim/freeze) │
  └─────┬─────┘ └─────┬─────┘  └──────┬───────┘ └──────┬───────┘ └────────┬─────────┘
        │             │               │                │                  │
        │       ┌─────────────┐       │                │                  │
        └──────▶│ WAL writer  │◀──────┴────────────────┴──────────────────┘
                │ (flush WAL  │
                │  buffers)   │
                └──────┬──────┘
                       │
        ╔══════════════▼═══════════════════════════════════════════════════╗
        ║                       SHARED MEMORY                                ║
        ║  ┌────────────────┐ ┌────────────┐ ┌──────────┐ ┌──────────────┐  ║
        ║  │ shared_buffers │ │ WAL buffers│ │  CLOG    │ │ lock tables, │  ║
        ║  │ (8KB pages)    │ │            │ │(xact     │ │ proc array,  │  ║
        ║  │  128 MB here   │ │            │ │ status)  │ │ buffer hash  │  ║
        ║  └────────────────┘ └────────────┘ └──────────┘ └──────────────┘  ║
        ╚════════════════════════════════════════════════════════════════════╝
```

Per-backend **private** memory holds `work_mem` (per sort/hash node — note the Parallel Hash in §5 reports 2432 kB usage) and `maintenance_work_mem` (for VACUUM, index build). Shared memory holds the buffer pool, WAL buffers, CLOG (commit log), the ProcArray (live transactions, used to build snapshots), lock tables, and the buffer-mapping hash table.

The background workers each own one slice of the durability/cleanup pipeline: **bgwriter** trickles dirty buffers to the OS so backends rarely have to write a victim themselves; **checkpointer** periodically forces *all* dirty buffers to disk and advances the redo pointer; **WAL writer** flushes WAL buffers so commits find their records already mostly on disk; **autovacuum** removes dead tuples and freezes old XIDs.

### 2.2 Data flow: a read and a write

```
 READ  (SELECT ... WHERE id = K)
 ─────
  backend ── B-tree descent ──▶ "I need heap block B"
       │
       ▼
  ReadBuffer(B) ── hash lookup in buffer pool
       │                         │
   HIT │                    MISS │
       ▼                         ▼
  pin & return            pick victim (clock-sweep) ─▶ if dirty, WAL-flush then write out
                                  │
                                  ▼
                          read 8KB from OS cache / disk into the buffer, pin, return

 WRITE (UPDATE / INSERT / DELETE)
 ─────
  backend modifies tuple in the pinned buffer (in shared memory only)
       │
       ▼
  emit WAL record into WAL buffers ──▶ get back an LSN
       │
       ▼
  stamp the buffer's page LSN, mark buffer DIRTY   (page NOT yet on disk)
       │
       ▼
  on COMMIT: XLogFlush(commitLSN) ── fsync WAL up to that LSN ──▶ transaction is durable
       │
       ▼
  later: checkpointer/bgwriter writes the dirty data page out (always AFTER its WAL — the WAL rule)
```

The key invariant visible above: **the data page reaches disk lazily and out of order, but its WAL record always reaches disk first.** That is what lets commit be cheap (one sequential WAL fsync) while still being durable.

---

## 3. Internal Design

### 3.1 Buffer Manager
*(source: `src/backend/storage/buffer/bufmgr.c`, `freelist.c`)*

PostgreSQL never reads a heap or index page directly into a backend's private memory. Everything goes through **shared_buffers**, an array of fixed `BLCKSZ` = 8192-byte frames (128 MB ÷ 8 KB ≈ 16 384 frames on this server).

**Buffer identity.** Each frame is described by a `BufferDesc` containing a **buffer tag**:

```
BufferTag = { relfilenode (db, tablespace, relation),
              forknum   (main / fsm / vm),
              blocknum }
```

A shared **hash table** maps `BufferTag → buffer id`. To find a page, a backend computes the tag and probes the hash table — that, not the array index, is the cache lookup.

**Pinning and usage.** Each `BufferDesc` carries:
- a **refcount / pin count** — how many backends are currently using the buffer; a pinned buffer cannot be evicted;
- a **usage_count** — a small saturating counter (0–5) bumped on each access, used as a frequency hint by the replacement policy;
- a dirty flag, the page LSN, and a content lock.

**Clock-sweep replacement** *(`freelist.c`, `StrategyGetBuffer`)*. PostgreSQL does not use strict LRU (too much contention to maintain an ordered list). Instead it uses a **clock / second-chance** sweep. A single "hand" (`nextVictimBuffer`) walks the buffer array circularly:

```
   ┌──────────────────────────── clock hand sweeps ──────────────────────────────┐
   ▼
 [buf0 u=0 pin=0]  [buf1 u=3 pin=0]  [buf2 u=0 pin=2]  [buf3 u=1 pin=0]  [buf4 u=0 ...]
        │                  │                 │                 │
   usage==0 &&        usage>0:           pinned:           usage>0:
   not pinned  ───▶   decrement to 2,    SKIP entirely,    decrement to 0,
   = VICTIM           give 2nd chance    move on           move on
```

At each frame the hand inspects it: if `pin==0 && usage_count==0`, that frame is the **victim**. Otherwise, if `usage_count > 0` it **decrements** it (a "second chance") and moves on; pinned frames are skipped. Hot pages keep getting their usage_count bumped back up before the hand returns, so they survive; cold pages decay to 0 and get evicted. This approximates LRU at far lower locking cost.

**Requesting a page — the full path (`ReadBuffer` → `BufferAlloc`):**
1. Compute the buffer tag, probe the buffer hash table.
2. **Hit:** pin the buffer (refcount++), bump usage_count, return it.
3. **Miss:** call `StrategyGetBuffer` → clock-sweep selects a victim frame.
4. If the victim is **dirty**, it must be written out — but first PostgreSQL calls `XLogFlush` up to the victim page's LSN (the WAL rule, enforced *here*), then writes the page to the OS.
5. Insert the new tag into the hash table, issue the read of block B into the frame, pin, return.

**Double buffering with the OS page cache.** PostgreSQL relies on the kernel page cache underneath shared_buffers, so a "read" on a miss is frequently a memcpy from OS cache, not a real disk seek. The §5 join shows this directly: `Buffers: shared hit=5905 read=36` — 99.4% of pages were already in shared_buffers, and the 36 "reads" were satisfied largely by the OS without physical I/O. The cost is **double buffering**: a page can live in both caches, wasting RAM. The benefit is robustness and simpler portability; this is a deliberate engineering trade (see §4).

**Ring buffers (buffer access strategies).** A large sequential scan or VACUUM that touched every frame would otherwise evict the entire working set ("cache trashing"). PostgreSQL instead confines such operations to a small **ring** of buffers (e.g. 256 KB for seq scans, configurable for VACUUM), so a 34 MB table scan does not blow away the hot index pages. This is why a Parallel Seq Scan on `orders` (§5) does not destroy cache locality.

### 3.2 B-Tree / nbtree
*(source: `src/backend/access/nbtree/` — `nbtinsert.c`, `nbtsearch.c`, `nbtpage.c`)*

PostgreSQL's default index is a **Lehman & Yao high-key B+-tree**, a variant specifically engineered for **high concurrency**: it adds a *high key* and a *right-link* to every page so that a reader descending the tree can still find its target even if a concurrent split moved data to a new right sibling.

**On-disk page layout** (every nbtree page is one 8 KB block, like every heap page):

```
 ┌────────────────────────────────────────────────────────────┐
 │ PageHeaderData (LSN, checksum, pd_lower, pd_upper, ...)     │  ← header
 ├────────────────────────────────────────────────────────────┤
 │ ItemId / line pointer array (grows DOWN ▼)                  │  ← (offset,len) per tuple
 │   lp1 → lp2 → lp3 → ...                                     │
 │                                                             │
 │            ... free space ...                               │
 │                                                             │
 │   ▲ index tuples (key + heap TID) grow UP                  │
 ├────────────────────────────────────────────────────────────┤
 │ BTPageOpaqueData (special area):                            │  ← btpo_prev, btpo_next,
 │   btpo_prev  btpo_next  btpo_flags (LEAF/ROOT/...)  high key│     btpo_flags
 └────────────────────────────────────────────────────────────┘
```

`btpo_next` / `btpo_prev` are the **right-link / left-link** chaining siblings at the same level; `btpo_flags` records whether the page is a leaf, root, deleted, etc. The `pageinspect` reading in §5 (`bt_page_stats`) shows a leaf with `live_items=169`, `avg_item_size=39`, `free_size=768` — a nearly-full leaf, consistent with sequential-ish integer keys packing tightly.

**Meta page and levels.** Block 0 is the **meta page** (`bt_metap`): magic number, version, and a pointer to the current **root** plus a cached `fastroot`. The §5 reading `root=block 290, level=2` means a **3-level tree**: level 2 (root) → level 1 (internal) → level 0 (leaves). Each non-leaf level stores **separator keys** that route the search.

```
                 ┌──────── root (level 2, blk 290) ────────┐
                 │  [k0 | k300 | k600 | ...]                │
                 └───┬───────────┬───────────┬─────────────┘
            ┌────────▼───┐  ┌────▼───────┐  ┌▼───────────┐    (level 1, internal)
            │ [.. .. ..] │  │ [.. .. ..] │  │ [.. .. ..] │
            └──┬──┬──┬───┘  └────────────┘  └────────────┘
       (level 0, leaves, chained by right-links →)
   ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐
   │ leaf A │─▶│ leaf B │─▶│ leaf C │─▶│ leaf D │─▶ ...
   └────────┘  └────────┘  └────────┘  └────────┘
   each leaf tuple = (indexed key, heap TID → (block,offset))
```

**Search path** (`_bt_search` in `nbtsearch.c`): start at the root, binary-search the separator keys to pick the child, descend, repeat until a leaf, then binary-search the leaf for the key. A lookup in this 3-level tree touches exactly 3 pages — hence "logarithmic depth." If a concurrent split has moved the target rightward past this page's **high key**, the reader simply follows `btpo_next` to the right sibling instead of restarting — the Lehman & Yao trick.

**Insertion and page splits** (`_bt_insert`, `_bt_split` in `nbtinsert.c`):
1. Descend to the correct leaf. If it has room, insert the tuple and we're done.
2. If the leaf is full, **split** it. PostgreSQL normally splits **~50/50** to keep pages half-to-fully packed, but for an **append-heavy rightmost** leaf (e.g. a monotonically increasing serial key, as `idx_orders_user_id` partly is) it uses a **fillfactor-biased split** that leaves the left page nearly full, because future inserts will go to the right — this avoids wasting half of every page on a load.
3. A split produces a new sibling; its first key is pushed **up** to the parent as a new separator. If the parent overflows, the split **propagates upward**, possibly creating a new root and raising the tree's level.
4. During the split the **right-link is set before the parent is updated**, so concurrent readers/searchers are never lost: a reader that arrives mid-split follows the right-link.

**Deduplication and index-only scans.** Since PG 13, nbtree applies **deduplication**: many index tuples with the *same key* but different heap TIDs are merged into a single "posting list" tuple, drastically shrinking indexes on low-cardinality columns. PostgreSQL also supports **index-only scans**: if every needed column is in the index *and* the heap page's visibility-map bit says "all visible," it can answer from the index alone, skipping the heap.

### 3.3 MVCC
*(source: `src/backend/access/heap/heapam.c`, `src/backend/utils/time/`)*

MVCC is the reason PostgreSQL readers never block writers and writers never block readers. The mechanism lives in the **heap tuple header**, `HeapTupleHeaderData`:

```
 ┌──────────────────────────── HeapTupleHeader ─────────────────────────────┐
 │ t_xmin   : XID that INSERTED this version                                 │
 │ t_xmax   : XID that DELETED/UPDATED it (0 = still live)                    │
 │ t_cid    : command id within the inserting/deleting transaction           │
 │ t_ctid   : (block,offset) — usually self; on UPDATE, points FORWARD to    │
 │            the new version (the "update chain" / HOT chain)                │
 │ t_infomask / t_infomask2 : hint bits (XMIN_COMMITTED, XMAX_COMMITTED,...)  │
 ├───────────────────────────────────────────────────────────────────────────┤
 │ ... user column data ...                                                  │
 └───────────────────────────────────────────────────────────────────────────┘
```

**How an UPDATE works** — *crucially, PostgreSQL never overwrites the old row in place.* It:
1. writes a **brand-new tuple version** (the new column values) into some heap page, stamped with `xmin = current_xid`;
2. sets the **old** tuple's `t_xmax = current_xid` and points its `t_ctid` at the new version's location.

This is exactly what the §5 `pageinspect` reading captures, after `UPDATE` of `id=1` in transaction 780:

```
 lp | t_xmin | t_xmax | t_ctid
  1 |  734   |  780   | (4347,67)   ← OLD version: created by xid 734, deleted by xid 780,
                                       ctid points FORWARD to the new version at (4347,67)
  2 |  734   |   0    | (0,2)        ← untouched live row (xmax=0, ctid = self)
  3 |  734   |   0    | (0,3)        ← untouched live row
```

```
   BEFORE UPDATE                          AFTER UPDATE (in txn 780)
   ──────────────                         ──────────────────────────
   page 0, lp 1                           page 0, lp 1            page 4347, off 67
   ┌───────────────────┐                  ┌───────────────────┐  ┌───────────────────┐
   │ xmin=734 xmax=0   │                  │ xmin=734 xmax=780 │─▶│ xmin=780 xmax=0   │
   │ ctid=(0,1) "self" │                  │ ctid=(4347,67)    │  │ ctid=(4347,67)    │
   │ amount=...        │                  │  (DEAD now)       │  │ amount=NEW        │
   └───────────────────┘                  └───────────────────┘  └───────────────────┘
```

A **DELETE** is the same minus the new tuple: it just stamps `t_xmax = current_xid`. A **rolled-back** transaction's xmax is simply ignored because CLOG records the abort.

**Visibility rules.** A snapshot is `{xmin (oldest active xid), xmax (next xid not yet assigned), xip[] (in-progress xid list)}`. A tuple version is **visible to a snapshot** iff:
- its **xmin is committed** (per CLOG) **and** xmin is *not* still in-progress relative to the snapshot (i.e. xmin < snapshot xmax and not in xip[]); **and**
- its **xmax is 0**, *or* the transaction in xmax **aborted**, *or* xmax is still in-progress / ≥ snapshot xmax (the delete hasn't "happened yet" for this snapshot).

Informally: *I can see a row if the transaction that made it had committed before my snapshot, and the transaction that killed it had not.* The same heap page can thus show different rows to different concurrent transactions with zero locking on reads.

**Hint bits.** Checking CLOG for every tuple on every scan would be expensive, so the first transaction to observe a committed/aborted xmin or xmax sets a **hint bit** (`XMIN_COMMITTED`, etc.) in `t_infomask`. Subsequent scans read the answer from the tuple itself and skip CLOG.

**CLOG (commit log)** *(`src/backend/access/transam/clog.c`)*: a compact shared-memory + on-disk array holding 2 bits of commit status (in-progress / committed / aborted / sub-committed) per transaction id. Visibility checks consult it; hint bits cache its answers.

**Why VACUUM is mandatory.** Because dead tuple versions are left in place, the heap accumulates **bloat**. `VACUUM`:
- **reclaims dead tuples** (the §5 `pgstattuple` shows `dead_tuple_count=1` immediately after the update — VACUUM would return that 64 bytes to free space);
- **prevents unbounded bloat** so tables and indexes stay dense (`tuple_percent=93.54%`, `free_percent=0.5%` here is healthy);
- **freezes old XIDs** to defeat **transaction-ID wraparound**. XIDs are 32-bit and cyclic; once a tuple's xmin is "old enough," VACUUM marks it **frozen** (effectively "infinitely in the past") so it stays visible even after the counter wraps. Skipping VACUUM long enough triggers wraparound-prevention shutdowns — a real production failure mode.

**Isolation levels.** Under **Read Committed** (the default — confirmed by `default_transaction_isolation=read committed` in §5), each *statement* takes a fresh snapshot, so a query sees rows committed before it started. Under **Repeatable Read** (snapshot isolation), one snapshot is taken at the first statement and reused for the whole transaction, giving a stable view. **Serializable** adds SSI (predicate-tracking) on top. All three are built from the same tuple-version + snapshot machinery — only *when the snapshot is taken* changes.

### 3.4 WAL (Write-Ahead Logging)
*(source: `src/backend/access/transam/xlog.c`, `xloginsert.c`)*

WAL is the durability backbone. Its single governing rule:

> **The Write-Ahead Rule:** a change's WAL record must be on durable storage *before* the corresponding data page is allowed to reach durable storage.

```
   modify page in buffer pool            (1) emit redo record  ──▶ WAL buffer, get LSN
        │                                                              │
        ▼                                                              ▼
   page is DIRTY, page.LSN = recordLSN                          (2) on COMMIT: XLogFlush ─▶ fsync WAL
        │                                                              │ durable now
        │   ◀──────── MUST NOT flush page before its WAL is on disk ───┘
        ▼
   (3) later) write data page out  ──  buffer mgr first ensures XLogFlush(page.LSN) happened
```

**WAL records.** Each record describes a physical/logical change as a **redo** action and carries an **LSN** (Log Sequence Number) — a monotonically increasing byte offset into the WAL stream. The page's stored LSN ties the page to the last WAL record that modified it; the buffer manager refuses to write a page whose LSN is past what has been flushed.

**Full-page writes.** The first time a page is modified after a checkpoint, PostgreSQL logs the **entire 8 KB page image**, not just the diff. This guards against **torn pages**: if the OS crashes mid-write leaving a half-written 8 KB block, recovery can restore the whole page from the WAL image. It is also why WAL volume is larger than raw row data — see §5: 10 000 rows produced **3620 kB** of WAL (~370 bytes/row, well above the row's own size, the overhead being record headers and periodic full-page images).

**WAL segments and fsync.** WAL is written to fixed-size (default 16 MB) **segment files** in `pg_wal/`, sequentially. `fsync` (or `fdatasync`) forces them to durable storage. Sequential append + a single fsync per commit batch is dramatically cheaper than fsyncing scattered data pages.

**Commit = XLogFlush.** When a transaction commits, PostgreSQL writes a commit WAL record and calls `XLogFlush(commitLSN)`, fsyncing the WAL up to that point. **Once that fsync returns, the transaction is durable** even though its data pages may still be only in shared_buffers. This is the heart of the performance story: COMMIT cost is one sequential WAL flush, not many random data-page writes. (`synchronous_commit=off` relaxes this for throughput at the cost of a small window of loss.)

**Checkpointing** *(`CreateCheckPoint` in `xlog.c`)*. A checkpoint:
1. flushes **all** currently-dirty buffers to disk;
2. records a **redo pointer** = the WAL LSN from which recovery must start;
3. trims/recycles WAL segments older than the redo pointer.

Checkpoints bound recovery time and bound how much WAL must be retained — but they cause an I/O spike (all dirty buffers at once), which is exactly why **bgwriter** trickles dirty pages out *between* checkpoints to smooth the load.

**Crash recovery (redo).** On restart after a crash, PostgreSQL reads the last checkpoint's redo pointer and **replays every WAL record from there forward** ("redo"), reapplying changes to data pages (using full-page images to repair torn pages). Because committed transactions' WAL was fsynced and uncommitted work is simply not replayed to commit, the database comes back **consistent and durable** to the last committed transaction.

---

## 4. Design Trade-Offs

### 4.1 No-overwrite MVCC: readers never block writers, but bloat is the bill

PostgreSQL's append-style versioning (new tuple per UPDATE, old kept until VACUUM) is the source of its greatest strength and its most-cited weakness.

- **Advantage:** read queries take a snapshot and never lock or block writers; UPDATE-heavy and read-heavy workloads coexist without reader/writer contention. Rollback is essentially free (just don't make the new version visible).
- **Limitation:** dead tuples accumulate as **bloat**, indexes carry pointers to dead tuples, and **VACUUM** must run continuously. Long-running transactions hold back the "oldest snapshot" and prevent reclamation, inflating tables.
- **Contrast with InnoDB / Oracle:** those engines update the row **in place** and push the *old* image into a separate **undo/rollback segment**. Readers reconstruct old versions from undo. This keeps the main table dense (no VACUUM), but makes rollback expensive (must apply undo), can cause **long-running-read** failures ("snapshot too old" / ORA-01555), and adds undo-log contention. PostgreSQL pushed the cost to a background cleaner (VACUUM) instead of the foreground transaction path — a deliberate engineering choice.

### 4.2 WAL: write amplification for durability and fast recovery

- **Advantage:** commit is one sequential fsync; crash recovery is a bounded redo from the last checkpoint; full-page writes defeat torn pages.
- **Limitation / performance implication:** **write amplification.** Every change is written at least twice (WAL + data page), and full-page writes after each checkpoint inflate WAL volume — the measured **3620 kB for 10 000 rows** (~370 B/row) is mostly overhead and page images, not user data. Mitigations: larger checkpoints (fewer full-page writes), `wal_compression`, and grouping commits.

### 4.3 Buffer manager double-buffering and `shared_buffers` sizing

- PostgreSQL caches pages in shared_buffers **and** relies on the OS page cache underneath → **double buffering** wastes some RAM, but gains portability and a robust second cache. The §5 `shared hit=5905 read=36` shows the cache working extremely well.
- **`shared_buffers` sizing trade-off:** too small → frequent eviction and OS-cache round trips; too large → memory taken from the OS cache (which PG also depends on) and longer checkpoint flush storms. The common heuristic is ~25% of RAM (here it's the conservative 128 MB default). It is *not* "set it to all of RAM," precisely because of double buffering.

### 4.4 Heap + secondary indexes vs. index-organized tables

- PostgreSQL uses a **heap** (unordered) table with **secondary B-tree indexes** that point at heap TIDs. Inserts are cheap (append to heap), and a table can have many indexes cheaply.
- **Limitation:** every index lookup needs a second hop to the heap (unless an **index-only scan** applies via the visibility map), and an UPDATE that moves a tuple to a new page must update indexes (mitigated by **HOT** updates when no indexed column changes). Index-organized tables (Oracle IOT, InnoDB clustered PK) make PK lookups single-hop but make every secondary index a two-level lookup and make non-PK inserts costlier. PostgreSQL chose flexibility and cheap inserts over clustered-read locality.

### 4.5 Trade-off summary

| Decision | Advantage | Cost / Limitation | Mitigation |
|---|---|---|---|
| No-overwrite MVCC | Readers never block writers; cheap rollback | Bloat; needs VACUUM; long txns hold back cleanup | Autovacuum, HOT updates, `fillfactor` |
| WAL + full-page writes | Durability; bounded fast recovery; cheap commit | Write amplification; large WAL volume | Checkpoint tuning, `wal_compression` |
| shared_buffers + OS cache | Robust, portable, two-level cache | Double buffering; checkpoint flush spikes | ~25% RAM sizing; bgwriter |
| Heap + secondary indexes | Cheap inserts; many indexes; flexible | Extra heap hop; index maintenance on UPDATE | Index-only scans (VM), HOT |
| Process-per-connection | Strong isolation; crash containment | Per-conn memory; high conn counts hurt | Connection pooler (PgBouncer) |

---

## 5. Experiments / Observations

> Measured on **PostgreSQL 16.13**, macOS, `block_size=8192`, `shared_buffers=128MB`, `max_connections=100`, `default_transaction_isolation=read committed`. Tables: `users` = 200 000 rows, `orders` = 500 000 rows.

### 5.1 Query plan and how the planner used statistics

```sql
EXPLAIN (ANALYZE, BUFFERS, VERBOSE)
SELECT u.country, count(*), sum(o.amount)
FROM users u JOIN orders o ON o.user_id = u.id
WHERE o.status = 'paid' AND u.country = 'IN'
GROUP BY u.country;
```

```
Finalize GroupAggregate (actual time=19.164..20.371 rows=1)
  -> Gather (Workers Planned: 2, Workers Launched: 2)
       -> Partial GroupAggregate
            -> Parallel Hash Join (Inner Unique: true, Hash Cond: o.user_id = u.id) (actual rows=11111 loops=3)
                 -> Parallel Seq Scan on orders (Filter: status='paid'; Rows Removed by Filter: 111111)
                 -> Parallel Hash (Buckets: 65536  Batches: 1  Memory Usage: 2432kB)
                      -> Parallel Bitmap Heap Scan on users (Recheck Cond: country='IN'; Heap Blocks: exact=981)
                           -> Bitmap Index Scan on idx_users_country (rows=40000)
Buffers: shared hit=5905 read=36
Planning Time: 0.538 ms
Execution Time: 20.528 ms
```

**Planner statistics consulted** (`pg_stats`, materialized from `pg_statistic`, gathered by ANALYZE):

| Column | `n_distinct` | `null_frac` | MCVs |
|---|---|---|---|
| `orders.status` | 3 | 0 | `{cancelled, paid, pending}` |
| `orders.user_id` | -0.380574 (negative ⇒ distinct ÷ row-count) | 0 | — |

**How the planner reasoned:**

- **`status = 'paid'` selectivity.** With `n_distinct = 3` and `'paid'` among the 3 MCVs (no skew recorded), the planner estimates selectivity ≈ **1/3**. On 500 000 rows that's ~166 667 matching, ~333 333 removed — and indeed the runtime shows `Rows Removed by Filter: 111111` *per worker* × 3 ≈ 333 333. Because **1/3 of the table qualifies**, a B-tree index on `status` would be useless (it would visit most of the table anyway plus pay random-I/O per row), so the planner correctly chose a **Parallel Seq Scan** with a filter. This is the textbook case where *low cardinality defeats an index* and the statistics tell the planner so directly.
- **`country = 'IN'` selectivity.** `users` has a column `country` with an index `idx_users_country`. Here the predicate is far more selective (~40 000 of 200 000 ⇒ ~20%), so the planner used a **Bitmap Index Scan** to gather TIDs, then a **Bitmap Heap Scan** touching `Heap Blocks: exact=981` — bitmap scans are chosen precisely when a value matches *enough* rows to make per-row index lookups wasteful but *few enough* that a full seq scan is also wasteful: the sweet spot in between.
- **Join.** With `user_id`'s negative `n_distinct` indicating many distinct values, and `users.id` unique (note `Inner Unique: true`), a **hash join** with `users` as the (smaller, filtered) hash side is optimal — the hash fits in `work_mem` (`Batches: 1`, 2432 kB), so no spill to disk.
- **Buffers.** `shared hit=5905 read=36` — **99.4% buffer-cache hit rate**. The entire working set is resident in shared_buffers; only 36 pages missed. This is the buffer manager (§3.1) doing its job; the 20.5 ms runtime is CPU-bound, not I/O-bound.

### 5.2 MVCC in the raw page (`pageinspect`)

After `UPDATE` of `id=1` inside transaction **780** (`heap_page_items` on `orders` page 0):

| lp | lp_off | lp_len | t_xmin | t_xmax | t_ctid | Interpretation |
|---|---|---|---|---|---|---|
| 1 | 8128 | 64 | 734 | **780** | **(4347,67)** | OLD version of id=1 — now **dead**; xmax stamped, ctid points forward |
| 2 | 8056 | 72 | 734 | 0 | (0,2) | live, untouched |
| 3 | 7992 | 64 | 734 | 0 | (0,3) | live, untouched |

This is §3.3 made concrete: the original tuple (`xmin=734, xmax=0`) was *not* overwritten. The UPDATE wrote a new version at `(4347,67)` with `xmin=780` and set the old tuple's `xmax=780` plus a forward `ctid`. The old version is dead and awaits VACUUM.

`pgstattuple('orders')`:

| Metric | Value | Note |
|---|---|---|
| `table_len` | 35 618 816 bytes (~34 MB) | matches reported heap size |
| `tuple_count` | 500 000 | all live rows |
| `tuple_percent` | 93.54% | dense, healthy packing |
| `dead_tuple_count` | 1 | exactly the one UPDATE above |
| `dead_tuple_len` | 64 | = the old tuple's `lp_len` |
| `free_percent` | 0.5% | almost no slack |

The lone dead tuple (64 bytes) is precisely the reclaimable space VACUUM would recover — a microcosm of how bloat forms and is cleaned.

### 5.3 B-Tree internals (`pageinspect`)

`bt_metap('idx_orders_user_id')`:

| Field | Value | Meaning |
|---|---|---|
| magic | 340322 | nbtree magic constant |
| version | 4 | modern (dedup-capable) format |
| root | block 290 | root page location |
| level | 2 | **3 levels** (root=2 → internal=1 → leaf=0) |
| fastroot | 290 | cached fast-root pointer |
| allequalimage | t | deduplication is safe for these keys |

`bt_page_stats(..., 1)` for a leaf page:

| Field | Value | Meaning |
|---|---|---|
| type | `l` | leaf |
| live_items | 169 | index tuples on the page |
| dead_items | 0 | no LP_DEAD-marked entries |
| avg_item_size | 39 bytes | key + heap TID + line pointer |
| page_size | 8192 | one block |
| free_size | 768 | ~9% free — nearly full |

A 3-level tree over 500 000 rows means any equality lookup touches just **3 pages** (root → internal → leaf), each likely a buffer-cache **hit**. With ~169 entries/leaf, ~500 000 rows need ~3 000 leaves, which a single internal level over a root comfortably indexes — confirming the logarithmic-depth analysis of §3.2.

### 5.4 WAL generation

| Operation | WAL generated | Per-row | Method |
|---|---|---|---|
| INSERT 10 000 rows into `orders` | **3620 kB** | ~370 bytes/row | `pg_wal_lsn_diff(pg_current_wal_lsn() after, before)` |

~370 bytes of WAL per row is far larger than the heap row itself (~64–72 bytes per §5.2). The difference is **WAL record overhead + periodic full-page-write images** (§3.4) — direct evidence of the **write-amplification** trade-off discussed in §4.2.

### 5.5 Relation sizes

| Object | Heap | Total (heap + indexes + TOAST) |
|---|---|---|
| `orders` | 34 MB | 57 MB |
| `users` | 11 MB | 17 MB |
| `idx_orders_user_id` | 8784 kB | — |

The 23 MB gap between `orders` heap (34 MB) and total (57 MB) is indexes — secondary indexes are not free, reinforcing the heap-vs-index-organized trade-off of §4.4.

---

## 6. Key Learnings

1. **The four subsystems are one machine.** The WAL rule is *enforced inside the buffer manager* (a dirty victim's WAL is flushed before the page is written), MVCC is what fills the pages the buffer manager caches, and the B-tree points at the heap TIDs MVCC versions. None works in isolation.
2. **MVCC trades VACUUM for non-blocking reads.** No-overwrite versioning means readers never block writers and rollback is free — but dead tuples (`dead_tuple_count=1` seen live) and bloat are the standing bill, paid by autovacuum, not the foreground transaction. The forward-pointing `ctid` chain is the literal mechanism.
3. **Statistics, not guesses, drive the plan.** `n_distinct=3` + MCV `{paid,...}` told the planner `status='paid'` is ~1/3 selective, so it (correctly) chose a parallel seq scan over an index; the more selective `country='IN'` got a bitmap scan. Cardinality is destiny.
4. **The buffer manager is the performance hinge.** A 99.4% hit rate (`shared hit=5905 read=36`) turned a multi-table join over 700 000 rows into a 20 ms CPU-bound operation. Clock-sweep + ring buffers keep the hot set resident without LRU's contention.
5. **WAL buys durability with write amplification.** ~370 bytes of WAL per 64–72-byte row is the cost of full-page writes + record overhead — and the payoff is a one-fsync commit and bounded crash recovery via redo from the last checkpoint.
6. **B-trees stay shallow and concurrent.** A 3-level tree (`level=2`) answers any 500 000-row lookup in 3 page accesses, while Lehman & Yao right-links let readers survive concurrent splits without restarting.
7. **Double buffering is a deliberate choice, not an accident.** Relying on the OS cache under shared_buffers wastes some RAM but yields portability and a robust second cache — which is why `shared_buffers` is sized to ~25% of RAM, not 100%.
8. **Checkpoints bound recovery but spike I/O.** They cap how much WAL must be replayed; bgwriter exists precisely to smear the dirty-page flush load out so the checkpoint isn't a stall.

---

## References

- PostgreSQL 16 Documentation — **Internals: Database Physical Storage** (page layout, heap tuple header, item pointers). <https://www.postgresql.org/docs/16/storage.html>
- PostgreSQL 16 Documentation — **Reliability and the Write-Ahead Log (WAL)** (write-ahead rule, full-page writes, checkpoints, recovery). <https://www.postgresql.org/docs/16/wal.html>
- PostgreSQL 16 Documentation — **Concurrency Control / MVCC** (snapshots, isolation levels, visibility). <https://www.postgresql.org/docs/16/mvcc.html>
- PostgreSQL 16 Documentation — **Routine Vacuuming** (bloat reclamation, XID freezing, wraparound prevention). <https://www.postgresql.org/docs/16/routine-vacuuming.html>
- PostgreSQL 16 Documentation — **Index Access Method / B-Tree** and the README in `src/backend/access/nbtree/`. <https://www.postgresql.org/docs/16/btree.html>
- PostgreSQL source tree: `src/backend/storage/buffer/` (`bufmgr.c`, `freelist.c`), `src/backend/access/nbtree/` (`nbtinsert.c`, `nbtsearch.c`, `nbtpage.c`), `src/backend/access/heap/` (`heapam.c`), `src/backend/access/transam/` (`xlog.c`, `xloginsert.c`, `clog.c`).
- P. Lehman & S. B. Yao, *"Efficient Locking for Concurrent Operations on B-Trees,"* ACM TODS, 1981 (the high-key / right-link algorithm nbtree implements).
- Hironobu Suzuki, *The Internals of PostgreSQL* — <https://www.interdb.jp/pg/> (excellent learning companion for buffer manager, MVCC, VACUUM, and WAL chapters).
- M. Stonebraker & L. Rowe, *"The Design of POSTGRES,"* SIGMOD 1986 (historical origin of the no-overwrite storage model).
