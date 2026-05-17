# Clock Sweep Page Replacement Algorithm

## Objective
This project implements the Clock Sweep buffer replacement algorithm in modern C++17. The Clock algorithm (also known as Second Chance) provides a more efficient approach to page replacement compared to strict LRU by utilizing a circular buffer and a reference bit/counter, avoiding the overhead of maintaining a fully sorted access list.

## Algorithm Explanation
The buffer pool is represented as a circular array (`std::vector`) of frames. Each frame tracks:
- `page_id`: The ID of the page stored.
- `ref_count`: The reference/usage counter (1 when recently accessed).
- `in_use`: A boolean flag indicating if the page is currently pinned.
- `occupied`: A boolean flag indicating if the frame holds a valid page.

A `clock_hand` points to the next candidate frame for eviction.
1. **Cache Hit**: If a requested page is in the buffer (`get()`), its `ref_count` is set to 1 and `in_use` is set to true (pinned).
2. **Cache Miss & Free Space**: The page is placed in an unoccupied frame.
3. **Eviction (Sweep Logic)**: The `clock_hand` scans the buffer:
   - If a frame is unpinned (`!in_use`) and `ref_count == 0`, it is evicted and replaced.
   - If a frame is unpinned but `ref_count > 0`, it is given a second chance (decrement `ref_count`) and the hand moves forward.
   - The hand wraps around the circular buffer (`capacity`) until a victim is found.

## Build Instructions
1. Create a `build` directory and navigate into it:
   ```bash
   mkdir build && cd build
   ```
2. Run CMake:
   ```bash
   cmake ..
   ```
3. Compile the project:
   ```bash
   make
   ```
   *(Alternatively, compile directly with g++: `g++ main.cpp -std=c++17 -o clock_sweep`)*

## Run Instructions
Run the generated executable:
```bash
./clock_sweep
```

## Expected Output
The program prints the step-by-step state of the buffer pool, demonstrating:
- Page insertion.
- Pinning pages (`in_use = Y`).
- Unpinning pages (`release()`).
- The clock hand scanning and decrementing reference counters.
- Page eviction.

## Complexity Analysis
- **Time Complexity**: 
  - `get(page_id)`: **O(1)** on average (using `std::unordered_map`).
  - `put(page_id)`: **O(1)** on average if free space exists or an immediate victim is found. In the worst case (e.g., all frames have `ref_count = 1`), it takes **O(N)** where N is the capacity, to complete a full sweep.
- **Space Complexity**: **O(N)** for the buffer pool (`vector`) and the index (`unordered_map`).
