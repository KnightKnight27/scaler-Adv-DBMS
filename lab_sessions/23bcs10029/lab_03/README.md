# Lab 3: Clock Sweep Page Replacement Algorithm

## What this lab covers

Implementation of PostgreSQL's buffer pool page replacement algorithm — ClockSweep — in C++.

## Build & Run

```bash
g++ -std=c++17 -o clocksweep clocksweep.cpp
./clocksweep
```

## Expected output (4-frame pool, access sequence 1 2 3 4 1 2 5 1 2 3 4 5)

```
[MISS] page 1 loaded into frame 0
[MISS] page 2 loaded into frame 1
[MISS] page 3 loaded into frame 2
[MISS] page 4 loaded into frame 3
[HIT]  page 1 in frame 0 usage=2
[HIT]  page 2 in frame 1 usage=2
[EVICT] page 3 from frame 2
[MISS] page 5 loaded into frame 2
...
```

## Algorithm

The clock hand sweeps frames circularly:
- `usage_count > 0`: decrement and move on (second chance)
- `usage_count == 0` and not pinned: evict this frame

Every cache hit increments `usage_count` (capped at 5).

## Why PostgreSQL uses ClockSweep over LRU

| Property              | LRU                       | ClockSweep                          |
|-----------------------|---------------------------|-------------------------------------|
| Eviction quality      | Exact recency             | Approximate recency                 |
| Data structure        | Doubly-linked list + map  | Circular array                      |
| Lock contention       | High (list update on hit) | Low (atomic usage_count increment)  |
| Sequential scan flood | Wrecks LRU                | Usage count cap limits damage       |

Source: `src/backend/storage/buffer/freelist.c` in PostgreSQL.
