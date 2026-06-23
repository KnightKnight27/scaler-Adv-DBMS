# MiniDB Engine Performance & Benchmarking Report

This report analyzes the raw benchmark performance results for the MiniDB transactional database engine across its core storage, indexing, query optimization, logging/recovery, and distributed replication layers.

---

## 1. Summary Performance Metrics

| Engine Layer / Benchmark Component | Metric Evaluated | Performance Result | Notes / Insights |
| :--- | :--- | :--- | :--- |
| **Slotted Page Insert** | Throughput (ops/sec) | **991,572 ops/sec** | Heavy memory copy (`std::memcpy`) on FSP allocations. |
| **Slotted Page Compaction** | Duration (ms) | **0.0822 ms** | Collects slots, defragments, and slides active data. |
| **Table Scan Lookup (2,000 rows)** | Latency per query | **0.01074 ms** | Sequentially fetches pages, parsing slot metadata. |
| **B+ Tree Index Scan (2,000 rows)** | Latency per query | **0.001004 ms** | Traverses internal nodes to fetch exact slot RID. |
| **B+ Tree Lookup Speedup** | Speedup Ratio | **10.69x** | Exponentially reduces disk page read requirements. |
| **ARIES Crash Recovery** | Log replay speed | **103,700 ops/sec** | Replays 5,000 logs (Analysis + Redo + Undo). |
| **Synchronous Replication** | Throughput | **2,258 ops/sec** | Blocked by network RTT waiting for socket LSN ACKs. |
| **Asynchronous Replication** | Throughput | **19,913 ops/sec** | Non-blocking write broadcasts over TCP streams. |
| **Replication Throughput Ratio** | Speedup Ratio | **8.81x** | Decoupling network latency yields massive throughput. |

---

## 2. Detailed Component Analysis

### A. Index Scans vs Table Scans
As demonstrated by the **10.69x Speedup**, indexing is crucial.
* **Table Scans** ($O(N)$): Must read every slot across all logical data blocks. As database page counts grow, this incurs massive buffer cache eviction and CPU parsing overhead.
* **B+ Tree Index Scans** ($O(\log N)$): The index directly maps search keys to physical `RID` (Page ID, Slot Index) pointers. The query executor fetches only the target page frame, avoiding cache pollution.

### B. Query Optimizer Dynamic Programming Scaling
The cost-based optimizer uses the Selinger dynamic programming algorithm. The microsecond scaling is as follows:
* **3-Way Join:** 41.5 μs
* **4-Way Join:** 129.8 μs
* **5-Way Join:** 435.5 μs
* **6-Way Join:** 1374.8 μs

Because the optimizer estimates costs for subsets using bitmask-keyed memoization, it scales in $O(3^N)$ or $O(2^N)$ rather than $O(N!)$ factorial. However, join queries involving $\ge 6$ tables scale exponentially. For massive queries, database engines usually pivot to genetic or randomized algorithms.

### C. ARIES Recovery Throughput
The recovery engine handles **103,700 ops/sec** on ARIES log parsing. 
Reapplying changes directly to `Page` data frames at designated byte offsets is extremely fast since it operates at raw pointer speeds. The bottleneck is log manager storage flushes, which we mitigate by reading and indexing the log file into memory during restart initialization.

### D. Log Replication Configurations
Synchronous replication is heavily bound by the network roundtrip time (RTT), executing at **2,258 ops/sec**. Every log broadcast blocks the execution thread on a socket read wait.
Asynchronous replication processes at **19,913 ops/sec** (**8.81x speedup**) because the primary pushes bytes onto the TCP sender queue in a non-blocking fashion. For high-write transactional applications, asynchronous or semi-synchronous configurations are preferred to prevent execution threads from stalling on WAN/LAN socket latencies.
