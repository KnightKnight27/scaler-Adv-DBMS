# Lab 3: PostgreSQL Clock-Sweep Buffer Eviction Simulator

This repository contains a C++ simulator demonstrating the **Clock-Sweep (Second-Chance)** buffer pool replacement algorithm. This eviction strategy is utilized internally by PostgreSQL to manage its shared buffer pool (`shared_buffers`).

---

## 1. How PostgreSQL's Clock-Sweep Works

In an enterprise database management system (DBMS), the active dataset is often larger than the available physical memory (buffer pool). When a query requests a page that is not in memory (a *buffer pool miss*), the DBMS must:
1. Select an existing page in memory to evict.
2. Flush the evicted page to disk if it was modified (is *dirty*).
3. Read the requested page from disk into the vacated buffer slot.

PostgreSQL's Clock-Sweep balances eviction efficiency with search speed:

### Key Metrics Tracked Per Buffer Frame
* **Page ID (Tag)**: Identifies the disk block mapping to the buffer.
* **Pin Count (Refcount)**: Represents the number of concurrent transactions using this page. **A page cannot be evicted if its pin count is > 0**.
* **Usage Count**: A popularity score from `0` to `5` (where `5` is `BM_MAX_USAGE_COUNT`). Each page hit increments the count.
* **Dirty Flag**: Indicates if the page has been modified.

### The Eviction Hand Iteration
When eviction is required, the clock hand sweeps circularly through the array of buffer descriptors:
1. **If a frame is pinned (`pin_count > 0`)**: The hand skips the frame and moves to the next.
2. **If a frame is unpinned (`pin_count == 0`) and has `usage_count > 0`**: The popularity count is decremented by 1, and the clock hand advances. (This grants the page a "second chance").
3. **If a frame is unpinned (`pin_count == 0`) and has `usage_count == 0`**: This frame is selected as the victim. 
   - If marked *dirty*, its contents are written/flushed to disk.
   - The clock hand advances past the victim to prepare for the next eviction.
   - The frame is returned to be overwritten by the new page.

---

## 2. File Structure

* **`clock_sweep.cpp`**: Contains the source code of the simulator:
  * `BufferHeader`: Struct mirroring PostgreSQL's page metadata headers.
  * `ClockSweepBufferManager`: Class managing page hits, misses, pins, unpins, and Clock-Sweep searches.
  * `main()`: A deterministic simulation scenario showing page loads, hits, unpins, dirty-flag flushes, and circular clock hand advances.

---

## 3. How to Compile and Run

To compile and run the simulation locally on your macOS machine:

```bash
# Compile using C++17 standard
g++ -std=c++17 -Wall -Wextra clock_sweep.cpp -o clock_sweep_sim

# Run the simulation executable
./clock_sweep_sim
```

### Expected Output Walkthrough
The simulator outputs the exact state of the buffer frames and details of the sweeps:
1. **Buffer Filling**: Pages `101`, `102`, and `103` are loaded into an empty pool.
2. **Usage Boost**: Page `101` is pinned multiple times, increasing its `usage_count` to `2`.
3. **Sweeper Decay**: Page `104` is requested, triggering a sweep. The clock hand decrements `usage_count` of page `101` (from `2` to `1`, then to `0`), decrements page `102`'s `usage_count`, skips pinned page `103`, and finally evicts `102`.
4. **Flushing**: Since page `102` was marked dirty during unpin, the console logs the simulated flushing step: `[EVICTION] Evicting Page 102 from Frame 1 (Flushing DIRTY changes to disk...)`.
