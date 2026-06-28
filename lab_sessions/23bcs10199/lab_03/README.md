# Lab 3 — Clock Sweep Buffer Pool Page Replacement Algorithm

**Student:** Indrajeet Yadav | **Roll No:** 23BCS10199

---

## Objective

Implement the **ClockSweep** algorithm used in PostgreSQL's shared buffer manager, understand why it was chosen over LRU, and observe how it handles sequential scan flooding — the pathological case that destroys LRU's performance in database workloads.

---

## Build & Run

```bash
g++ -std=c++17 -Wall -Wextra -O2 clocksweep.cpp -o clocksweep
./clocksweep
```

---

## Background: Why Buffer Pools Exist

A database engine cannot afford to go to disk for every page access. A single B-tree lookup on a 1 M-row table might touch 3–4 pages (root → interior → leaf). If each page fetch costs a disk seek (~4 ms on HDD, ~100 µs on NVMe SSD), and a query touches 10,000 pages, disk I/O alone would take 40 seconds.

The solution is a **buffer pool**: a fixed-size RAM region divided into frames, each holding one database page. The buffer manager keeps hot pages in RAM and evicts cold pages when new pages must be loaded.

```
Application Layer
      │
      ▼
Buffer Manager (this lab)
      │
      ├─ Page in pool? ──YES──► return pointer to frame (no disk I/O)
      │
      └─ Page NOT in pool?
              │
              ▼
         Clock Sweep → find victim frame
              │
              ├─ if dirty: flush victim to disk (WAL write)
              └─ load new page from disk into victim frame
```

PostgreSQL calls this the **shared buffer pool**, controlled by the `shared_buffers` configuration parameter (typically 25% of RAM).

---

## The Clock Sweep Algorithm

### Why Not LRU?

Pure LRU maintains an ordered doubly-linked list of frames: every page access moves that frame to the front of the list (O(1) with a hashmap), and eviction always takes the tail. The problem:

1. **Lock contention** — every page access requires updating a shared linked list. At 10,000 transactions/second, this is a bottleneck.
2. **Sequential scan flooding** — a full table scan touches N pages (where N > buffer pool size) in strict sequential order. LRU evicts ALL hot pages as the scan runs, destroying the pool's effectiveness for everything else.

### The Clock Sweep Solution

The buffer pool frames are arranged in a **circular array**. A "clock hand" sweeps through them:

```
        Frame [0] page=A  usage=2
                    ↑
               clock hand

    Frame [4] page=E        Frame [1] page=B
      usage=0               usage=1

    Frame [3] page=D        Frame [2] page=C
      usage=1               usage=3
```

On every miss (a page not in the pool), the clock sweep runs:

```
while (frames_checked < 2 * pool_size):
    frame = frames[hand]

    if frame.pinned:
        advance hand, continue          # skip — someone is using it

    if frame.usage_count > 0:
        frame.usage_count--             # second chance: decrement and skip
        advance hand, continue

    if frame.usage_count == 0:
        EVICT this frame                # victim found
        advance hand past victim
        return victim
```

### The Usage Count

Unlike the simple "reference bit" in textbook Clock, PostgreSQL uses a `usage_count` integer (0–5). This provides finer granularity:

- A page accessed once gets `usage_count = 1`.
- A page accessed N more times gets `usage_count = min(count + N, 5)`.
- Every time the clock hand passes a page without evicting it, `usage_count` is decremented by 1.
- Only when `usage_count` reaches 0 can the page be evicted.

This means a "hot" page (accessed frequently, usage_count = 5) needs the clock hand to pass it **5 times** without re-access before it becomes evictable.

---

## Why Two Full Sweeps?

The algorithm limits itself to `2 × capacity` clock hand advances before declaring failure (all frames pinned). Why two?

After **one full sweep**, every frame with `usage_count > 0` has been decremented at least once. A frame with `usage_count = 5` now has `usage_count = 4`.

After **two full sweeps**, even a frame with `usage_count = max (5)` has been decremented twice. But since we stop as soon as we find `usage_count == 0`, in practice the victim is found in far fewer than 2N advances for realistic access patterns.

---

## Sequential Scan Flooding: ClockSweep vs LRU

Consider a 4-frame pool with pages A, B, C, D loaded (all used frequently). Now a `SELECT * FROM large_table` scans 1,000 pages in order.

### LRU behavior

```
Scan page 1   → evict D (LRU), load 1
Scan page 2   → evict C (LRU), load 2
Scan page 3   → evict B (LRU), load 3
Scan page 4   → evict A (LRU), load 4
Scan page 5   → evict 1 (LRU), load 5
... all hot pages gone within 4 scan pages ...
```

LRU is completely polluted. After the scan, a lookup for page A misses.

### ClockSweep behavior

