# Lab 3: Generic & Thread-Safe Clock Sweep Cache in C++

**Student Name:** Ayush Kumar Patra  
**Roll Number:** 24bcs10474  

---

## 1. Project Overview

This repository contains a robust, generic, and thread-safe implementation of a **Clock Sweep Cache** (Second Chance page replacement algorithm) in C++. 

Beyond the core requirements, this implementation models **real-world database storage buffer pool behavior** by including advanced features such as:
1. **Page Pinning & Unpinning**: Actively pinned frames are skipped by the eviction sweeping mechanism to prevent corruption of pages currently undergoing transactions.
2. **Dirty Page Tracking & Disk Write-back**: Tracks modified pages (dirty bits) and simulates writing them to disk upon eviction, while allowing clean pages to be overwritten instantly.
3. **Background Maintenance Thread**: Simulates page aging by periodically sweeping the cache and decaying the reference bits of inactive frames.
4. **Interactive Cache Visualizer**: A beautiful terminal visualizer that renders the circular clock layout, showing keys, values, reference bits, dirty bits, pin counts, and the current clock hand position.

---

## 2. The Clock Sweep (Second Chance) Eviction Algorithm

The **Clock Sweep algorithm** approximates **Least Recently Used (LRU)** eviction but with significantly less overhead ($O(1)$ lookup and eviction complexity). It acts as a circular queue with a **clock hand** pointing to the current candidate frame.

```
       Circular Traversal
         ┌───> [0] ───┐
         │            ▼
        [3] <── (Hand) [1]
         ▲            │
         └─── [2] <───┘
```

### Eviction Logic Flowchart
1. **Full Cache Check**: When a new page is inserted and the cache is full, the eviction process starts at `clockHand`.
2. **Skip Pinned Pages**: If the candidate frame is pinned (`pinCount > 0`), the clock hand skips it immediately without modifying its reference bit.
3. **Inspect Reference Bit**:
   - **`referenceBit == 0`**: The frame is selected as the **eviction victim**! The clock hand is advanced to the next frame (`(clockHand + 1) % size`), the old entry is erased, and the new page is loaded.
   - **`referenceBit == 1`**: The frame is given a **second chance**. Its reference bit is reset to `0`, the clock hand is advanced, and the loop moves to the next frame.
4. **Guaranteed Termination**: If all frames are unpinned and have a reference bit of `1`, they will all be given a second chance (their reference bits reset to `0`). In the second pass, the first frame visited will have its reference bit as `0` and will be evicted. This guarantees finding a victim in at most $2 \times N$ steps.

---

## 3. Data Structures Used

To achieve $O(1)$ lookups and $O(1)$ evictions while maintaining a circular layout, we combine three data structures:

| Data Structure | Purpose | Rationale |
| :--- | :--- | :--- |
| **`std::vector<Frame>`** | **Circular Clock Buffer** | Stores the physical cache entries sequentially. Allows circular navigation (`index = (index + 1) % size`) which models the clock structure. |
| **`std::unordered_map<Key, size_t>`** | **Index Hash Map** | Maps each generic cache `Key` to its corresponding index in the `std::vector`. This enables $O(1)$ fast lookups during `get()` and `put()` operations. |
| **`std::thread`** | **Background Maintenance** | Runs the background sweeper to periodically age page reference bits. |

### The `Frame` Structure
```cpp
struct Frame {
    Key key;
    Value value;
    bool referenceBit = false; // "Second chance" bit
    bool occupied = false;     // Identifies empty frames
    bool isDirty = false;      // Requires disk write-back
    int pinCount = 0;          // Prevent eviction if > 0
};
```

---

## 4. Thread Synchronization & Multithreading Strategy

In high-concurrency environments like databases and operating systems, cache integrity is paramount. This implementation ensures **thread safety** and **deadlock avoidance** through:

### 1. Unified Lock-Guard Synchronization (`std::mutex`)
Every public entry point (`get()`, `put()`, `pin()`, `unpin()`, `markDirty()`) acquires a `std::lock_guard<std::mutex>` on `cacheMutex`. This ensures exclusive access to both the `std::vector` and the `std::unordered_map`, preventing data corruption or race conditions.

### 2. Cooperative Background Sweeper Aging
The background aging thread periodically modifies the `referenceBit` of inactive frames. To prevent data races:
- It acquires a `std::unique_lock<std::mutex>` on the same `cacheMutex`.
- It performs the sweep quickly and releases the lock.
- It sleeps using `std::condition_variable::wait_for` so it can be awoken instantly during cache shutdown.

### 3. Graceful Destructor Shutdown
To prevent dangling threads or segmentation faults when the cache object is destroyed:
1. The destructor locks the mutex and sets a `stopSweeper` boolean flag to `true`.
2. It signals the condition variable `cvStop.notify_all()`.
3. The background thread wakes up, detects the `stopSweeper` signal, and exits its loop.
4. The destructor calls `sweeperThread.join()` to safely reap the background thread.

---

## 5. Architectural Assumptions Made

1. **Comparable and Hashable Keys**: The `Key` template type must support equality comparison (`operator==`) and have a valid specialization of `std::hash` (satisfied by standard types like `int`, `std::string`, `float`, etc.).
2. **Comparable Values**: The `Value` template type must support `operator==` to check if a value has actually changed when marking an entry dirty during updates.
3. **Database Buffer Pool Exhaustion**: If *all* frames in the cache are pinned and a new insertion is requested, the system cannot evict any frame. In this case, the implementation throws a `std::runtime_error("All occupied frames are pinned!")`. This mimics actual DBMS behavior where transaction threads must wait for pages to be unpinned.
4. **C++17 Requirement**: The `get()` method uses C++17's `std::optional<Value>` to represent cache hits and misses cleanly without returning default-constructed values or throwing exceptions.

---

## 6. How to Build and Run

### Prerequisites
- A modern C++ compiler supporting **C++17** (`g++` >= 8, `clang` >= 7, or MSVC 2017+).
- **CMake** >= 3.10.

### Option A: Standard Direct Compilation (Recommended for Fast Testing)
Navigate to the `Lab 3` directory and run:

```bash
# Compile the files directly with thread support and C++17 enabled
g++ -std=c++17 -Wall -Wextra -pthread main.cpp -o clock_sweep_cache

# Run the test executable
./clock_sweep_cache
```

### Option B: Compilation via CMake
To build using the supplied `CMakeLists.txt`:

```bash
# Create build directory
mkdir build
cd build

# Generate build configurations
cmake ..

# Compile the project
cmake --build .

# Run the compiled binary
./clock_sweep_cache
```

---

## 7. Sample Interactive Visualizer Output

Below is an illustration of the terminal visualization of our Clock Sweep cache state during testing:

```text
============================================================
 CLOCK SWEEP CACHE VISUALIZER (Capacity: 3/3)
------------------------------------------------------------
  Index | Occupied | Key | Value | Ref Bit | Dirty | Pin Count
------------------------------------------------------------
      0 | Yes      |   1 | ValueA |    0    |   0   |    0
      1 | Yes      |   2 | ValueB |    1    |   1   |    1  <-- [Clock Hand]
      2 | Yes      |   4 | ValueD |    1    |   0   |    0
============================================================
 Stats: Hits: 4 | Misses: 2 | Evictions: 1 | Dirty Write-backs: 1 | Hit Rate: 66.7%
============================================================
```

> [!NOTE]
> - `Ref Bit == 1` indicates pages that are actively read/written and are protected on the *first* sweep.
> - `Pin Count > 0` pages are strictly un-evictable and skipped completely.
> - `Dirty == 1` pages will trigger a disk write-back when they are evicted.
