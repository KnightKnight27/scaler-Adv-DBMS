# MySQL InnoDB Storage Engine

InnoDB is the default storage engine of MySQL. It is a transactional engine with row-level locking, multi-version concurrency control, and crash recovery built on write-ahead logging. This document describes how InnoDB stores data on disk, manages it in memory, processes transactions, controls concurrency, and recovers after failure, and explains the engineering reasons behind those choices.

## 1. Problem Background

A general-purpose relational engine has to satisfy several requirements at the same time, and several of them pull against each other.

The first requirement is durability. Once a transaction commits, its effects must survive a process crash or a power loss. The naive way to guarantee this is to flush every modified data page to its final location before returning success to the client. Data pages are scattered across the file, so this turns every commit into a set of random writes, and a 16 KB page write is not atomic with respect to a power cut, so a half-written page can corrupt the table.

The second requirement is concurrency. Many transactions read and write at once. A reader must not see another transaction's uncommitted changes, and a long-running report should not block writers or be blocked by them. A locking-only design forces readers and writers to serialize on the same rows, which collapses throughput under mixed read and write load.

The third requirement is recoverability of in-flight work. A crash can occur with some transactions committed, some half-applied, and some pages flushed while others are not. After restart the engine must reconstruct a state in which every committed transaction is present and every uncommitted transaction is absent.

InnoDB's design is the set of mechanisms that resolve these tensions: a buffer pool that absorbs random access, a redo log that converts random commit-time writes into sequential ones, undo logs that let readers reconstruct old versions without blocking writers, a doublewrite area that makes page writes recoverable from torn writes, and a locking scheme that prevents phantoms without serializing everything.

## 2. Architecture Overview

InnoDB splits its state between memory and disk.

In memory, the central structure is the buffer pool, which caches table and index pages. Around it sit the change buffer (deferred secondary-index maintenance), the adaptive hash index (a hash shortcut over hot B-tree pages), and the log buffer (redo records staged before they reach disk).

On disk, InnoDB keeps the system tablespace, per-table tablespaces (the .ibd files created under file-per-table), optional general tablespaces, undo tablespaces holding undo logs, temporary tablespaces, the redo log files, and the doublewrite area. Data and indexes live as B-trees inside tablespaces, divided into pages (16 KB by default).

The data model is index-organized. Every InnoDB table is physically a B-tree keyed on the primary key, and the row data lives in the leaf pages of that B-tree. Secondary indexes are separate B-trees whose leaf entries hold the indexed columns plus the primary-key value as the locator back into the clustered index.

Transactions are coordinated through two log families and an in-place update model. The redo log provides durability and roll-forward recovery. The undo logs provide rollback and supply old row versions for multi-version reads. Locks are taken on index records and on the gaps between them.

## 3. Internal Design

### Storage structures

An InnoDB table is its primary-key B-tree. The leaf level holds complete rows in primary-key order, so the table and its primary index are the same object. This is why InnoDB tables are called clustered. If a table declares no primary key, InnoDB picks the first unique index whose columns are all NOT NULL; if none exists, it generates a hidden 6-byte monotonically increasing row id (DB_ROW_ID) and clusters on that.

Each page has a fixed layout: a file header, a page header, the infimum and supremum boundary records, the user records, free space, a page directory of slots used for binary search within the page, and a file trailer carrying a checksum. Records inside a page are singly linked in key order, and the page directory provides coarse entry points so a lookup does not scan the whole page.

Secondary indexes store the indexed columns followed by the primary-key value. A secondary lookup that needs columns beyond the index and the primary key therefore performs a second descent, into the clustered index, to fetch the row. A query whose every referenced column is contained in the secondary index (a covering index) avoids that second descent.

The choice to cluster on the primary key has direct consequences. Primary-key point lookups end at the row itself with no extra indirection. Primary-key range scans are sequential because rows are physically ordered. The costs are a mandatory physical order (inserts in non-monotonic primary-key order cause page splits and fragmentation) and larger secondary indexes, since each secondary entry carries the full primary-key value.

### Memory management

The buffer pool caches pages and is the layer that makes random access tolerable. Its replacement policy is a variant of LRU implemented as a list split into a young (new) sublist at the head and an old sublist at the tail. The boundary between them is the midpoint. By default the old sublist is about 3/8 (roughly 37 percent) of the pool, controlled by innodb_old_blocks_pct.

