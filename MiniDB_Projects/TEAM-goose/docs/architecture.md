# MiniDB Architecture — Detailed Design Notes

## LSM-tree Write Path

```
User SQL → Parser → Executor → LSMEngine::put()
                                    │
                                    ▼
                              MemTable::put()
                           (std::map insert)
                                    │
                              size > threshold?
                               │         │
                              No        Yes
                               │         │
                               ▼         ▼
                             Done    LSMEngine::flush()
                                         │
                                    freeze MemTable
                                         │
                                    SSTable::build()
                                    (sequential write)
                                         │
                                    Compaction::compact()
                                    (k-way merge if needed)
```

## LSM-tree Read Path

```
User SQL → Parser → Executor → LSMEngine::get(key)
                                    │
                         ┌──────────┼──────────┐
                         ▼          ▼          ▼
                    MemTable   Frozen MTs   SSTables
                    (active)   (flushed)   (on disk)
                         │          │          │
                         ▼          ▼          ▼
                      Found?     Found?     Found?
                         │          │          │
                         └──────────┴──────────┘
                                    │
                               newest wins
                                    │
                                    ▼
                                 Record
```

## Composite Key Design

To support multiple tables in a single LSM-tree instance, keys are prefixed with the table ID:

```
Composite key = [table_id (4 bytes)] + [user key bytes]
```

This ensures different tables' data doesn't collide and allows efficient table-scoped scans.

## B+ Tree Node Structure

```
Internal Node:
┌──────┬──────┬──────┬──────┬──────┐
│ key0 │ key1 │ key2 │ ...  │ keyN │
└──┬───┴──┬───┴──┬───┴──────┴──┬───┘
   ▼      ▼      ▼              ▼
 child0  child1 child2        childN

Leaf Node:
┌──────┬──────┬──────┬──────────┬──────┐
│ key0 │ key1 │ key2 │   ...    │ keyN │
│ rec0 │ rec1 │ rec2 │          │ recN │
└──────┴──────┴──────┴──────────┴──────┘
   └──────────── sibling pointer ──────┘
```

## 2PL Transaction State Machine

```
  ┌──────────────────────────────────────────┐
  │                                          │
  ▼                                          │
BEGIN ──→ ACTIVE ──→ COMMITTING ──→ COMMITTED
              │                    (locks released)
              │
              └──→ ABORTING ──→ ABORTED
                             (locks released,
                              changes undone)
```

## WAL Recovery (ARIES-style simplified)

```
WAL records (sequential):

[LSN=1, TXN=1, BEGIN] [LSN=2, TXN=1, UPDATE, k=1, before=(...), after=(...)]
[LSN=3, TXN=1, COMMIT] [LSN=4, TXN=2, BEGIN] [LSN=5, TXN=2, UPDATE, k=2, ...]
← CRASH — TXN=2 not committed

Recovery:
1. Analysis: TXN=1 is committed, TXN=2 is active
2. Redo: Replay LSN=2 (TXN=1's update)
3. Undo: Restore k=2's before-image
```

## Directory Layout for Data

```
minidb_data/
├── lsm/
│   ├── sstable_1.sst
│   ├── sstable_2.sst
│   └── ... (compacted → sstable_N.sst)
└── wal.log
```
