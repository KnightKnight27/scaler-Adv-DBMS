# PostgreSQL Internals

## Student Details
- Name: Harshit Tiwari
- Roll Number: 24BCS10277

## Request Flow
1. A client sends SQL through a connection.
2. PostgreSQL parses the SQL into a parse tree.
3. The planner estimates costs and chooses a physical plan.
4. The executor pulls tuples from plan nodes.
5. The buffer manager loads needed pages from disk into shared buffers.
6. WAL records are generated before dirty pages are flushed.

## MVCC
PostgreSQL does not overwrite a row in-place during an update. Instead, it creates a new tuple version and marks visibility using transaction metadata. This lets readers continue seeing their snapshot while writers create newer versions.

Important effects:
- Readers and writers usually do not block each other.
- Long-running transactions can delay cleanup.
- Vacuum is needed to reclaim dead tuples.
- Index entries may point to multiple tuple versions until cleanup.

## Buffer Manager
Shared buffers cache database pages. When a query needs a tuple, PostgreSQL checks whether the page is already in memory. If not, it reads the page from disk. Dirty pages are eventually written back by checkpoints or background writer activity.

## WAL And Recovery
Write-Ahead Logging means PostgreSQL records the change in WAL before the changed data page is considered durable. After a crash, WAL replay brings data files back to a consistent state.

## Design Tradeoffs
- MVCC improves concurrency but creates dead tuples.
- WAL improves durability but adds sequential write overhead.
- Shared buffers reduce random disk I/O but need careful memory sizing.
- Query planning gives flexibility but depends on accurate statistics.

## Key Takeaway
PostgreSQL is designed around correctness under concurrency. MVCC, WAL, buffer management, and vacuum work together to provide transactional behavior at scale.