A newly read page is inserted at the midpoint, that is, at the head of the old sublist, not at the head of the whole list. A page already in the old sublist is promoted to the young sublist only when it is accessed again. This is the defense against scan pollution: a large table scan reads each of its pages once, those pages enter at the midpoint and age out of the old sublist without ever displacing the hot pages in the young sublist. The innodb_old_blocks_time window (default 1000 ms) strengthens this by requiring a second access after a delay before promotion, so the burst of accesses that a scan makes to a single page while reading it does not by itself promote that page.

The change buffer holds modifications (insert, update, delete-marking) destined for secondary index pages that are not currently in the buffer pool. Rather than read the target page from disk just to modify it, InnoDB records the change and merges it later, when the page is read in for another reason or by background activity. This converts what would be random read-modify-write traffic on secondary indexes into deferred, batched work. It applies only to non-unique secondary indexes, because applying a buffered change to a unique index would require reading the page anyway to check the uniqueness constraint, which defeats the purpose.

The adaptive hash index observes access patterns and builds an in-memory hash index over frequently accessed B-tree pages, so repeated equality lookups can skip the B-tree descent. It is maintained automatically and is a pure optimization layered on top of the B-tree.

The log buffer stages redo records in memory before they are written to the redo log files, so a transaction that generates many small redo records does not issue a separate write per record.

### Index organization

All indexes are B-trees. The clustered index orders the whole table; secondary indexes order their key columns and point back through the primary key. Searches descend from the root to a leaf, using the page directory inside each page for the within-page step. Because secondary index leaves reference rows by primary-key value rather than by physical address, a row that moves to a different page (for example due to a page split) does not require any secondary index to be rewritten, since the logical locator is unchanged.

### Transaction processing and MVCC

InnoDB updates the clustered row in place and keeps the prior content in undo logs. Every clustered index record carries hidden columns: DB_TRX_ID (6 bytes), the id of the transaction that last inserted or updated the record, and DB_ROLL_PTR (7 bytes), a pointer to the undo record that holds the previous version. The hidden DB_ROW_ID (6 bytes) is present only when InnoDB had to generate an internal clustered key. A delete is represented as a special update that sets a delete-mark bit rather than physically removing the record.

A consistent (non-locking) read does not take locks. It uses a read view, a snapshot of which transactions were committed at the moment the view was established. When such a read encounters a clustered record whose DB_TRX_ID is not visible to its read view, it follows DB_ROLL_PTR into the undo chain and reconstructs the version that the read view should see, repeating until it reaches a visible version. This is how readers and writers proceed concurrently without blocking each other.

Secondary index records do not carry DB_TRX_ID or DB_ROLL_PTR and are not updated in place. An update to an indexed column delete-marks the old secondary entry and inserts a new one. Because the secondary entry has no version information of its own, visibility for a secondary lookup is resolved by checking the page's maximum transaction id and, when that is not conclusive, going to the clustered index, reading DB_TRX_ID there, and reconstructing the correct version from undo. A delete-marked or recently-updated secondary entry therefore forces the clustered lookup even when the index would otherwise cover the query.

The purge thread reclaims space in the background. Once no existing read view can still need a given old version, purge removes the corresponding undo records and physically removes records that were only delete-marked. If the workload deletes and inserts at a high, balanced rate, purge can fall behind and the history of old versions (visible as the history list length) grows; innodb_max_purge_lag can throttle incoming work to let purge catch up.

### Concurrency control and locking

InnoDB takes shared (S) and exclusive (X) locks on index records. S locks are mutually compatible; an X lock conflicts with both S and X. Table-level intention locks (IS, IX) are taken before row locks so that a transaction wanting a full-table lock can detect row-level conflicts without scanning every row.

Three lock granularities operate on the index:

| Lock type | What it covers | Effect |
|-----------|----------------|--------|
| Record lock | A single index record | Blocks modification of that record |
| Gap lock | The open interval between two adjacent index records | Blocks inserts into that gap; does not block other gap locks |
| Next-key lock | A record lock plus the gap before that record | Blocks both modification of the record and inserts in the preceding gap |

All row locks are placed on index records. A table with no suitable index is still locked through the hidden clustered index, so locking is always defined in terms of an index.

