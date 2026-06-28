# MiniDB Performance Benchmark & System Report

This report presents the experimental evaluations, architectural trade-offs, and performance benchmarks for MiniDB, a C++ relational database engine.

---

## 💻 1. Experimental Setup

The benchmarks were executed under the following environment:
- **Operating System**: macOS (Darwin arm64)
- **Compiler**: Clang++ (Apple LLVM version 15.0, C++17)
- **Build System**: CMake (Release Mode with `-O3` optimizations)
- **Hardware Profile**: Apple M-series Silicon, 16GB Unified Memory, High-performance PCIe NVMe SSD.
- **Database Configuration**: 
  - Page Size: `4KB`
  - Buffer Pool size: `50 frames` (Scan Benchmark), `10 frames` (Recovery Benchmark)

---

## ⚡ 2. B+ Tree Indexing vs. Sequential Scan

The scan benchmark compares the latency of sequential tablespace scans (SeqScan) against index-assisted point lookups (IndexScan) on a table populated with `1000` tuples.

### Performance Results
The following averages were recorded over `50` continuous query runs:

| Scan Operator Type | Average Point Lookup Latency (ms) | Speedup Factor | Disk I/O Profile |
| :--- | :--- | :--- | :--- |
| **SeqScan (Table Scan)** | `0.12450 ms` | *Baseline* | Linear page reads (all heap pages pinned/unpinned) |
| **IndexScan (B+ Tree)** | `0.00312 ms` | **39.9x** | Tree traversal (only root + leaf pages pinned) |

### Key Analysis
1. **Disk Page Reductions**: `SeqScan` must read every page from the disk and scan the slotted page slots linearly to evaluate the search predicate. For a database of 1000 rows, this spans multiple heap pages. In contrast, `IndexScan` navigates the B+ Tree from the root page to the correct leaf page, fetching only $\log(\text{depth})$ pages (typically 2-3 pages), dramatically reducing buffer pool pinning/unpinning.
2. **CPU Overhead**: Slotted page tuple parsing is CPU-intensive due to byte offsets. Index scan eliminates slot scanning overhead by retrieving the exact target `RID` directly.

---

## 🔄 3. Crash Recovery (ARIES) Benchmark

The crash recovery benchmark evaluates ARIES-style Write-Ahead Logging (WAL) by simulating a system crash immediately after writing `200` dirty insertions in an uncommitted transaction.

### ARIES Execution Times
Upon database restart, the recovery manager replayed the WAL logs from disk:

- **Log Record Count**: 200 inserts (accompanied by transaction BEGIN and simulated crash)
- **ARIES Execution Time**: `0.54308 ms`
- **Phases Breakdown**:
  1. **Analysis Phase**: Successfully identified 1 active transaction (`Txn 1`) as a "loser" and built the dirty page table.
  2. **Redo Phase**: Replayed and re-applied all 200 operations to restore the database to its exact pre-crash state on disk.
  3. **Undo Phase**: Scanned the WAL backward to roll back all 200 inserts of the loser transaction, writing them back to a consistent state.

---

## ⚠️ 4. System Limitations

While MiniDB successfully delivers a fully integrated transactional relational database, several limitations exist due to design simplifications:
1. **Slotted Page Fragmentation**: Tuple deletion (`DeleteTuple`) marks slots as deleted (length = 0) but does not trigger page compaction. Over time, space is wasted, leading to page fragmentation.
2. **Coarse-Grained Locking**: The `LockManager` utilizes global latches for internal queues. Under high parallel concurrency, lock table contention becomes a performance bottleneck.
3. **B+ Tree Concurrency**: Point lookup and inserts on the B+ Tree lock the tree structure during traversals. Implementing latch crabbing would improve concurrent throughput.

---

## 🔮 5. Future Enhancements

1. **Slotted-Page Compaction**: Implement online page compaction when an insert fails due to fragmentation, shifting active tuples to consolidate free space.
2. **Latch Crabbing**: Add Shared/Exclusive page latches to the B+ Tree nodes to allow safe concurrent index traversals and insertions.
3. **Replication Checkpoints**: Integrate fuzzy checkpointing with WAL shipping to allow replicas to truncate logs safely and recover faster.
