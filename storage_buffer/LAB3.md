# Lab 3: Clock Sweep Cache Replacement

## Overview

This lab implements the **Clock Sweep** (second-chance) page replacement algorithm. The cache maintains a circular list of frames, each with a reference bit. On eviction, a clock hand sweeps frames: pages with `ref=1` get a second chance (bit cleared); pages with `ref=0` are evicted.

## Build and Run

```bash
cd storage_buffer
mkdir -p build && cd build
cmake .. && make
./storage_buffer
```

## What the Demo Shows

The program runs two phases:

1. **Reference string** `{7,0,1,2,0,3,0,4,2,3,0,3,2}` with cache size 4 — interleaved `getKey()` (access) and `putKey()` (load on miss). Logs include cache hits, misses, ref-bit clears, and evictions.
2. **Skewed access** — pages 0 and 3 are accessed repeatedly. Clock Sweep retains frequently used pages because their reference bits stay set.

## Clock Sweep vs FIFO vs LRU

| Policy | Mechanism | Eviction choice | Overhead |
| :--- | :--- | :--- | :--- |
| **FIFO** | Queue order | Oldest inserted page | O(1) — simple queue |
| **LRU** | Access timestamp/order | Least recently used page | O(1) with linked list, higher memory for timestamps |
| **Clock Sweep** | Circular scan + ref bit | First page with ref=0 after clearing ref=1 pages | O(n) worst case per eviction, but approximates LRU with one bit per frame |

**FIFO** would evict page 7 immediately when the cache fills, even if page 7 is about to be reused — it ignores recency. **LRU** would always keep the most recently accessed pages, giving optimal hit rates but requiring per-page access tracking. **Clock Sweep** is a practical middle ground used in OS and database buffer managers (e.g., PostgreSQL's clock sweep for shared buffers): it gives recently accessed pages a second chance via the reference bit without maintaining a full LRU list.

In Phase 2, hot pages 0 and 3 survive evictions because repeated `getKey()` calls keep their reference bits set. Under pure FIFO, page 0 would have been evicted early regardless of its reuse frequency.

## Key Events Logged

- `Hit` — page found in cache; reference bit set to 1
- `Miss` — page not in cache
- `Fault` — page must be loaded (cache miss on putKey)
- `Ref-bit cleared` — clock hand passed a used page, giving it a second chance
- `Evicting` — victim selected (ref=0), page replaced