Under the default REPEATABLE READ isolation level, index scans use next-key locks. By locking both the matching records and the gaps around them, a range query under FOR UPDATE prevents any other transaction from inserting a row that would fall inside the range, which is how phantom rows are prevented. An exception: a unique-index lookup with an equality that matches an existing row takes only a record lock, because a unique index cannot produce a second matching row, so no gap protection is needed.

InnoDB supports the four standard isolation levels:

| Level | Read behavior | Gap / next-key locking | Anomalies allowed |
|-------|---------------|------------------------|-------------------|
| READ UNCOMMITTED | Non-locking, may read an earlier version | None | Dirty reads, non-repeatable reads, phantoms |
| READ COMMITTED | Fresh read view per statement | Disabled except for foreign-key and uniqueness checks | Non-repeatable reads, phantoms |
| REPEATABLE READ (default) | One read view for the whole transaction | Gap and next-key locks used on scans | None of the above under InnoDB's locking |
| SERIALIZABLE | Like REPEATABLE READ, plain SELECT promoted to locking reads | Gap and next-key locks used | None |

The difference between READ COMMITTED and REPEATABLE READ is where the read view is created. REPEATABLE READ establishes one read view at the first read and reuses it, giving a stable snapshot for the whole transaction. READ COMMITTED creates a new read view for each statement, so each statement sees the latest committed state, and it omits gap locking, which raises concurrency at the cost of allowing phantoms.

### Recovery and logging

InnoDB keeps a redo log and undo logs because they perform different jobs. The redo log rolls forward; undo rolls back and supplies snapshots.

The redo log implements write-ahead logging. Before a data page change is allowed to reach the data files, the redo record describing that change is written to the log. Redo is flushed to disk at or before commit, governed by innodb_flush_log_at_trx_commit (a value of 1 flushes and fsyncs at every commit for full durability; other values trade durability for throughput). Positions in the log are tracked by an ever-increasing log sequence number (LSN). A checkpoint records the LSN up to which all changes are guaranteed present in the data files; redo before the checkpoint LSN can be discarded, and recovery starts from the checkpoint LSN. The redo log files form a fixed-capacity ring sized by innodb_redo_log_capacity; when the log approaches its capacity, InnoDB flushes dirty pages more aggressively to advance the checkpoint and free log space.

This is what makes commit cheap and durable at once. At commit InnoDB must force the small, sequential redo records, not the scattered data pages. The dirty data pages are written later, asynchronously, by background flushing. The random writes are removed from the commit path.

The doublewrite buffer addresses torn page writes. A 16 KB page write is not guaranteed atomic against a crash, so a page could be left half old and half new, which the redo log alone cannot fix because redo describes logical changes against a known-good page image. InnoDB first writes the page to a contiguous doublewrite area and only then writes it to its real location. If a crash leaves the real page torn, recovery finds the intact copy in the doublewrite area and uses it as the base before replaying redo.

Crash recovery proceeds in this order. Redo is replayed from the last checkpoint LSN forward, restoring every change (committed or not) that had reached the log, using doublewrite copies to repair any torn pages. After redo, InnoDB rolls back transactions that were not committed at the time of the crash by applying their undo logs. The result is a state containing exactly the committed transactions.

## 4. Design Trade-Offs

Clustered, index-organized storage. The advantage is that primary-key lookups land directly on the row and primary-key range scans are physically sequential. The limitations are that the table has a mandatory physical order, so inserts that do not arrive in primary-key order cause page splits and fragmentation, and every secondary index is larger because it embeds the primary-key value. The engineering decision is to use the primary key as the row locator instead of a physical address; this keeps secondary indexes stable when rows move between pages (only the logical primary-key reference is stored), at the cost of a second clustered-index descent for non-covering secondary lookups. A practical implication is that a short, monotonically increasing primary key reduces both index size and split frequency, while a wide or random primary key inflates every secondary index and fragments inserts.

In-place update with separate undo. Because the live row is updated in place and old versions are kept in a separate undo area, the clustered table does not accumulate dead row versions inside the table pages, so table scans stay dense. The cost is that undo must be purged, and a long-running transaction holds back a read view that keeps old versions alive, which inflates undo and the history list and can slow consistent reads that must walk longer undo chains. This is a different balance from append-style multi-version engines, which write a new row version in the table and rely on a vacuum-style process to remove dead versions in place; there the table can bloat, whereas InnoDB concentrates the version garbage in undo. The decision favors compact tables and fast reads of current data, conditioned on transactions committing promptly.

