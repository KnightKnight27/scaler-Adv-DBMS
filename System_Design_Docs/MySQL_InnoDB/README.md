# Topic 3: MySQL / InnoDB Storage Engine

## 1. Problem Background
InnoDB was developed to provide ACID-compliant transaction capabilities, row-level locking, and crash recovery for MySQL, overcoming the limitations of the older MyISAM engine which only supported table-level locking and lacked transaction support.

## 2. Architecture Overview
InnoDB operates as a modular storage engine within MySQL.
- **Buffer Pool**: Caches data and index pages.
- **Log Buffer**: Caches redo log data.
- **System Tablespace**: Contains the data dictionary and undo logs.

## 3. Internal Design
### Clustered Indexes
In InnoDB, every table has a clustered index (usually the primary key). The leaf nodes of the clustered index contain the actual row data. Secondary indexes contain the indexed column values and the primary key value (not a physical pointer to the row).

### Undo and Redo Logs
- **Undo Logs**: Store old versions of data. They are used for rolling back transactions and providing older snapshots for MVCC.
- **Redo Logs**: A physical log of changes made to pages. Ensures durability and allows crash recovery.

### Concurrency and Locking
InnoDB uses row-level locking. To prevent phantom reads, it employs "Gap Locks" and "Next-Key Locks," which lock the index records as well as the gaps between them.

## 4. Design Trade-Offs
- **Clustered Indexes**: 
  - *Advantage*: Primary key lookups are extremely fast because data is right there on the leaf node.
  - *Limitation*: Secondary index lookups require a "double lookup" (first find the PK in the secondary index, then find the row in the clustered index). Updates to the primary key can cause expensive page splits.
- **In-place Updates vs PostgreSQL MVCC**: InnoDB updates rows in-place in the clustered index and writes the old version to the undo log. This prevents table bloat (unlike PostgreSQL) but makes undo log management complex and can slow down long-running read transactions.

## 5. Experiments / Observations
Benchmarking InnoDB typically shows superior performance on primary key lookups compared to heap-based storage engines (like PostgreSQL). However, heavy insertion of non-sequential primary keys (like UUIDs) leads to severe fragmentation and page splits in the clustered index, degrading performance significantly.

## 6. Key Learnings
InnoDB's architecture is highly optimized for primary key access and space efficiency. The decision to use in-place updates with an undo log for MVCC presents a completely different set of trade-offs compared to PostgreSQL, emphasizing the fact that there is no single "best" way to implement database concurrency.
