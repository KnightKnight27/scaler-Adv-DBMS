# Lab Session 3: Clock Sweep Page Replacement Algorithm

## Objective
The goal of this lab is to implement the Clock Sweep buffer pool page replacement algorithm. This is the algorithm used in PostgreSQL's buffer manager, and we want to see how it approximates a Least Recently Used (LRU) policy without the heavy overhead of keeping a strictly ordered list.

## Background
PostgreSQL uses the Clock Sweep algorithm, rather than LRU, to decide which pages to evict from its shared buffer pool. In this setup, every buffer frame has a `usage_count` ranging from 0 to 5. A "clock hand" moves continuously across the frames in a circle:
- If a frame has a `usage_count > 0`, the count is decremented, and the hand moves on. This gives the page a "second chance."
- If the `usage_count == 0` and the frame isn't currently pinned by any process, the algorithm evicts it.

Whenever a page is accessed, its `usage_count` goes up, capping at 5 as it does in Postgres.

## Implementation Details
We will write a C++ program that simulates a small buffer pool using this approach. The code is available in `clocksweep.cpp`.

To compile and run the simulation:
```bash
g++ -std=c++17 -o clocksweep clocksweep.cpp
./clocksweep
```

## Trace Example
If we have a 4-frame buffer pool and access pages in the sequence `1 2 3 4 1 2 5`, here is how it plays out:

- Pages 1 through 4 are loaded into the empty frames (0 through 3), starting with a usage count of 1.
- Accessing pages 1 and 2 again increments their usage counts to 2. The clock hand sweeps past them, dropping their counts to 1, but doesn't evict them since the counts were greater than 0.
- When page 5 is requested, a miss occurs, and the algorithm starts searching for a victim from the current hand position.
- The clock hand will decrement the usage counts of frames as it passes them until it finds one that reaches 0. That frame is chosen for eviction, making room for page 5.

This shows how pages 1 and 2 stay in the pool longer because they were accessed more recently and frequently, giving them a higher usage count.

## Why Clock Sweep over LRU?
PostgreSQL opts for Clock Sweep for a few very good reasons:

- **Eviction Quality**: It provides near-optimal recency tracking without needing exact timestamps.
- **Data Structure**: It relies on a simple circular array rather than a complex doubly-linked list paired with a hash map.
- **Performance**: Operations take O(1) time and avoid the lock contention that is common with strictly tracking LRU.
- **Sequential Scans**: In a standard LRU, a sequential scan can flood the cache, evicting all useful pages. With Clock Sweep, the usage count caps at 5, meaning "hot" pages survive full table scans and remain in memory.

## Summary
- Clock Sweep is a great trade-off, giving up perfect recency for better performance and lower lock contention.
- The circular hand approach completely avoids the need to maintain an ordered list on every single page access.
- A granular `usage_count` is much better for tracking how "hot" a page is compared to a single reference bit.
- We can actually find this same algorithm implemented in `src/backend/storage/buffer/freelist.c` within the PostgreSQL source code.