Redo plus doublewrite for durability. Write-ahead redo logging turns commit-time durability into sequential log writes, which is far cheaper than flushing scattered data pages, and the doublewrite area closes the torn-write hole that redo alone cannot. The cost is that each data page is effectively written twice (once to the doublewrite area, once to its home), increasing write volume. The trade is accepted because it converts an unrecoverable corruption (a torn page) into a recoverable one, and on storage where writes are already atomic at the page level the doublewrite step can be disabled to recover the bandwidth.

MVCC consistent reads versus locking reads. Plain SELECT under MVCC takes no locks and reads a snapshot, so readers and writers do not block each other, which is the main throughput advantage under mixed workloads. The cost is reconstruction work: a read that meets recently changed records follows undo chains to find its visible version, and a read whose snapshot is old keeps undo alive. Locking reads (FOR UPDATE, FOR SHARE) and the SERIALIZABLE level give stronger guarantees by taking locks, at the cost of blocking and higher deadlock probability.

Next-key locking for phantom prevention. Locking gaps as well as records lets REPEATABLE READ prevent phantoms without escalating to full serialization, which is stronger than the SQL standard requires of that level. The cost is reduced insert concurrency: gap locks block inserts into ranges that a scanning transaction has touched, and they widen the surface for lock waits and deadlocks. READ COMMITTED removes gap locking to raise concurrency and accepts phantoms in exchange, which is the right setting for workloads that do not need stable range reads.

Change buffering for secondary indexes. Deferring and batching secondary-index maintenance for pages not in memory reduces random I/O on write-heavy workloads. The cost is that buffered changes must be merged before the affected pages are fully usable and that the buffer occupies space in the system tablespace; it also only applies to non-unique secondary indexes, since unique indexes must read the page to verify the constraint.

## 5. Experiments / Observations

The observations below were measured on MySQL 9.5.0 (InnoDB 9.5.0) with the default 16 KB page size, a 128 MB buffer pool, and the default REPEATABLE READ isolation level. The schema was two InnoDB tables: customers(id INT PRIMARY KEY, city VARCHAR(64)) and orders(id INT PRIMARY KEY, customer_id INT, amount DECIMAL(10,2), KEY idx_customer (customer_id)), populated with 10000 customers and 100000 orders generated by recursive CTEs, with each customer_id appearing in 10 orders. ANALYZE TABLE was run before collecting plans.

Clustered versus secondary access. Three EXPLAIN plans on the orders table show the locator design. A primary-key point lookup, SELECT * FROM orders WHERE id = 42, reports type const, key PRIMARY, rows 1, and an empty Extra, because the primary key is the clustered index and the lookup lands directly on the row. A non-covering secondary lookup, SELECT * FROM orders WHERE customer_id = 42, reports type ref, key idx_customer, rows 10, and an empty Extra. The secondary index locates the 10 matching entries, and because the selected columns are not all contained in idx_customer, each match requires a second descent into the clustered index to fetch the row. A covering secondary lookup over the same predicate, SELECT id, customer_id FROM orders WHERE customer_id = 42, reports type ref, key idx_customer, rows 10, and Extra "Using index". Here every selected column (the primary key id is embedded in every secondary entry, and customer_id is the index key) is present in the secondary index, so the clustered descent is skipped. The optimizer cost reflected the difference: the non-covering plan was estimated at cost 3.5 and the covering plan at cost 1.25 for the same 10 rows.

Gap and next-key locking. Two sessions were used at the default REPEATABLE READ level. Session one ran START TRANSACTION followed by SELECT COUNT(*) FROM orders WHERE customer_id BETWEEN 40 AND 60 FOR UPDATE, which matched 210 rows and held next-key and gap locks across that range on idx_customer, and then held the transaction open. Session two set innodb_lock_wait_timeout to 2 and attempted INSERT INTO orders (id, customer_id, amount) VALUES (100001, 50, 99.99), whose customer_id of 50 falls inside the locked range. The insert blocked and then failed with ERROR 1205 (HY000) "Lock wait timeout exceeded; try restarting transaction" after 2.02 seconds, matching the 2 second timeout. As a control, while session one still held its locks, an INSERT with customer_id 9000 (outside the locked range) succeeded immediately, confirming that the block was specific to the gap covered by the range scan rather than a table-wide lock. This demonstrates phantom prevention through gap and next-key locking on the secondary index.

