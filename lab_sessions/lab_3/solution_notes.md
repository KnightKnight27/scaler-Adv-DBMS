# Lab 3 — ClockSweep Page Replacement Algorithm

## Concept

A database buffer pool keeps frequently used disk pages in RAM to avoid expensive disk I/O. When all frames are occupied and a new page is needed, the buffer manager must pick a victim to evict. PostgreSQL uses the **ClockSweep** algorithm instead of true LRU.

ClockSweep approximates LRU using a circular array of frames and a `usage_count` per frame (0–5). A "clock hand" sweeps through frames:
- If `usage_count > 0`: decrement it and move on (second chance).
- If `usage_count == 0` and not pinned: evict this frame.

Pages that are accessed frequently accumulate higher usage counts and survive multiple sweeps — approximating recency without the cost of maintaining a sorted list.

## Approach

1. Model each buffer frame as a struct with `page_id`, `usage_count`, and `pinned` flag.
2. Maintain a hash map from `page_id → frame index` for O(1) lookup.
3. On `fetch()`: if the page is already in pool → increment usage count (HIT). Otherwise → run clocksweep to find a victim → evict → load new page (MISS).
4. The clocksweep scans at most `2 × capacity` frames (two full passes) to guarantee termination even when all usage counts start at 5.
5. Add `pin()` / `unpin()` so the buffer manager can protect pages being actively used.

## Solution

`clocksweep.cpp` implements a 4-frame `BufferPool` class with:
- `fetch(page_id)` — load page, print HIT/MISS/EVICT
- `pin(page_id)` / `unpin(page_id)` — prevent a frame from being evicted
- `print_state()` — show all frames, usage counts, and clock hand position

### Trace for access sequence `1 2 3 4 1 2 5 1 2 3 4 5`:
```
Pages 1,2,3,4 → cold MISS, fill all 4 frames (usage=1 each)
Pages 1,2     → HIT, usage bumped to 2
Page 5        → MISS: clocksweep decrements usage counts, evicts page 3 (hits 0 first)
Pages 1,2     → HIT again (usage drops back to 1)
Page 3        → MISS: evicts page 4
Page 4        → MISS: evicts page 1 (pages 2 and 5 still have usage > 0)
Page 5        → HIT
```

Pages 1 and 2 survived longer than pages 3 and 4 because they were accessed more — ClockSweep captured that recency without a sorted list.

## Why ClockSweep over LRU

| | LRU | ClockSweep |
|---|---|---|
| Data structure | Doubly-linked list + hashmap | Circular array |
| Time per access | O(1) but list pointer updates under lock | O(1), only atomic increment |
| Sequential scan | Entire working set evicted | Usage cap (5) limits damage |
| Implementation | Complex | ~30 lines |

PostgreSQL's implementation lives in `src/backend/storage/buffer/freelist.c`.
