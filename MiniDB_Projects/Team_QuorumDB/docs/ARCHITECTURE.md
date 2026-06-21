# MiniDB Architecture Notes

Companion to the README. Focuses on layering, key invariants, and the
lifecycle of the operations examiners are most likely to probe.

## Layered dependency graph

```
                         cli.py
                           │
                        engine.py  (Database, Connection)
        ┌──────────────────┼───────────────────────────┐
        │                   │                           │
     sql/                 txn/                         wal/
   parser                lock_manager               log_record
   ast                   transaction ──────────────► log_manager
   optimizer  ───────┐                               page_ops ◄──┐
   plan              │                               recovery ───┘
   executor ─────────┤                                   │
        │            │                                   │
   catalog/          └───────────► storage/ ◄────────────┘
   schema                          page
   catalog ──────────► index/      disk_manager
                       bplustree    buffer_pool
                                    heapfile
                                       │
                              replication/ (primary, replica, protocol)
```

Arrows point from a user to its dependency. The **storage** layer depends on
nothing above it; **wal/page_ops** is the single shared redo/undo→page mapping
used by recovery, transaction abort, *and* the replica — so those three code
paths can never drift apart.

## Key invariants

1. **Stable RIDs.** A `(page_id, slot_no)` never changes once assigned; deletes
   tombstone the slot. B+Tree leaves store RIDs, so this must hold.
2. **Write-ahead rule.** The buffer pool flushes the log up to `page.lsn`
   before writing a dirty page to disk.
3. **Commit durability.** `COMMIT` appends a record and `fsync`s the log before
   returning.
4. **Idempotent replay.** Redo is guarded by `page.lsn`; undo targets a fixed
   slot with a fixed image. Both can be re-run safely after a mid-recovery crash.
5. **Strict 2PL.** Locks are released only at commit/abort.

## Lifecycle: `INSERT INTO t VALUES (…)` (autocommit)

```
Connection.execute
  └─ TransactionManager.begin              → BEGIN log record
  └─ Executor._insert
       ├─ ctx.lock_exclusive("t")          → X lock on table t (2PL)
       ├─ check unique indexes (no dup)
       ├─ HeapFile.insert(bytes, txn)
       │    ├─ BufferPool.fetch_page (pin)
       │    ├─ Page.insert_record → slot
       │    ├─ txn.log_insert(...)          → INSERT log record (after-image)
       │    ├─ page.lsn = lsn
       │    └─ unpin(dirty)
       └─ index.tree.insert(key, rid)       for every index
  └─ TransactionManager.commit              → COMMIT record + log fsync
```

## Lifecycle: crash recovery (ARIES)

```
Database.__init__(recover=True)
  └─ RecoveryManager.recover()
       1. Analysis : scan log → winners/losers, table→pages touched
       2. Redo     : replay INSERT/DELETE/UPDATE forward where page.lsn < lsn
       3. Undo     : reverse losers' changes (before-images), write CLRs
  └─ catalog.adopt_pages(...)               reconcile heap page lists
  └─ catalog.rebuild_all_indexes()          rebuild B+Trees from heaps
```

## Lifecycle: replication (Track D)

```
Primary (writable Database)            Replica (following Database)
  log grows as txns commit
  ── CATALOG(snapshot) ───────────────►  catalog.load_from_doc()
  ── RECORDS(redo since lsn) ─────────►  for each rec: ensure page,
                                           page_ops.redo guarded by lsn
  ◄── ACK(applied_lsn) ───────────────   rebuild indexes; serve reads
        (lag = primary_lsn - acked)

  on primary failure:  Replica.promote() → stop following, accept writes
```

## Why Track D reuses the WAL

The redo record format is **physiological** (page + slot + image), so applying
one is a page fetch plus an in-place edit — no SQL re-parse, no re-planning.
A replica is therefore "continuous recovery" against a live log feed, and the
benchmark shows the apply path runs at hundreds of thousands of records/sec.
This reuse is the central design idea of our extension.