Undo growth and purge. The History list length value from SHOW ENGINE INNODB STATUS, under the TRANSACTIONS section, was read across the lifecycle of a long read view. At a quiet baseline the value was 3. Opening a transaction with a consistent snapshot (START TRANSACTION WITH CONSISTENT SNAPSHOT followed by a read of orders) did not change the value by itself, confirming that establishing a read view costs nothing until other transactions produce old versions. While that read view stayed open, a second session committed 500 separate single-row UPDATE transactions, and the History list length rose to 501, because purge cannot remove undo records that the still-open read view might need. After the long transaction committed and released its read view, the background purge thread reclaimed the undo and the History list length fell to 0 within about two seconds. This shows the direct relationship between an open read view, retained undo, and purge: undo accumulates for as long as a read view can still reach it and is reclaimed once no read view needs it. A separate observation clarified what the counter measures. Updating all 95001 rows of the table inside a single transaction while a read view was held raised the History list length only to 2, because the counter tracks undo log segments in the rollback segments rather than individual row versions, and one transaction's undo occupies few segments regardless of how many rows it changes.

Buffer pool state. With a 128 MB buffer pool (8191 pages of 16 KB) and the working set fully resident after the load and the queries above, SHOW ENGINE INNODB STATUS reported a buffer pool hit rate of 1000 / 1000 (the maximum on InnoDB's per-1000 scale, indicating that requests were served from the pool without physical reads), with 1651 database pages cached of which 589 were in the old sublist. The old sublist holding roughly 36 percent of the cached pages (589 of 1651) is consistent with the default innodb_old_blocks_pct of about 37 percent, the midpoint-insertion split described in the memory management section.

## 6. Key Learnings

InnoDB is index-organized: the table is the primary-key B-tree, and that single decision explains both its fast primary-key access and the extra clustered lookup that non-covering secondary queries pay.

Durability and performance are reconciled by logging, not by flushing data pages at commit. Redo write-ahead logging makes commit a sequential write, the checkpoint LSN bounds recovery work, and the doublewrite area repairs torn pages that redo alone cannot.

Concurrency comes from keeping old versions in undo rather than from locking reads. Consistent reads reconstruct a snapshot by walking DB_ROLL_PTR through the undo chain, so readers do not block writers, with the standing obligation to commit transactions promptly so purge can reclaim undo.

Locks are an index concept. Record, gap, and next-key locks are all defined on index records, and next-key locking under REPEATABLE READ prevents phantoms at the cost of insert concurrency, a trade that READ COMMITTED reverses by dropping gap locks.

Secondary indexes are second-class for versioning: they hold no transaction columns, are maintained by delete-mark plus insert, and defer visibility decisions to the clustered index, which is why a delete-marked entry can negate the benefit of an otherwise covering index.

## References

- MySQL 8.4 Reference Manual, InnoDB In-Memory Structures: https://dev.mysql.com/doc/refman/8.4/en/innodb-in-memory-structures.html
- MySQL 8.4 Reference Manual, InnoDB On-Disk Structures: https://dev.mysql.com/doc/refman/8.4/en/innodb-on-disk-structures.html
- MySQL 8.4 Reference Manual, The InnoDB Buffer Pool: https://dev.mysql.com/doc/refman/8.4/en/innodb-buffer-pool.html
- MySQL 8.4 Reference Manual, InnoDB Multi-Versioning: https://dev.mysql.com/doc/refman/8.4/en/innodb-multi-versioning.html
- MySQL 8.4 Reference Manual, InnoDB Locking: https://dev.mysql.com/doc/refman/8.4/en/innodb-locking.html
- MySQL 8.4 Reference Manual, Transaction Isolation Levels: https://dev.mysql.com/doc/refman/8.4/en/innodb-transaction-isolation-levels.html
- MySQL 8.4 Reference Manual, Redo Log: https://dev.mysql.com/doc/refman/8.4/en/innodb-redo-log.html
- MySQL 8.4 Reference Manual, Doublewrite Buffer: https://dev.mysql.com/doc/refman/8.4/en/innodb-doublewrite-buffer.html
