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

## Code Structure

- [clocksweep.cpp](file:///Users/kushalsacharya/Desktop/scaler-Adv-DBMS/lab%203/clocksweep.cpp): Implements the `BufferPool` with a circular Clock Sweep replacement strategy.
- [Makefile](file:///Users/kushalsacharya/Desktop/scaler-Adv-DBMS/lab%203/Makefile): Build configuration file.

---

## Build and Run Instructions

To compile the application:
```bash
make
```

To run the simulation and trace the algorithm:
```bash
make run
```

To clean build artifacts:
```bash
make clean
```

---

## Trace of the Algorithm

For a 4-frame pool with access sequence `1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5`:

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

---

## Why PostgreSQL uses ClockSweep over LRU

| Property              | LRU                          | ClockSweep                        |
|-----------------------|------------------------------|-----------------------------------|
| **Eviction quality**  | Optimal (exact recency)      | Near-optimal (approximate recency)|
| **Data structure**    | Doubly-linked list + hashmap | Circular array                    |
| **Time per access**   | O(1) but with lock contention| O(1), lock-free on `usage_count`    |
| **Sequential scan**   | Wrecks LRU (all pages evict) | Usage count caps at 5, limits damage|

---

## Key Takeaways
- ClockSweep trades perfect recency tracking for lower overhead and lock contention.
- The circular hand means no expensive list maintenance on every page access.
- `usage_count` (not a single reference bit) gives finer-grained "hotness" tracking.
- This is the exact algorithm in `src/backend/storage/buffer/freelist.c` in the PostgreSQL source.
