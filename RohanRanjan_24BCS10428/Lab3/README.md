# Lab 3 — ClockSweep Page Replacement Algorithm

**Rohan Ranjan — 24BCS10428**

## Objective
Implement the ClockSweep (Clock) buffer-pool page-replacement algorithm used in
PostgreSQL's buffer manager, and understand how it approximates LRU without the
overhead of maintaining an ordered list.

## Build & run
```bash
g++ -std=c++17 -o clocksweep clocksweep.cpp
./clocksweep
```

## Background
PostgreSQL uses ClockSweep (not LRU). Each buffer frame carries a `usage_count` (0–5).
A circular "clock hand" sweeps frames:
- `usage_count > 0` → decrement and move on (the page gets a *second chance*).
- `usage_count == 0` and the frame is not pinned → evict it.

Every access to a resident page increments its `usage_count` (capped at 5).

## Trace (4-frame pool, accesses `1 2 3 4 1 2 5`)
```
MISS  page 1 -> frame 0  usage=1
MISS  page 2 -> frame 1  usage=1
MISS  page 3 -> frame 2  usage=1
MISS  page 4 -> frame 3  usage=1
HIT   page 1 -> frame 0  usage=2
HIT   page 2 -> frame 1  usage=2
MISS  page 5 -> ClockSweep:
        frame 0: usage 2 -> 1, skip
        frame 1: usage 2 -> 1, skip
        frame 2: usage 1 -> 0, skip
        frame 3: usage 1 -> 0, skip
        ...evicts the first frame that reaches 0
```
Pages 1 and 2 survive longer thanks to their higher usage counts — ClockSweep
approximates LRU without a sorted structure.

## Why PostgreSQL uses ClockSweep over LRU
| Property              | LRU                           | ClockSweep                          |
|-----------------------|-------------------------------|-------------------------------------|
| Eviction quality      | Optimal (exact recency)       | Near-optimal (approximate recency)  |
| Data structure        | Doubly-linked list + hashmap  | Circular array                      |
| Time per access       | O(1) but with lock contention | O(1), lock-free on usage_count      |
| Sequential-scan flood | Wrecks LRU (all pages evict)  | usage_count cap limits the damage   |

The `usage_count` cap also protects against sequential-scan flooding: a full table scan
increments each page's count by only 1, so hot pages (count = 5) survive the sweep.

## Key takeaways
- ClockSweep trades perfect recency tracking for lower overhead and lock contention.
- The circular hand means no expensive list maintenance on every access.
- `usage_count` (not a single reference bit) gives finer-grained "hotness" tracking.
- This is the exact algorithm in `src/backend/storage/buffer/freelist.c` in PostgreSQL.
