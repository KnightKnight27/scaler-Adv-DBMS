<div align="center">

# 🔄 Lab Session 3: Clock Sweep Page Replacement Algorithm in C++
### Simulating PostgreSQL Buffer Pool Page Eviction & Usage Counts

[![C++](https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://www.kernel.org/)

</div>

---

## 👨‍🎓 Student Details
- **Name:** Siddhant Prasad
- **Roll Number:** 24BCS10255

---

## 🎯 Objective
Implement the ClockSweep (Clock) buffer pool page replacement algorithm used in PostgreSQL's buffer manager. Understand how it approximates Least Recently Used (LRU) eviction policy without the overhead of maintaining a sorted, lock-heavy linked list of pages.

---

## 📚 Background
PostgreSQL uses the **ClockSweep** algorithm (not pure LRU) to evict pages from its shared buffer pool (`shared_buffers`). 
- **`usage_count` (0 to 5)**: Each buffer frame carries a usage counter representing its access hotness.
- **Pinning**: Active queries pin pages in memory. A pinned page cannot be evicted.
- **Clock Hand**: A circular sweep pointer tracks the next candidate frame to examine.

### Eviction Flow:
1. The clock hand sweeps through the buffer frames sequentially in a circular loop.
2. If the current frame is **pinned**, it is skipped.
3. If the frame is unpinned:
   - If `usage_count > 0`, the hand decrements its `usage_count` by 1 (giving the page a *"second chance"*) and advances.
   - If `usage_count == 0`, the page is chosen as the **victim for eviction**. The hand advances to the next position, and the sweep terminates.
4. When a cached page is accessed (Page Hit), its `usage_count` is incremented and capped at `5` to prevent a single page from dominating the cache indefinitely.

---

## 💻 Code Implementation

The simulation code is located in [clocksweep.cpp](file:///c:/Users/Siddhant/OneDrive/Desktop/scaler-Adv-DBMS/Lab_3/clocksweep.cpp).

### Compilation and Execution
Compile the C++ code using standard flags:
```bash
g++ -std=c++17 clocksweep.cpp -o clocksweep
```

Execute the binary to trace page eviction events:
```bash
./clocksweep
```

---

## 📊 Dry-Run Execution Trace

For a **4-frame buffer pool** under the access sequence: `1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5`:

```text
[MISS] page 1 loaded into frame 0
[MISS] page 2 loaded into frame 1
[MISS] page 3 loaded into frame 2
[MISS] page 4 loaded into frame 3
[HIT]  page 1 in frame 0 usage=2  (Page 1 accessed again -> usage count incremented to 2)
[HIT]  page 2 in frame 1 usage=2  (Page 2 accessed again -> usage count incremented to 2)

[EVICTION TRIGGERED] page 5 needs to be loaded. ClockSweep initiates eviction scan:
  - Frame 0 (Page 1, usage=2): Unpinned. Decrement usage to 1. Hand advances.
  - Frame 1 (Page 2, usage=2): Unpinned. Decrement usage to 1. Hand advances.
  - Frame 2 (Page 3, usage=1): Unpinned. Decrement usage to 0. Hand advances.
  - Frame 3 (Page 4, usage=1): Unpinned. Decrement usage to 0. Hand advances.
  - Frame 0 (Page 1, usage=1): Unpinned. Decrement usage to 0. Hand advances.
  - Frame 1 (Page 2, usage=1): Unpinned. Decrement usage to 0. Hand advances.
  - Frame 2 (Page 3, usage=0): Unpinned. usage_count is 0! Page 3 is evicted from Frame 2.
[EVICT] page 3 from frame 2
[MISS] page 5 loaded into frame 2
```

---

## ⚖️ PostgreSQL ClockSweep vs. LRU

| Feature / Dimension | 🔄 LRU (Least Recently Used) | 🕒 ClockSweep (Second-Chance) |
| :--- | :--- | :--- |
| **Eviction Quality** | Optimal (strictly evicts the least recently accessed page). | Near-optimal (approximates recency based on sweep ticks). |
| **Data Structure** | Doubly-linked list (for order tracking) + HashMap (for lookup). | Fixed-size circular array of frames. |
| **Runtime Overhead** | High. Modifies pointer chains on *every* single page read/write. | Extremely low. Modifies a single usage integer on page access. |
| **Concurrency & Locks** | Poor. Requires global mutex lock on pointer updates, causing contention. | High. Can be implemented using lock-free atomic CAS operations. |
| **Sequential Scan Flooding** | Highly vulnerable. A single full-table scan evicts the entire hot cache. | Protected. `usage_count` is capped at 5; cold scan pages get `usage=1` and are easily swept. |

---

## 🏁 Key Takeaways
- **Performance Tradeoff**: ClockSweep trades perfect recency tracking for lower execution overhead and greatly reduced lock contention.
- **Lower Maintenance**: The rotating clock hand eliminates expensive linked-list reorder operations on every cache read/write.
- **Usage Counting**: Utilizing a multi-value `usage_count` (0-5) instead of a single binary reference bit allows the engine to track page "hotness" with finer granularity.
- **Production Standard**: This implementation reflects the core freelist mechanics found in PostgreSQL's source database engine (`src/backend/storage/buffer/freelist.c`).