```
Scan page 1   → usage_count[A]=2,B=1,C=1,D=1 (all get decremented, lowest evicted)
                  hot pages have usage_count 2-5 → survive the initial sweep
Scan page 2   → next victim is the cold scan page (usage_count just set to 1)
...
After 5 scan pages: pages with original usage_count 5 still have count 0
  → hot pages survive proportionally longer than cold scan pages
```

The `usage_count` cap (5) means a scan can only increment each hot page's count by 1 per pass, not raise it to 5. Hot pages that were actually frequently accessed before the scan began entered with high counts and survive longer.

---

## Data Structures

```cpp
struct Frame {
    int  page_id;      // which database page is loaded (-1 = empty)
    int  usage_count;  // 0–5; decremented by clock sweep, incremented on access
    bool pinned;       // true = active query is using this page, cannot evict
    bool dirty;        // true = page has been modified, must flush before eviction
};

class BufferPool {
    std::vector<Frame>           frames_;     // circular array
    std::unordered_map<int,int>  page_map_;   // page_id → frame index (O(1) lookup)
    int                          hand_;       // current clock hand position
};
```

The `page_map_` is essential: it gives O(1) lookup to check if a page is already in the pool (the cache hit path). Without it, we'd have to scan all frames linearly on every fetch.

---

## Pin Count (Simplified in This Lab)

In the real PostgreSQL buffer manager, each frame has a **pin count** (not a boolean). When a query starts using a page, it increments the pin count. When it's done with that page, it decrements it. The page can only be evicted when the pin count reaches 0.

This handles the case where two concurrent queries are both using the same page: the second query pins it; when the first finishes, the count drops to 1 (not 0); the page is still protected.

In this lab, `pinned` is a boolean for simplicity — it captures the same concept without the concurrent mutation complexity.

---

## Dirty Page Eviction

In a real database, when the clock sweep selects a dirty page as the victim, it cannot simply overwrite the frame. It must:

1. **Write the dirty page to the WAL** (Write-Ahead Log) or flush it to its heap page location on disk.
2. Wait for the write to complete.
3. Only then overwrite the frame with the new page.

This is why PostgreSQL's background writer (`bgwriter`) proactively writes dirty pages before they become eviction candidates — to reduce the latency spike when the clock sweep finds a dirty victim.

```
Clock Sweep finds victim:
    if dirty:
        smgrwrite(victim.page_data)  → write to disk
        wait for I/O to complete
    smgrread(new_page_data)          → read new page from disk
    load into victim frame
```

The simulation in `clocksweep.cpp` prints a `[FLUSH]` message to represent this disk write step.

---

## PostgreSQL Source Reference

The actual implementation lives in:

```
postgresql/src/backend/storage/buffer/freelist.c
  └── StrategyGetBuffer()
        └── ClockSweepTick()    ← decrements usage_count, advances hand
```

Key macros:
```c
/* in buf_internals.h */
#define BUF_USAGECOUNT_ONE         (1 << 22)
#define BUF_USAGECOUNT_MASK        (0x1F << 22)
#define BUF_USAGECOUNT_MAX         5
```

The usage_count is packed into a `uint32` along with other flag bits for efficiency, but the logical behavior is identical to our implementation.

---

## ClockSweep vs LRU vs LFU — Comparison Table

| Property | LRU | LFU | ClockSweep |
|----------|-----|-----|-----------|
| Eviction quality | Optimal for recency | Optimal for frequency | Near-optimal (approximate recency) |
| Data structure | Doubly-linked list + hashmap | Min-heap / counter map | Circular array |
| Time per access | O(1) but list update needed | O(log n) heap update | O(1) increment only |
| Lock contention | High (shared list write per access) | High (shared counter write) | Low (atomic counter increment) |
| Sequential scan | Destroys pool (no protection) | Somewhat resistant | Good protection (usage cap) |
| Memory overhead | 3 pointers + counter per frame | Counter per frame | 1 counter per frame |
| Used by | Many in-memory caches | Web caches (CDN) | PostgreSQL, many DBMS |

---

## Key Takeaways

1. **Clock Sweep trades perfect recency tracking for lower contention and overhead.** A simple counter decrement is far cheaper than a linked-list reorder, especially under high concurrency.

2. **The circular hand means no expensive list maintenance.** The O(1) work per access (increment) is write-once; the sweep amortizes the scan cost across many misses.

3. **`usage_count` (not a single bit) gives finer-grained hotness tracking.** A page accessed 5 times survives 5 clock passes after the last access; a page accessed once evicts after the very next sweep.

4. **Sequential scan flooding is bounded.** Each scan page enters with `usage_count=1` and no prior history. Hot pages with `usage_count=5` survive 5 more sweeps even if never re-accessed. This makes ClockSweep far more robust than LRU for mixed OLTP + OLAP workloads.

5. **This is the exact algorithm in PostgreSQL** — one of the most battle-tested database engines in the world chose ClockSweep over LRU for exactly these reasons.
