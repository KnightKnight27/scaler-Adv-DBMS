# MySQL / InnoDB Storage Engine

**Piyush Bansal — 24BCS10079**

---

## 1. Problem Background

InnoDB is the default storage engine of MySQL. It solves the same problem as
PostgreSQL — safe, durable, concurrent transactions — but it makes **different
engineering choices**. The most important one is *how it stores rows* and *how it
does MVCC*. Understanding InnoDB is mostly about understanding two ideas:

1. **Clustered index** — the table *is* a B-Tree sorted by primary key.
2. **Undo + redo logs** — how it updates rows in place but still supports MVCC and
   crash recovery.

---

## 2. Architecture Overview

```
            SQL query
               │
               ▼
        ┌──────────────┐
        │ MySQL server │
        └──────┬───────┘
               ▼
   ┌───────────────────────────────┐
   │        InnoDB engine          │
   │  ┌─────────────┐              │
   │  │ Buffer Pool │  cached pages │
   │  └─────────────┘              │
   │  Redo log (durability)        │
   │  Undo log  (MVCC + rollback)  │
   └──────────────┬────────────────┘
                  ▼
        Clustered index (B+Tree on disk)
        = the actual table data, sorted by primary key
```

- **Buffer Pool** — InnoDB's in-memory page cache (same role as PostgreSQL's shared
  buffers).
- **Clustered index** — the table's rows are stored *inside* the primary-key B-Tree
  leaves. There is no separate heap.
- **Redo log** — for durability/crash recovery.
- **Undo log** — keeps old row versions for MVCC and rollback.

---

## 3. Internal Design

### 3.1 Clustered Index (the key idea)

In InnoDB, **the table is the primary-key B-Tree.** The leaf nodes of that B-Tree
*contain the full rows*, sorted by primary key.

- Looking up a row by primary key lands you directly on the data — no extra hop.
- Rows with nearby primary keys are physically stored together → range scans on the
  primary key are very fast and cache-friendly.

**Secondary indexes** (on non-primary columns) don't store the row. They store the
**primary key value**, and InnoDB then does a second lookup in the clustered index
to fetch the row. So:

```
Secondary index lookup:
   secondary key  ─►  primary key  ─►  clustered index  ─►  row
                       (two B-Tree lookups)
```

This is why a good (small, sequential) primary key matters so much in InnoDB.

### 3.2 In-place updates + Undo logs (MVCC, InnoDB style)

Unlike PostgreSQL (which writes a whole new row version), InnoDB **updates the row
in place** but first copies the *old* values into the **undo log**.

- A reader that needs an older snapshot reconstructs the old version by walking the
  undo log backwards. This is "Oracle-style" MVCC.
- The undo log also powers **ROLLBACK** — to undo a transaction, just apply its undo
  records.

So InnoDB's MVCC garbage isn't dead rows in the table (like PostgreSQL); it's old
**undo records**, which a background "purge" thread cleans up once no transaction
needs them.

### 3.3 Redo log (durability)

The redo log is InnoDB's write-ahead log: changes are written to the redo log
*before* the data pages are flushed. On crash, InnoDB replays the redo log to recover
committed changes. (Same principle as PostgreSQL's WAL.)

**So InnoDB needs both logs for different reasons:**
- **Redo log** = "how to *redo* committed changes after a crash" → durability.
- **Undo log** = "how to *undo* a change / show an old version" → rollback + MVCC.

### 3.4 Locking

InnoDB does **row-level locking** (not table-level), so two transactions writing
different rows don't block each other.
- **Gap locks** lock the *gap between* index records to stop other transactions from
  inserting there — this prevents "phantom rows" and is how InnoDB achieves the
  REPEATABLE READ isolation level safely.
- Isolation levels supported: READ UNCOMMITTED, READ COMMITTED, REPEATABLE READ
  (default), SERIALIZABLE.

---

## 4. Design Trade-Offs

| Choice | Advantage | Trade-off |
|--------|-----------|-----------|
| Clustered index | Primary-key lookups & range scans very fast | Secondary lookups need two B-Tree traversals; PK choice is critical |
| In-place update + undo | Table stays compact (no dead rows in heap) | Reads of old versions must rebuild from undo log |
| Redo + undo logs | Durability *and* rollback/MVCC | Two logs to write and maintain |
| Row-level + gap locks | High write concurrency, no phantoms | Gap locks can cause unexpected lock waits on inserts |

### InnoDB vs PostgreSQL — the core difference

| | PostgreSQL | InnoDB |
|---|-----------|--------|
| Update style | Append a new row version | Update in place |
| Old versions kept | In the table (dead tuples) | In the undo log |
| Cleanup | `VACUUM` | Background purge thread |
| Table storage | Heap + separate indexes | Clustered index *is* the table |
| MVCC flavor | Multiple tuple versions in heap | Reconstruct old version from undo |

---

## 5. Experiments / Observations

```sql
-- See the chosen plan and whether a secondary index is used
EXPLAIN SELECT * FROM students WHERE email = 'a@b.com';

-- Observe locking: in two sessions, run REPEATABLE READ transactions and
-- INSERT into a range another session is scanning — watch the gap lock wait.
SELECT * FROM performance_schema.data_locks;   -- shows held locks incl. gap locks
```

**Observation to note:** a query filtering on a *secondary* indexed column shows a
two-step access (index → clustered index lookup), which is exactly the
"secondary index stores the primary key" design in action.

---

## 6. Key Learnings

- **Why clustered indexes help:** the row lives in the index leaf, so a primary-key
  lookup is one traversal and primary-key ranges are sequential on disk.
- **Why InnoDB needs both undo and redo logs:** redo = durability (redo committed work
  after a crash); undo = rollback + showing old versions for MVCC. They answer two
  different questions.
- **Why PostgreSQL chose a different MVCC model:** PostgreSQL keeps versions in the
  heap (simpler writes, but needs VACUUM); InnoDB keeps them in undo logs (compact
  table, but reads of old versions cost more). Neither is "right" — it's a trade-off
  between write simplicity and read/cleanup cost.

---

### References
- MySQL documentation — *InnoDB Storage Engine* (clustered index, undo/redo logs, locking)
- Comparison drawn against my PostgreSQL Internals write-up (MVCC + WAL)
