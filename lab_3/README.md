# Lab 3: ClockSweep Buffer Pool Page Replacement Algorithm

**Student:** Pulasari Jai  
**Roll No:** 24BCS10656  
**Date:** June 23, 2026

---

## Overview

This lab implements PostgreSQL's ClockSweep (Clock) page replacement algorithm for buffer pool management. ClockSweep approximates LRU (Least Recently Used) eviction without the overhead of maintaining an ordered list, using a circular "clock hand" and per-frame usage counts.

---

## Objectives

1. ✅ Understand PostgreSQL's buffer pool management strategy
2. ✅ Implement ClockSweep algorithm with usage_count (0-5)
3. ✅ Handle pinned pages (cannot be evicted)
4. ✅ Demonstrate hot page protection
5. ✅ Compare ClockSweep vs LRU trade-offs

---

## Directory Structure

```
lab_3/
├── README.md          # This file
├── clocksweep.cpp     # ClockSweep implementation
├── compile.sh         # Compilation script
└── clocksweep         # Compiled binary
```

---

## Algorithm Overview

### ClockSweep Mechanism

```
Buffer Pool (4 frames):
┌────┬────┬────┬────┐
│ F0 │ F1 │ F2 │ F3 │  ← Frames
└────┴────┴────┴────┘
  ↑              ↓
  └──clock hand──┘  (circular)

Each frame has:
- page_id: Which page is stored
- usage_count: 0 to 5 (reference count)
- pinned: true/false (can it be evicted?)
```

### Eviction Process

When a new page needs to be loaded:

1. **Check if page exists:** If yes → HIT (increment usage_count, cap at 5)
2. **If not, find victim:**
   - Start at current clock hand position
   - If frame is pinned → skip
   - If usage_count == 0 → EVICT this frame
   - If usage_count > 0 → decrement and move on (second chance)
   - Move hand forward (circular)
3. **Load new page** into victim frame with usage_count = 1


---

## Implementation

### Frame Structure

```cpp
struct Frame {
    int  page_id = -1;         // -1 = empty
    int  usage_count = 0;      // 0-5 (capped at 5)
    bool pinned = false;       // Cannot be evicted if true
};
```

### BufferPool Class

```cpp
class BufferPool {
    std::vector<Frame>              frames;
    std::unordered_map<int, int>    page_to_frame;
    int                             hand;
    int                             capacity;
    
public:
    int  fetch(int page_id);    // Load page (HIT or MISS)
    void pin(int page_id);      // Mark as pinned
    void unpin(int page_id);    // Remove pin
    void print_state();         // Display buffer state
};
```

### Key Methods

**1. fetch(page_id)**
- If page in buffer → HIT (increment usage_count)
- If not → MISS (find victim via clocksweep(), load page)

**2. clocksweep()**
- Circular scan starting at hand position
- Skip pinned frames
- Decrement usage_count > 0 (second chance)
- Evict when usage_count == 0
- Return frame index or -1 if all pinned

**3. pin/unpin**
- Control whether a frame can be evicted
- Used for pages being actively modified

---

## Building and Running

### Compile

```bash
chmod +x compile.sh
./compile.sh
```

Or manually:
```bash
g++ -std=c++17 -O2 -Wall -Wextra -o clocksweep clocksweep.cpp
```

### Run

```bash
./clocksweep
```

---

## Testing Results

### Test 1: Basic Access Pattern (from lab spec)

```
Pool Size: 4 frames
Access Sequence: 1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5
```

**Results:**
```
Hits: 5
Misses: 7
Evictions: 3
Hit Rate: 41.67%
```

**Key observation:** Pages 1 and 2 are accessed multiple times, increasing their usage_count, which protects them from immediate eviction during the sweep.


### Test 2: Sequential Scan

```
Pool Size: 3 frames
Access Sequence: 10, 11, 12, 13, 14, 15, 16, 17, 18, 19
```

**Results:**
```
Hits: 0
Misses: 10
Evictions: 7
Hit Rate: 0.00%
```

**Analysis:** Sequential scans have poor hit rates because pages are accessed once and never reused. This is a known limitation of buffer pool algorithms for sequential workloads.

