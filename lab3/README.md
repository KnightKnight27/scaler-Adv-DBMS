# Lab Session 3: Clock Sweep Page Replacement Algorithm in C++

## Objective
Implement the ClockSweep (Clock) buffer pool page replacement algorithm used in PostgreSQL's buffer manager. Understand how it approximates LRU without the overhead of maintaining an ordered list.

---

## Background

PostgreSQL uses ClockSweep (not LRU) to evict pages from its shared buffer pool. Each buffer frame carries a `usage_count` (0–5). The "clock hand" sweeps through frames in a circular fashion:
- If `usage_count > 0`: decrement and move on (the page got a "second chance").
- If `usage_count == 0` and frame is not pinned: evict this frame.

Every time a page is accessed, its `usage_count` is incremented (capped at 5 in PostgreSQL).

---

## Implementation

The source code is located in [clocksweep.cpp](file:///C:/Users/singh/Downloads/scaler-Adv-DBMS-main/scaler-adv-dmbs/lab3/clocksweep.cpp).

Compile and run:
```bash
g++ -std=c++17 -o clocksweep clocksweep.cpp
./clocksweep
```

---

## Trace of the algorithm

For a 4-frame pool with access sequence `1 2 3 4 1 2 5`:

```
MISS  page 1 -> frame 0  usage=1
MISS  page 2 -> frame 1  usage=1
MISS  page 3 -> frame 2  usage=1
MISS  page 4 -> frame 3  usage=1
HIT   page 1 -> frame 0  usage=2   (hand sweeps, won't evict usage>0)
HIT   page 2 -> frame 1  usage=2
MISS  page 5 -> ClockSweep starts at hand:
        frame 0: usage=2 -> decrement to 1, skip
        frame 1: usage=2 -> decrement to 1, skip
        frame 2: usage=1 -> decrement to 0, skip
        frame 3: usage=1 -> decrement to 0, skip
        frame 0: usage=1 -> decrement to 0, skip
        ...eventually evicts the frame that hits 0 first
```

Pages 1 and 2 survive longer because of their higher usage count — ClockSweep approximates LRU without a sorted structure.

---

## Why PostgreSQL uses ClockSweep over LRU

| Property              | LRU                          | ClockSweep                        |
|-----------------------|------------------------------|-----------------------------------|
| Eviction quality      | Optimal (exact recency)      | Near-optimal (approximate recency)|
| Data structure        | Doubly-linked list + hashmap | Circular array                    |
| Time per access       | O(1) but with lock contention| O(1), lock-free on usage_count    |
| Sequential scan flood | Wrecks LRU (all pages evict) | Usage count caps at 5, limits damage|

The `usage_count` cap also provides natural protection against sequential scan flooding — a full table scan increments each page's count by 1, so hot pages (count=5) survive the sweep.

---

## Key Takeaways
- ClockSweep trades perfect recency tracking for lower overhead and lock contention.
- The circular hand means no expensive list maintenance on every page access.
- `usage_count` (not a single reference bit) gives finer-grained "hotness" tracking.
- This is the exact algorithm in `src/backend/storage/buffer/freelist.c` in the PostgreSQL source.
