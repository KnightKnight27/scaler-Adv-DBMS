# Topic 3: MySQL / InnoDB Storage Engine

This report details the internal architecture of MySQL's default transaction-safe storage engine, **InnoDB**. It covers storage layout, index structures, the transaction log pipeline, concurrency control, and compares these mechanisms with those of PostgreSQL.

---

## 1. Clustered Indexes & Storage Architecture

InnoDB organizes database tables in a structured, index-organized format, fundamentally different from PostgreSQL's heap-file design.

```
┌────────────────────────────────────────────────────────────────────────┐
│                      CLUSTERED INDEX PAGE LAYOUT                       │
├────────────────────────────────────────────────────────────────────────┤
│                                                                        │
│                      ┌────────────────────────┐                        │
│                      │      Root Page         │                        │
│                      └───────────┬────────────┘                        │
│                                  │                                     │
│                     ┌────────────┴────────────┐                        │
│                     ▼                         ▼                        │
│         ┌──────────────────────┐   ┌──────────────────────┐            │
│         │   Interior Page      │   │   Interior Page      │            │
│         └──────────┬───────────┘   └──────────┬───────────┘            │
│                    │                          │                        │
│           ┌────────┴────────┐        ┌────────┴────────┐               │
│           ▼                 ▼        ▼                 ▼               │
│      ┌──────────┐      ┌──────────┐┌──────────┐      ┌──────────┐      │
│      │ Leaf Page│◄────►│ Leaf Page││ Leaf Page│◄────►│ Leaf Page│      │
│      │ [Data 1] │      │ [Data 2] ││ [Data 3] │      │ [Data 4] │      │
│      └──────────┘      └──────────┘└──────────┘      └──────────┘      │
│      └────────────────────────────────────────────────────────┘      │
│                    Rows stored directly in leaf nodes                  │
└────────────────────────────────────────────────────────────────────────┘
```

### Clustered Index (Primary Key Storage)
* In InnoDB, every table has a **Clustered Index** (organized as a B+Tree).
* The leaf nodes of the clustered index contain the **entire physical row data** (all columns).
* If a table has a primary key defined, that key is used as the clustered index key. If no primary key is defined, InnoDB searches for the first `UNIQUE` index containing only `NOT NULL` columns. If none exists, InnoDB automatically generates an implicit 6-byte row identifier (`rowid`) to organize the clustered index.
* **Lookup Performance**: Looking up a row by its primary key is extremely fast. Once the primary key B+Tree is traversed to the leaf page, the data is immediately accessible, requiring no secondary lookups.

### Secondary Indexes
* Leaf nodes of secondary indexes do not contain pointers to physical file offsets. Instead, they store the **primary key value** of the corresponding row.
* **Secondary Lookup Overhead**: Finding a row via a secondary index requires a two-step traversal:
  1. Traverse the secondary index B+Tree to retrieve the primary key.
  2. Traverse the clustered index B+Tree using that primary key to access the actual row data.
  This is known as a **double lookup**.
* **Impact of Primary Key Size**: Because every secondary index contains copy of the primary key in its leaf nodes, a large primary key (e.g., a long UUID string) increases the storage footprint of all secondary indexes, potentially reducing buffer pool efficiency.

---

## 2. Memory Management: The Buffer Pool

InnoDB manages its memory cache through the **Buffer Pool** (configured via `innodb_buffer_pool_size`).
* **Cached Structures**: Caches data pages, index pages, undo pages, change buffers (which defer index writes), and adaptive hash indexes.
* **LRU List with Sublists**: Unlike standard LRU, the buffer pool divides its Page List into two segments:
  * **New Sublist (5/8 of the pool)**: Contains recently accessed "young" pages.
  * **Old Sublist (3/8 of the pool)**: Contains older pages prone to eviction.
  * **Page Promotion Mechanism**: When a page is read from disk, it is initially placed at the *head of the old sublist*. It is only promoted to the new sublist if it remains accessed after a configurable period (controlled by `innodb_old_blocks_time`, default 1000 ms). This prevents bulk queries (like full table scans) from flushing hot pages out of the buffer pool.

---

## 3. The Transaction Log Pipeline: Undo & Redo Logs

To achieve ACID compliance with in-place updates, InnoDB relies on a dual-logging architecture consisting of **Undo Logs** (for rollback and MVCC) and **Redo Logs** (for durability and crash recovery).

```
          ┌──────────────────────────────────────────────┐
          │             Transaction Start                │
          └──────────────────────┬───────────────────────┘
                                 │
                                 ▼
          ┌──────────────────────────────────────────────┐
          │   Write Old Value to UNDO LOG buffer         │
          │   (For rollback and historical snapshots)    │
          └──────────────────────┬───────────────────────┘
                                 │
                                 ▼
          ┌──────────────────────────────────────────────┐
          │   Modify page in-place in Buffer Pool        │
          └──────────────────────┬───────────────────────┘
                                 │
                                 ▼
          ┌──────────────────────────────────────────────┐
          │   Write physical page change to REDO LOG     │
          │   (Guarantees durability on crash)           │
          └──────────────────────────────────────────────┘
```

### Undo Logs (Logical Operations)
* **Purpose**: Records the *undo information* needed to revert a transaction or reconstruct previous states of a row for concurrent transactions (MVCC).
* **Format**: Logical undo records (e.g., "if this was an update, change column A back from value X to Y").
* **Lifecycle**: Written to undo segments (stored in undo tablespaces). Once a transaction commits and no active reader snapshots require the old versions, the **Purge Thread** asynchronously cleans up the undo records.