### Test 3: Hot Page Protection

```
Pool Size: 3 frames
Page 100 accessed 4 times (usage_count → 4)
Then pages 101, 102, 103, 104 loaded
```

**Results:**
```
Final State:
Frame 0: Page 100 (usage=2) ← Hot page survived!
Frame 1: Page 103 (usage=1)
Frame 2: Page 104 (usage=1)
```

**Key insight:** Page 100 with usage_count=4 survives multiple sweeps while pages 101 and 102 (usage_count=1) are evicted. This demonstrates how ClockSweep protects frequently-accessed pages.

### Test 4: Pinned Page Behavior

```
Pool Size: 3 frames
Page 200 is pinned (cannot be evicted)
```

**Results:**
```
Before unpin:
Frame 0: Page 200 (pinned=YES) ← Never evicted
Frame 1: Page 203
Frame 2: Page 204

After unpin:
Frame 0: Page 205 ← Now 200 can be evicted
Frame 1: Page 203
Frame 2: Page 204
```

**Use case:** Pinning is used when a transaction is actively modifying a page and it must remain in memory until the operation completes.

### Test 5: Usage Count Cap

```
Page 300 accessed 11 times
```

**Results:**
```
usage_count progression: 1 → 2 → 3 → 4 → 5 → 5 → 5 → 5...
(capped at 5)
```

**Why cap at 5?**
- Prevents integer overflow
- Limits the number of sweeps needed to evict a hot page
- Balances protection vs responsiveness
- PostgreSQL's empirical choice

---


## ClockSweep vs LRU Comparison

| Aspect | LRU | ClockSweep |
|--------|-----|------------|
| **Data Structure** | Doubly-linked list + hashmap | Circular array + hashmap |
| **Eviction Quality** | Optimal (exact recency) | Near-optimal (approximate) |
| **Time per access** | O(1) with lock contention | O(1) lock-free on usage_count |
| **Memory overhead** | 2 pointers per frame | 1 int per frame |
| **Implementation** | Complex (list maintenance) | Simple (circular scan) |
| **Sequential scan** | Wrecks LRU (evicts all) | usage_count cap limits damage |
| **Concurrency** | High lock contention | Low contention (atomic increment) |

### Why PostgreSQL Chose ClockSweep

1. **Lower Lock Contention:**
   - LRU: Every access requires moving node to front of list (mutex lock)
   - ClockSweep: Increment usage_count (can be atomic)

2. **Sequential Scan Protection:**
   - LRU: Full table scan evicts all hot pages
   - ClockSweep: Hot pages with usage=5 survive scans

3. **Simpler Implementation:**
   - No pointer manipulation
   - Cache-friendly (linear array)

4. **Predictable Performance:**
   - Maximum 2 * capacity sweeps to find victim
   - No worst-case list traversal

---

## Key Concepts Demonstrated

### 1. Second Chance Algorithm

```
Sweep example with 4 frames:
[page=1, usage=3] → decrement to 2, skip
[page=2, usage=1] → decrement to 0, skip
[page=3, usage=0] → EVICT! ✓
```

Pages get multiple chances based on usage_count.

### 2. Clock Hand Circular Movement

```
┌─────┬─────┬─────┬─────┐
│  0  │  1  │  2  │  3  │
└─────┴─────┴─────┴─────┘
       ↑
     hand=1

After increment:
hand = (hand + 1) % capacity = (1 + 1) % 4 = 2
```

### 3. Pin/Unpin Mechanism

```
Transaction lifecycle:
1. BEGIN
2. fetch(page_100) → loads into buffer
3. pin(page_100)   → mark as non-evictable
4. UPDATE ...      → modify page in memory
5. unpin(page_100) → allow eviction now
6. COMMIT          → flush dirty page to disk
```

### 4. Usage Count Semantics

```
Access 1: usage = 1 (loaded)
Access 2: usage = 2
Access 3: usage = 3
...
Access 5: usage = 5
Access 6: usage = 5 (capped!)
```

More accesses = more protection from eviction.

---


## Connection to PostgreSQL

### PostgreSQL's Buffer Manager

Located at: `src/backend/storage/buffer/freelist.c`

