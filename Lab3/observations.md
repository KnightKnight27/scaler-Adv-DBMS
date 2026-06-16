# Lab Session 3: Clock Sweep Page Replacement Algorithm in C++

## Objective

To implement and understand the ClockSweep (Clock) page replacement algorithm used in PostgreSQL's buffer manager and observe how it approximates LRU while maintaining low overhead.

---

# Introduction

PostgreSQL uses the ClockSweep algorithm instead of a traditional Least Recently Used (LRU) policy for managing its shared buffer pool.

The algorithm maintains:

* A circular list of buffer frames
* A clock hand that moves through frames
* A usage count for each frame
* An optional pinned status

When a page is accessed, its usage count is increased. During eviction, the clock hand scans frames and gives pages with non-zero usage counts a "second chance" by decrementing the count instead of immediately evicting them.

This approach avoids the overhead of maintaining an ordered LRU list while still approximating recency of access.

---

# Data Structures Used

## Frame Structure

Each frame stores:

```text
page_id
usage_count
pinned
```

### page_id

Identifies the page currently stored in the frame.

### usage_count

Tracks how frequently or recently the page has been accessed.

### pinned

Indicates whether the page can be evicted.

---

# Algorithm Logic

## Page Hit

When the requested page already exists in the buffer:

1. Locate the frame.
2. Increase its usage count.
3. Return the frame index.

Result:

```text
[HIT]
```

---

## Page Miss

When the requested page is not present:

1. Invoke ClockSweep.
2. Search for a victim frame.
3. Evict the victim page.
4. Load the new page.

Result:

```text
[MISS]
```

---

## ClockSweep Victim Selection

The clock hand moves circularly across frames.

For each frame:

### Case 1: Pinned

```text
Skip frame
```

The frame cannot be evicted.

### Case 2: usage_count > 0

```text
Decrement usage_count
Move to next frame
```

The page receives a second chance.

### Case 3: usage_count == 0

```text
Select frame as victim
```

The page is evicted.

---

# Test Configuration

Buffer Pool Size:

```text
4 Frames
```

Page Access Sequence:

```text
1 2 3 4 1 2 5 1 2 3 4 5
```

---

# Program Output

```text
[MISS] page 1 loaded into frame 0
[MISS] page 2 loaded into frame 1
[MISS] page 3 loaded into frame 2
[MISS] page 4 loaded into frame 3

[HIT] page 1 in frame 0 usage=2
[HIT] page 2 in frame 1 usage=2

[EVICT] page 3 from frame 2
[MISS] page 5 loaded into frame 2

[HIT] page 1 in frame 0 usage=1
[HIT] page 2 in frame 1 usage=1

[EVICT] page 4 from frame 3
[MISS] page 3 loaded into frame 3

[EVICT] page 1 from frame 0
[MISS] page 4 loaded into frame 0

[HIT] page 5 in frame 2 usage=1

--- Buffer Pool State (hand=1) ---
Frame[0] page=4 usage=1
Frame[1] page=2 usage=0 <-- hand
Frame[2] page=5 usage=1
Frame[3] page=3 usage=0
-------------------------------
```

---

# Observations

## Initial Loading Phase

The first four accesses:

```text
1 2 3 4
```

produced misses because the buffer pool was empty.

Pages were loaded into frames:

| Frame | Page |
| ----- | ---- |
| 0     | 1    |
| 1     | 2    |
| 2     | 3    |
| 3     | 4    |

---

## Repeated Accesses

Pages:

```text
1
2
```

were accessed again.

Result:

```text
HIT
```

Their usage counts increased to 2.

This made them less likely to be evicted.

---

## First Eviction

When page:

```text
5
```

was requested:

* Buffer pool was already full.
* Clock hand started sweeping.
* Pages with non-zero usage counts received second chances.
* Usage counts were decremented.

Eventually:

```text
Page 3
```

was selected as victim.

Result:

```text
[EVICT] page 3
```

Page 5 replaced Page 3.

---

## Second Eviction

Later, page:

```text
3
```

was requested again.

The algorithm performed another sweep.

Page:

```text
4
```

became the victim.

Result:

```text
[EVICT] page 4
```

Page 3 was loaded into that frame.

---

## Third Eviction

When page:

```text
4
```

was requested again:

Page:

```text
1
```

was selected as victim.

Result:

```text
[EVICT] page 1
```

This demonstrates that pages gradually lose protection as their usage counts decrease.

---

# Final Buffer State

| Frame | Page | Usage Count |
| ----- | ---- | ----------- |
| 0     | 4    | 1           |
| 1     | 2    | 0           |
| 2     | 5    | 1           |
| 3     | 3    | 0           |

Clock Hand Position:

```text
1
```

---

# Comparison with LRU

| Property         | LRU         | ClockSweep     |
| ---------------- | ----------- | -------------- |
| Recency Tracking | Exact       | Approximate    |
| Data Structure   | Linked List | Circular Array |
| Memory Overhead  | Higher      | Lower          |
| Lock Contention  | Higher      | Lower          |
| Scalability      | Moderate    | High           |
| PostgreSQL Usage | No          | Yes            |

---

# Advantages of ClockSweep

1. O(1) average replacement cost.
2. No need for maintaining an ordered LRU list.
3. Lower synchronization overhead.
4. Better scalability for large buffer pools.
5. Frequently accessed pages survive longer.
6. Protects hot pages from immediate eviction.

---

# Why PostgreSQL Uses ClockSweep

PostgreSQL prioritizes scalability and concurrency.

Maintaining an exact LRU list would require:

* Frequent updates on every page access.
* Additional locking.
* Higher contention among concurrent processes.

ClockSweep avoids these costs while providing eviction decisions that are very close to LRU in practice.

---

# Conclusion

The ClockSweep page replacement algorithm was successfully implemented in C++ and tested on a buffer pool of four frames.

The experiment demonstrated:

* Page hits and misses
* Usage count updates
* Victim selection
* Clock hand movement
* Page replacement behavior

The results confirm that ClockSweep efficiently approximates LRU while maintaining low overhead, which is why PostgreSQL uses it in its buffer manager instead of a traditional LRU implementation.