### Redo Logs (Physical Changes)
* **Purpose**: Guarantees durability. If the database crashes, dirty pages in the buffer pool that were not yet flushed to the data files can be reconstructed by replaying the redo log.
* **Format**: Physical-logical records indicating exact byte-level modifications on specific page blocks (e.g., "in page 102, write bytes 0xAB at offset 45").
* **Write Pipeline**: Redo records are written to an in-memory buffer (`innodb_log_buffer_size`) and flushed to disk (`ib_logfile0`, `ib_logfile1`) on transaction commit (controlled by `innodb_flush_log_at_trx_commit`).

---

## 4. Concurrency Control: Locking & Next-Key Locks

InnoDB implements multi-granular locking to support concurrent transactions while preventing anomalies.

### Lock Types
* **Record Locks**: Locks on the physical index records (e.g., `SELECT * FROM t WHERE id = 4 FOR UPDATE;` locks index record 4).
* **Gap Locks**: Locks on the gaps *between* index records, or the gap before the first or after the last record. These locks do not lock the records themselves; they prevent other transactions from inserting values into the gap.
* **Next-Key Locks**: A combination of a record lock on the index record and a gap lock on the gap preceding it. Next-key locks are used by default in `REPEATABLE READ` isolation level.

### Preventing Phantom Reads
A **Phantom Read** occurs when a transaction runs a query, and a concurrent transaction inserts new rows matching the query criteria, making them appear in subsequent executions within the same transaction.
* InnoDB prevents phantom reads in the `REPEATABLE READ` isolation level using **Next-Key Locking**:
  * For example, if a table has index records for values 10 and 20, executing:
    ```sql
    SELECT * FROM t WHERE id BETWEEN 10 AND 20 FOR UPDATE;
    ```
    locks the record 10, the record 20, and the gap between them $(10, 20)$.
  * Any attempt by a concurrent transaction to insert a row with `id = 15` will block on the gap lock, preventing phantoms from appearing.

---

## 5. Comparison: InnoDB vs. PostgreSQL MVCC

The table below contrasts the MVCC architectures of InnoDB and PostgreSQL:

| Dimension | MySQL / InnoDB | PostgreSQL |
| :--- | :--- | :--- |
| **Row Modification** | **In-place updates**: The original row page is modified directly. | **Append-only updates**: The update leaves the old tuple in place and writes a new one elsewhere. |
| **Old Version Storage** | **Undo Log**: Historical row versions are stored in dedicated undo segments. | **Heap Pages**: All row versions (active, historical, and dead) are stored together in the table heap pages. |
| **Tuple Overhead** | **Small**: Rows contain a 6-byte Transaction ID (`DB_TRX_ID`) and a 7-byte Rollback Pointer (`DB_ROLL_PTR`). | **Large**: Tuples contain a 23-byte header including `t_xmin`, `t_xmax`, and command IDs. |
| **Read/Write Behavior** | **Undo reconstruction**: Readers locate the current row in the B+Tree and follow `DB_ROLL_PTR` to reconstruct older versions from the undo log if needed. | **Snapshot evaluation**: Readers scan heap pages and evaluate `xmin`/`xmax` flags against their transaction snapshot. |
| **Index Updates** | **Single index update**: Modifying non-indexed columns does not require updating secondary indexes (they still point to the same primary key). | **Index write amplification**: Modifying any column requires updating all secondary indexes to point to the new tuple ID, unless optimized by HOT (Heap-Only Tuples). |
| **Cleanup Overhead** | **Purge thread**: Asynchronously deletes undo pages. Very low database fragmentation. | **Vacuuming**: Requires periodic manual or automatic table scans to sweep and remove dead tuples, causing space bloat if neglected. |

---

## 6. Suggested Questions Answered

### Q1: Why does InnoDB need both undo and redo logs?
* **Redo logs** handle **durability and speed**. They record physical modifications to be replayed during crash recovery, allowing the database to delay flushing dirty buffer pool pages to disk. This converts slow random writes into fast sequential log appends.
* **Undo logs** handle **concurrency and atomicity**. They record the logical operations needed to roll back active transactions (for atomicity) and reconstruct older snapshots of rows for concurrent readers (for MVCC), ensuring transactions do not see uncommitted changes.

### Q2: What advantages do clustered indexes provide?
* **I/O Efficiency for Primary Lookups**: Storing row data directly inside the leaf nodes of the primary key index eliminates the need for an additional disk read (pointer dereference) to locate the row after index traversal.
* **Range Scan Locality**: Because leaf pages are physically ordered by the primary key, range scans (e.g., `WHERE id BETWEEN 100 AND 200`) enjoy excellent physical cache locality, as sequential keys reside on the same or adjacent pages.

### Q3: Why did PostgreSQL choose a different MVCC model?
PostgreSQL chose a heap-versioned MVCC model (avoiding undo logs) for several architectural reasons:
1. **Simplicity in Recovery**: Recovery is simplified because there is no need to maintain undo state or roll back uncommitted changes during startup; the visibility rules naturally ignore uncommitted transaction IDs.
2. **Extensibility**: By storing versions in the heap, PostgreSQL can easily support diverse indexing types (GIN, GiST, BRIN, Hash) because all indexes point to a stable physical Heap Tuple ID (TID) rather than being forced to go through a primary key indirection.
3. **No Undo Bottleneck**: It avoids lock and I/O bottlenecks associated with writing to shared undo segments under heavy parallel write-heavy workloads.
