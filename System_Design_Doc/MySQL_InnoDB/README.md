# MySQL / InnoDB Storage Engine

**Author:** Abhiroop Sistu

**Roll Number:** 24BCS10287

I studied InnoDB, MySQL's default engine. The interesting parts: tables live inside a clustered B+Tree on the primary key, MVCC uses undo logs, durability uses redo logs, and concurrency uses row locks plus gap locks. I compare it with PostgreSQL throughout, since they chose opposite paths.

---

## 1. Problem Background

Early MySQL used MyISAM: fast reads but no transactions and no crash recovery. InnoDB was made to fix that, adding ACID transactions, crash recovery, row-level locking, and MVCC. It became the default in MySQL 5.5.

## 2. Architecture Overview

```text
clients -> MySQL SQL layer (parser, optimizer)
                 |  storage engine API
              InnoDB
   in memory: Buffer Pool (16 KB pages) + Change Buffer + Log Buffer
        |  flush pages              |  flush log
        v                           v
   Tablespace (.ibd):           Redo log
   clustered B+tree +
   secondary indexes + undo
```

The SQL layer sits on top and calls InnoDB through an engine interface. InnoDB caches 16 KB pages in the buffer pool; each table is a tablespace file holding the clustered index, secondary indexes, and undo; durability is provided by the redo log.

## 3. Internal Design

### Clustered Index (The Table Is an Index)

Rows live in the leaves of a B+Tree ordered by the primary key, so finding the PK is finding the row in one search. Rows are stored in PK order, so PK range scans read nearby pages. If no primary key exists, InnoDB creates a hidden row identifier.

### Secondary Indexes

A secondary index leaf stores the primary-key value, not a physical pointer. Therefore a secondary lookup is:

```text
Secondary Index -> Primary Key -> Clustered Index -> Row
```

This means:

- Secondary lookups require two searches.
- Large primary keys increase the size of every secondary index.

*PostgreSQL comparison:* PostgreSQL indexes point to a physical tuple identifier (TID).

### Buffer Pool

The Buffer Pool is an in-memory cache of 16 KB pages, similar to PostgreSQL's `shared_buffers`.

Features:

- Young/old LRU page replacement
- Prevents large scans from evicting frequently used pages
- Change Buffer reduces secondary-index write amplification

### Undo Logs and MVCC

InnoDB uses Oracle-style MVCC.

When an UPDATE occurs:

1. The row is updated in place.
2. The previous version is written to an undo log.
3. A rollback pointer links the row to older versions.

Each row stores:

- Transaction ID
- Roll Pointer

Readers can follow the rollback chain to reconstruct older versions.

Undo logs provide:

- Rollback support
- MVCC snapshot reads

A purge thread eventually removes obsolete undo records.

### Redo Log (Durability)

The redo log implements Write-Ahead Logging (WAL).

Process:

1. Write change to redo log.
2. Flush redo log on COMMIT.
3. Write data pages later.

Crash recovery:

- Replay committed changes from redo logs.
- Undo incomplete transactions.

### Locking

InnoDB supports:

- Shared (S) locks
- Exclusive (X) locks
- Gap locks
- Next-key locks

Gap locks prevent inserts into a key range.

A next-key lock is:

```text
Next-Key Lock = Record Lock + Gap Lock
```

This is the default behavior under MySQL's `REPEATABLE READ` isolation level.

### Version Chain Example

```text
clustered row (latest): PK=7 balance=500 ROLL_PTR -+
undo chain: balance=300 -> balance=100 -> ...       (older versions)
```

## 4. Design Trade-Offs

### Clustered Index

**Advantages**

- Fast primary-key lookups
- Efficient primary-key range scans

**Disadvantages**

- Secondary indexes require a second lookup
- Random primary keys (UUIDs) cause page splits
- Auto-increment keys are generally more storage-friendly

### Why Both Undo and Redo Logs?

| Log Type | Purpose |
|-----------|----------|
| Redo Log | Roll forward committed changes after a crash |
| Undo Log | Roll back uncommitted changes and support MVCC |

Both are necessary for ACID guarantees.

### InnoDB vs PostgreSQL MVCC

**InnoDB**

- Updates rows in place
- Stores old versions in undo logs
- Uses a purge thread for cleanup
- Keeps tables relatively compact

**PostgreSQL**

- Creates a new tuple version for each update
- Keeps old versions in the table
- Rollback is effectively free
- Requires VACUUM to reclaim space

Both approaches solve MVCC differently.

### Locking Trade-Off

Gap and next-key locks prevent phantom reads under `REPEATABLE READ`.

However:

- Inserts into locked ranges are blocked.
- Write contention can increase.

## 5. Experiments / Observations

**Environment:** MySQL/InnoDB 9.6.0

Test table:

```sql
accounts(
    id INT PRIMARY KEY,
    name VARCHAR(...),
    balance DECIMAL(...),
    city VARCHAR(...)
)
```

Rows inserted: **50,000**

### EXPLAIN Results

```sql
WHERE id = 42345
-> type=const  key=PRIMARY   rows=1

WHERE city = 'city_7' (no index)
-> type=ALL    key=NULL      rows=50275

CREATE INDEX idx_city ON accounts(city);

WHERE city = 'city_7' (indexed)
-> type=ref    key=idx_city  rows=1000
```

Observations:

- Primary-key lookup uses the clustered index directly.
- Unindexed predicates require a full table scan.
- Secondary indexes significantly reduce scanned rows.

### Gap Lock Demonstration

A gap was created:

```sql
DELETE FROM accounts WHERE id = 150;
```

**Session 1**

```sql
START TRANSACTION;

SELECT *
FROM accounts
WHERE id BETWEEN 100 AND 200
FOR UPDATE;
```

Locks observed through:

```sql
performance_schema.data_locks
```

Output:

```text
LOCK_TYPE  LOCK_MODE        n
RECORD     X                100
TABLE      IX                 1
RECORD     X,REC_NOT_GAP      1
```

**Session 2**

```sql
INSERT INTO accounts VALUES (150, ...);
```

Result:

```text
ERROR 1205 (HY000): Lock wait timeout exceeded; try restarting transaction
```

The insert was blocked because the gap was locked, demonstrating phantom prevention under `REPEATABLE READ`.

## 6. Key Learnings

- In InnoDB, the table itself is a clustered B+Tree.
- Primary-key lookups require only one search.
- Secondary indexes require an additional hop through the clustered index.
- Keeping primary keys small and sequential improves performance.
- Redo logs provide durability, while undo logs provide rollback and MVCC.
- InnoDB avoids table bloat by storing historical versions in undo logs.
- PostgreSQL stores historical versions directly in the table and relies on `VACUUM`.
- Gap and next-key locks prevent phantom inserts.
- Different database systems implement MVCC in fundamentally different but equally valid ways.

## References

1. MySQL Reference Manual – InnoDB Storage Engine.
2. MySQL Reference Manual – Locking and Transaction Model.
3. Experiments run locally on MySQL/InnoDB 9.6.0 using:
   - 50,000-row test table
   - EXPLAIN query analysis
   - Two-session gap-lock demonstration
   - `performance_schema.data_locks`