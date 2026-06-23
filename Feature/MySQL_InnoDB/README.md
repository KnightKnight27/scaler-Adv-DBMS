# MySQL / InnoDB Storage Engine

## 1. Problem Background

InnoDB became MySQL's default storage engine in 5.5 because the older MyISAM engine had no row-level locking, no transactions, and no crash recovery. InnoDB brought OLTP-grade ACID to MySQL: clustered indexes, MVCC, redo and undo logs, row-level locking. Its model is heavily influenced by Oracle — in-place updates and rollback via undo, instead of PostgreSQL-style new-tuple-per-update.

---

## 2. Architecture Overview

```
              ┌──────────────────────────────┐
   SQL ─────▶ │ MySQL server (parser, opt.)   │
              └─────────────────┬─────────────┘
                                │ handler API
              ┌─────────────────▼─────────────┐
              │           InnoDB              │
              │  ┌────────────────────────┐   │
              │  │ Buffer Pool (LRU)      │   │
              │  └──────────┬─────────────┘   │
              │  ┌──────────▼─────────────┐   │
              │  │ Row format, B+tree     │   │
              │  └──────────┬─────────────┘   │
              │  ┌──────────▼──┐  ┌──────────┐│
              │  │ Redo log    │  │ Undo log ││
              │  └─────────────┘  └──────────┘│
              └───────────────────────────────┘
                       │
                       ▼
              tablespaces (.ibd files)
```

---

## 3. Internal Design

### Clustered Index

In InnoDB, **the primary key IS the table**. Rows live in the leaves of a B+tree keyed by the PK. Consequences:
- PK lookups need one B+tree descent — no separate heap fetch.
- Range scans on PK are sequential I/O.
- If you don't define a PK, InnoDB picks the first non-null unique index, or invents a hidden 6-byte rowid.
- Inserts in PK order are cheap (append at right edge); random PKs (e.g., UUIDv4) cause page splits and fragmentation.

### Secondary Indexes

A secondary index stores `(secondary_key → primary_key)`, not a row pointer. So a secondary lookup is:
1. Descend the secondary B+tree → get the PK.
2. Descend the clustered B+tree with that PK → get the row.

This makes secondary lookups two B+tree traversals, but it means a PK change does not invalidate every secondary index entry's "row pointer" — there isn't one.

### Buffer Pool

A large in-memory cache of 16 KB pages, managed as a **modified LRU**:
- New pages enter at the boundary between a "young" and "old" sub-list, not at the head.
- Only if a page is touched again after a delay does it get promoted to "young".
- This prevents a single big table scan from evicting the working set.
- Dirty pages are flushed asynchronously by the page-cleaner threads, throttled to smooth I/O spikes.

### Redo Log (WAL for data)

A circular log of physical page changes (`ib_logfile*` / redo log files):
- A change writes to the in-memory log buffer; commit forces the buffer to disk (`innodb_flush_log_at_trx_commit=1`).
- After a crash, recovery replays redo records to bring data pages back to the last committed state.
- Redo is **physical** ("at this offset on this page, write these bytes"), so replay is fast and order-independent within a page.

### Undo Log (MVCC + rollback)

Every row update keeps the **previous version** in an undo segment, with a pointer in the row header (`DB_ROLL_PTR`).
- Rollback rebuilds the row by walking the undo chain.
- MVCC reads also walk the chain: if the current row's `DB_TRX_ID` isn't visible to the reader's snapshot, follow the roll pointer back until a visible version is found.
- Old undo records are purged once no transaction can need them anymore.

So InnoDB does **MVCC by undo**, not by keeping every version inline like PostgreSQL.

### Row-Level Locking and Gap Locks

InnoDB locks index records, not table rows directly.
- **Record lock**: on the index entry itself.
- **Gap lock**: locks the space between two index entries — prevents phantom inserts.
- **Next-key lock**: record + gap to its left — the default under `REPEATABLE READ`.

`READ COMMITTED` disables gap locks; `SERIALIZABLE` turns plain SELECT into locking reads.

---

## 4. Design Trade-Offs

### vs PostgreSQL

| Aspect | InnoDB | PostgreSQL |
|---|---|---|
| Row update | In-place + undo entry | New tuple version |
| MVCC source | Undo chain | Inline old versions |
| Garbage collection | Undo purge | VACUUM |
| Table storage | Clustered B+tree (PK) | Heap |
| PK lookup | 1 B+tree descent | Index descent + heap fetch |
| Secondary index | Stores PK | Stores `ctid` |
| Long-running readers | Hold undo from purge → undo grows | Hold dead tuples → bloat |

### Why InnoDB Needs **Both** Undo and Redo

They cover different failure modes:
- **Redo** answers "the server crashed mid-commit — did committed work survive?" Replays physical changes from a known-good checkpoint.
- **Undo** answers "the transaction needs to roll back" and "this reader wants the old version of the row." Logical row-level history.

A redo-only system can't roll back or do MVCC; an undo-only system can't recover crashed pages efficiently.

### Trade-offs of Clustered Storage

- Wins: fast PK scans, smaller index footprint for PK access.
- Costs: random-key inserts hurt, PK should be small (every secondary index stores it), changing PK is expensive.

---

## 5. Experiments / Observations

**Page splits on random PK.** Loading 1 M rows with sequential `INT` PK vs random `UUID` PK: the UUID load is several times slower and produces a much larger `.ibd` because random inserts split pages mid-way and leave them ~50% full.

**Gap locks blocking inserts under REPEATABLE READ:**
```sql
-- session 1
START TRANSACTION;
SELECT * FROM t WHERE id BETWEEN 10 AND 20 FOR UPDATE;
-- session 2
INSERT INTO t(id) VALUES (15);   -- blocks until session 1 commits
```
Switching to `READ COMMITTED` removes gap locks and the insert proceeds immediately — at the cost of phantoms.

**Undo growth under a long reader.** A long-running `REPEATABLE READ` transaction in one session, while another session updates rows, makes the **history list length** (`SHOW ENGINE INNODB STATUS`) grow steadily. Undo cannot be purged until the long reader ends.

---

## 6. Key Learnings

- Clustered storage and the row format are the heart of InnoDB. Almost every performance question reduces to "what does this do on the clustered B+tree?"
- Two logs are not redundant — they answer different questions (durability vs history).
- MVCC by undo means writers don't make dead row versions in the table itself, but they do build an undo chain that long readers can keep alive.
- The lessons from comparing InnoDB and PostgreSQL: both implement MVCC and ACID, but every concrete decision (where versions live, how indexes point, what gets locked) cascades into different operational behavior.
