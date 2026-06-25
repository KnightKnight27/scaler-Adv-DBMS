# DBMS Buffer Pool Design

A Buffer Pool manages data page transfers between disk and main memory, minimizing slow disk I/O to optimize database performance.

### 1. Core Architecture
* **Buffer Frames:** An in-memory array of fixed-size memory blocks allocated to cache physical disk pages.
* **Page Table:** An in-memory hash map that tracks which disk page currently resides in which buffer frame, enabling $O(1)$ lookups.

### 2. Frame Metadata
* **Pin Count (Reference Count):** Tracks how many concurrent threads are actively using a page. A page cannot be evicted if its pin count > 0.
* **Dirty Bit:** A boolean flag indicating if the page was modified in memory. If true, the page must be written back to disk before being evicted.

### 3. Page Replacement Policy
When the buffer pool is full, an eviction policy selects an unpinned page to free up space. Common strategies include:
* **LRU (Least Recently Used):** Evicts the page accessed furthest in the past.
* **Clock Protocol:** A low-overhead approximation of LRU using a circular buffer and a usage bit.
* **LRU-K:** Evicts based on the time of the $K$-th backward reference to prevent buffer pollution from sequential scans. 