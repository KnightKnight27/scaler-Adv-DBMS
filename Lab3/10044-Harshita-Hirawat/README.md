# Lab 3: ClockSweep Page Replacement

## 1. Aim

The aim of this lab is to implement the ClockSweep page replacement algorithm
used by PostgreSQL's buffer manager and understand how it approximates LRU.

A database keeps frequently used disk pages in a limited memory area called the
buffer pool. When the pool is full, a replacement policy must select a page to
evict so that another page can be loaded.

## 2. Background

Two common replacement policies are:

- **LRU (Least Recently Used):** evicts the page that has not been accessed for
  the longest time.
- **ClockSweep:** arranges frames in a circular order and moves a clock hand
  through them to find a suitable victim.

Exact LRU must update an ordered data structure on every page access.
ClockSweep instead uses a small `usageCount` for each frame, so it is simpler
and cheaper to maintain.

## 3. Implementation

Each frame stores:

```cpp
struct Frame {
    int pageId = -1;
    int usageCount = 0;
    bool pinned = false;
};
```

| Member | Purpose |
|---|---|
| `pageId` | Identifies the page stored in the frame; `-1` means empty |
| `usageCount` | Measures recent or repeated use and is capped at 5 |
| `pinned` | Prevents a frame from being evicted while it is in use |
| `pageToFrame` | Maps a page ID to its frame for fast lookup |
| `clockHand` | Stores the current position of the circular sweep |

The `BufferPool` class supports fetching pages, pinning and unpinning them, and
printing the final state of every frame.

## 4. Page Access

The `fetch(pageId)` method first checks whether the page is already present.

- On a **hit**, its `usageCount` is increased, up to a maximum of 5.
- On a **miss**, ClockSweep finds a free frame or an eviction victim and loads
  the requested page with `usageCount = 1`.
- If every frame is pinned, the method returns `-1` instead of looping forever.

## 5. ClockSweep Eviction

The clock hand moves through the frames in a circle:

```text
frame 0 -> frame 1 -> frame 2 -> frame 3 -> frame 0 -> ...
```

For each frame:

1. If the frame is pinned, it is skipped.
2. If `usageCount > 0`, the count is decreased and the page receives another
   chance.
3. If `usageCount == 0` and the frame is not pinned, it is selected for
   eviction.

Frequently accessed pages build a higher usage count and therefore survive
more sweeps than pages accessed only once.

## 6. Example

The program creates a four-frame buffer pool and uses this access sequence:

```text
1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5
```

Pages 1 and 2 receive additional usage credit because they are accessed again.
When page 5 is requested from a full pool, the clock hand decreases usage
counts until it reaches an unpinned frame with a count of zero.

The output labels each access as a hit or miss, reports evictions, and prints
the final buffer-pool state and clock-hand position.

## 7. Sequential Flooding

Sequential flooding occurs when a scan reads more one-time pages than the
buffer pool can hold:

```text
1, 2, 3, 4, 5, 6, 7, 8, ...
```

Exact LRU treats every scan page as recently used, even if it will never be
needed again. These pages can push useful pages out of the cache.

ClockSweep gives one-time scan pages only a small amount of usage credit. Their
counts quickly reach zero, while repeatedly accessed pages survive longer.
This makes ClockSweep more resistant to cache pollution from large scans.

## 8. ClockSweep Compared with LRU

| Property | LRU | ClockSweep |
|---|---|---|
| Recency tracking | Exact | Approximate |
| Data structure | Ordered list plus lookup map | Circular frame array |
| Work on a hit | Reorder the page | Increase a small counter |
| Sequential scans | Can replace useful pages | One-time pages lose credit quickly |

## 9. Limitations

This is a learning implementation, so:

- `pinned` is a boolean instead of a production pin count.
- It does not store actual page bytes or dirty-page information.
- It does not write evicted dirty pages back to disk.
- Integer page IDs are used instead of real disk block addresses.

These simplifications do not change the main ClockSweep replacement logic.

## 10. Compile and Run

```bash
g++ -std=c++17 -Wall -Wextra clocksweep.cpp -o clocksweep
./clocksweep
```

## 11. Conclusion

ClockSweep approximates LRU without maintaining an exact recency list. The
circular hand, capped usage counts, and pin checks provide a simple replacement
policy in which frequently used pages receive more chances to remain in the
buffer pool.
