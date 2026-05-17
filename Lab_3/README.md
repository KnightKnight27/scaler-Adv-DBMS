# Lab 3: Clock Sweep Algorithm

**Author:** Siddhant Prasad  
**Roll Number:** 24BCS10255

This lab implements the **Clock Sweep Algorithm** (also known as the **Second-Chance Page Replacement Algorithm**). The algorithm acts as an improvement over the standard FIFO page replacement, where it gives pages a "second chance" before evicting them if they have been recently accessed.

## Overview of Implementation

The `ClockSweepCache` is a template-based C++ class designed to simulate a memory cache buffer:
- **`buffer`**: A `std::vector` of `PageFrame` structures that acts as a circular buffer.
- **`pageMap`**: An `std::unordered_map` that provides $O(1)$ constant time lookups for pages currently residing in the cache.
- **`hand`**: An integer pointer that acts as the "clock hand," sequentially sweeping through the buffer to find the next victim for eviction.

### The `PageFrame` Structure
Each frame inside the buffer holds the following information:
- `key`: The unique identifier for the page.
- `value`: The data associated with the page.
- `referenceBit`: A boolean flag (1 or 0) indicating whether the page has been recently referenced.
- `occupied`: A boolean flag indicating whether the frame contains a valid page.

## Core Methods

### 1. `insert(key, value)`
- If the page already exists in the buffer, its value is updated, and its `referenceBit` is set to `1`.
- If the page is not in the cache, it searches for the first unoccupied frame and places the page there.
- If the buffer is completely full, it triggers the `replacePage()` method.

### 2. `access(key)`
- Returns `std::nullopt` (Page Miss) if the key is not found in the `pageMap`.
- If found (Page Hit), the `referenceBit` of the corresponding frame is set to `1`, signaling that the page is in active use and should not be evicted immediately.

### 3. `replacePage(key, value)`
- Sweeps circularly through the `buffer` using the `hand` variable.
- If the frame at the current `hand` position has a `referenceBit` of `0`, it is evicted. The new page replaces it, gets a `referenceBit` of `1`, and the hand moves to the next frame.
- If the `referenceBit` is `1`, it is reset to `0` (giving it a "second chance"), and the hand continues its sweep to the next frame.

## Compilation and Execution

Since the implementation utilizes `std::optional` introduced in modern C++, you must compile the code using the **C++17** standard or higher.

To build and run the algorithm, use the following commands in your terminal:

```bash
# Compile using g++
g++ -std=c++17 clock_sweep.cpp -o clock_sweep

# Execute the compiled binary
./clock_sweep
```