```c
// Simplified version of PostgreSQL's algorithm
Buffer StrategyGetBuffer(BufferAccessStrategy strategy) {
    while (true) {
        BufferDesc *buf = &BufferDescriptors[nextVictimBuffer];
        
        if (buf->refcount == 0 && buf->usage_count == 0) {
            return buf;  // Found victim
        }
        
        if (buf->usage_count > 0) {
            buf->usage_count--;  // Second chance
        }
        
        nextVictimBuffer = (nextVictimBuffer + 1) % NBuffers;
    }
}
```

### Real-World Configuration

```sql
-- PostgreSQL configuration
SHOW shared_buffers;     -- e.g., 128MB (default)
                         -- Each frame = 8KB page

-- Buffer statistics
SELECT * FROM pg_stat_database;
-- blks_hit: pages served from buffer
-- blks_read: pages read from disk

-- Hit ratio (should be > 90%)
SELECT 
    sum(blks_hit) / (sum(blks_hit) + sum(blks_read)) AS hit_ratio
FROM pg_stat_database;
```

### When ClockSweep Matters

**Scenario:** 1000 concurrent connections, each running queries

- Without good eviction: Frequent disk I/O (10ms HDD, 0.1ms SSD)
- With ClockSweep: Hot pages stay in buffer (50ns RAM access)

**Performance impact:** 100,000x to 200,000x speedup for cache hits!

---

## Performance Analysis

### Time Complexity

| Operation | Complexity | Notes |
|-----------|------------|-------|
| fetch() hit | O(1) | Hash lookup + increment |
| fetch() miss | O(n) worst | Max 2 * capacity sweeps |
| pin/unpin | O(1) | Hash lookup + flag set |
| print_state | O(n) | Iterate all frames |

### Space Complexity

```
Total space = O(capacity) + O(pages_in_buffer)

Per frame: sizeof(Frame) = 12 bytes (int + int + bool)
Hash map: O(pages) entries

Example: 100 frames
- Frame array: 100 * 12 = 1,200 bytes
- Hash map: ~100 * (8 + 8) = 1,600 bytes
- Total: ~3KB (negligible)
```

---

## Testing Checklist

✅ **Basic functionality**
- [x] Load pages into empty pool
- [x] Handle cache hits (increment usage_count)
- [x] Evict pages when pool is full
- [x] Clock hand moves circularly

✅ **Usage count behavior**
- [x] Increments on each access
- [x] Capped at 5
- [x] Decrements during sweep
- [x] Pages with higher counts survive longer

✅ **Pin/unpin**
- [x] Pinned pages never evicted
- [x] Unpinned pages can be evicted
- [x] Clock hand skips pinned frames

✅ **Edge cases**
- [x] All frames pinned (returns -1)
- [x] Sequential scan (0% hit rate)
- [x] Hot page protection (survives sweeps)

---


## Key Takeaways

1. **ClockSweep approximates LRU** without expensive list maintenance
2. **usage_count provides fine-grained "hotness" tracking** (not just a single reference bit)
3. **Circular hand means no preference** - all frames treated equally over time
4. **Pinning enables safe in-place updates** during transactions
5. **Cap at 5 balances protection vs responsiveness** - prevents hot pages from becoming "too hot"
6. **Low lock contention** makes it ideal for high-concurrency databases

### Real-World Impact

```
Database workload: 1M queries/sec
Buffer pool size: 1GB (131,072 pages of 8KB)
Hit rate: 95%

Disk I/O saved per second:
950,000 queries * 0.1ms (SSD) = 95 seconds of I/O avoided!

This is why PostgreSQL uses ClockSweep instead of simpler FIFO or random eviction.
```

---

## References

- PostgreSQL Buffer Manager: `src/backend/storage/buffer/freelist.c`
- PostgreSQL Documentation: https://www.postgresql.org/docs/current/runtime-config-resource.html
- Clock Algorithm: Corbató, F. J. (1968). "A Paging Experiment with the Multics System"
- Lab Session Requirements: `../lab_sessions/lab_3.txt`

---

## Author

**Pulasari Jai** (Roll No: 24BCS10656)  
Advanced Database Management Systems  
Scaler Academy
