# Lab 3: Second-Chance Buffer Pool Replacer in C++

**Student Name:** [Insert Friend's Name Here]  
**Roll Number:** [Insert Friend's Roll Number Here]  

---

## 1. Executive Summary

This repository contains a modern, template-driven, and highly-concurrent implementation of a **Second-Chance (Clock Sweep) Buffer Replacer** in C++.

Designed to simulate storage subsystem behavior in high-performance database engines (DBMS) and operating systems, this module optimizes cache operations through:
- **Generic Key-Value Architecture**: Uses C++ templates to seamlessly handle multiple types (e.g. `int`, `std::string`, or customized page block structures).
- **Block Lock Synchronization (Pinning)**: Restricts specific frames from being evicted when they are locked by active database transaction processes, returning a buffer pool exhaustion exception if the cache is fully locked.
- **Dirty Block Write-back Mechanism**: Efficiently identifies if cached blocks have been modified, initiating a simulated physical disk flush when evicted while avoiding disk write-backs for unmodified clean pages.
- **Asynchronous Aging Worker**: Periodically executes background sweeps to decay the second-chance bits of older, inactive pages, modeling actual kernel page cleaner processes.
- **Real-time Performance Visualizer**: Renders beautiful terminal status charts detailing occupied slots, key-value sets, lock status, modification flags, and the position of the sweep hand.

---

## 2. Theoretical Background: The Second-Chance Caching Mechanism

The **Second-Chance (Clock) replacement policy** is a highly efficient approximation of the Least Recently Used (LRU) algorithm. Unlike LRU, which requires costly data movement (like moving nodes in a doubly-linked list on every hit), the Clock Sweep policy operates in **$O(1)$ constant time complexity** by maintaining entries in a circular queue.

```
       Circular Buffer Scanning
          ┌─── [Slot 0] ───┐
          │                ▼
       [Slot 3] <── (Hand) [Slot 1]
          ▲                │
          └─── [Slot 2] ───┘
```

### Operation Algorithm
1. **Lookup & Hits**: When an active page is loaded or updated, its `secondChance` bit is immediately toggled to `true`.
2. **Eviction Loop**: When insertion is requested and capacity is reached:
   - The replacer inspects the block pointed to by the circular `pointerIndex`.
   - **Skip Locked Pages**: If the block is pinned (`pinRefs > 0`), the pointer bypasses it immediately without resetting its reference bit.
   - **`secondChance == false`**: The block is marked as the eviction victim. The old key mapping is cleared, the dirty block is flushed if modified, and the new key is placed here. The pointer advances to the next slot.
   - **`secondChance == true`**: The block is given a "second chance". Its bit is toggled to `false`, and the pointer advances to continue searching.
3. **Guaranteed Execution**: This circular sweep guarantees that if all blocks are unpinned and have a bit of `1`, they will be cleared in the first sweep, and the very first block will be evicted on the second sweep.

---

## 3. Data Layout and Implementation

To ensure $O(1)$ lookup and $O(1)$ eviction capabilities, the replacer operates on three interconnected components:

| Component | Class Type | Specific Responsibility |
| :--- | :--- | :--- |
| **Circular Buffer Pool** | `std::vector<CacheBlock>` | Sequentially holds physical cache data. Circular iteration is achieved using index math: `(idx + 1) % capacity`. |
| **Lookup Dictionary** | `std::unordered_map<Key, size_t>` | Maps generic cache keys directly to their array indexes inside the circular pool, yielding $O(1)$ lookup speed. |
| **Aging Worker** | `std::thread` | Runs the janitor process asynchronously to decay second-chance bits. |

### The `CacheBlock` Layout
```cpp
struct CacheBlock {
    Key pageKey;
    Value data;
    bool secondChance = false; // referenced bit
    bool isEmpty = true;        // unoccupied block indicator
    bool dirtyFlag = false;     // modified page indicator
    int pinRefs = 0;           // locking counter
};
```

---

## 4. Multi-threaded Safety & Janitor Synchronization

To prevent data corruption under concurrent load, the cache features a bulletproof synchronization design:

1. **Mutual Exclusion locking (`std::mutex`)**: Every public-facing API (`get()`, `put()`, `pin()`, `unpin()`, `markDirty()`) acquires a local `std::lock_guard<std::mutex>` on the class lock variable. This protects both the dictionary map and the block array, making transactions thread-safe.
2. **Cooperative Janitor Loop**: The background cleaning worker utilizes the same mutex to perform fast scans. It uses `std::condition_variable::wait_for` to handle sleep periods, ensuring it wakes up immediately upon system shutdown to avoid hanging threads.
3. **Leak-free Shutdown Sequence**:
   - The class destructor locks the mutex and raises the `stopJanitor` boolean flag.
   - It fires `cv.notify_all()` to wake up the worker.
   - The worker exits safely, and the destructor calls `janitorThread.join()` to clean up.

---

## 5. Compilation and Execution Guidelines

### Hardware/Software Requirements
- A standard C++ compiler with C++17 support (GCC 8+, Clang 7+, MSVC 2017+).
- **CMake** version 3.12 or newer.

### Method A: Single Command Direct Compilation (Recommended)
You can compile all files directly using standard compilers inside the project directory:

```bash
# Compile code with thread safety and C++17 active
g++ -std=c++17 -Wall -Wextra -pthread main.cpp -o clock_replacer

# Launch verification tests
./clock_replacer
```

### Method B: Building via CMake
To set up a standard build hierarchy:

```bash
# Generate build workspace
mkdir build
cd build

# Generate platform-specific makefiles
cmake ..

# Compile the target binary
cmake --build .

# Run tests
./clock_replacer
```

---

## 6. Sample Diagnostic Visualization Output

Renders clean status details on every main event to help developers trace cache hits, modifications, and pointer rotations:

```text
##############################################################
 BUFFER POOL REPLACER STATUS (Elements: 3 / 3)
--------------------------------------------------------------
  Slot  | State |  Key  |  Value  | Second Chance | Dirty | Pinned
--------------------------------------------------------------
   0    | Active |   10 |    Ten  |      0       |   0   |   0
   1    | Active |   20 |  Twenty |      1       |   1   |   1  <== [Clock Pointer]
   2    | Active |   40 |  Forty  |      1       |   0   |   0
##############################################################
 Metrics: hits: 5 | misses: 2 | evictions: 1 | flushes: 1 | ratio: 71.4%
##############################################################
```
