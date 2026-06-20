# Lab 3: Clock Sweep Page Replacement

## 1. Aim

The aim of this lab is to implement a simplified version of the Clock Sweep page replacement algorithm and explain why it can behave better than a pure LRU policy in the sequential flooding scenario.

In a database system, the buffer pool is limited. When a new page must be loaded and all frames are full, the system needs a replacement policy to decide which existing page should be removed. This lab focuses on that decision.

## 2. Background

A database does not read every page directly from disk every time. It keeps recently or frequently used pages in a memory area called the buffer pool. Since memory is smaller than disk, the buffer manager must evict some pages when the buffer is full.

Two common replacement ideas are:

- **LRU, Least Recently Used:** evict the page that has not been used for the longest time.
- **Clock Sweep:** arrange frames in a circular order and use a moving hand to search for a page whose usage information shows it is safe to evict.

LRU is simple to understand, but it can perform badly when a workload scans a large table sequentially. Clock Sweep is commonly used in database systems because it is cheaper to maintain and can avoid giving too much importance to pages that were touched only once during a scan.

## 3. Overview of My Implementation

My implementation is written in C++ using a template class:

```cpp
template<typename T>
class ClockSweep
```

The cache stores generic keys, so the same structure could represent page numbers, block ids, or any other page identifier.

The important data members are:

| Member | Purpose |
|---|---|
| `maxCacheSize` | Maximum number of pages that can be kept in memory |
| `pages` | Vector of frames currently present in the cache |
| `cacheMap` | Maps a key to its frame index for fast lookup |
| `clockHand` | Current position of the clock sweep pointer |
| `usageCount` | Approximate measure of recent/frequent use |
| `pinned` | Marks a page as temporarily protected from eviction |
| `bgClockThread` | Background thread that periodically performs aging |
| `mtx` | Mutex used to protect shared cache state |

Each page frame stores:

```cpp
struct Page {
    T key;
    int usageCount{0};
    bool pinned{false};
};
```

This is a simplified model of what a real DBMS buffer frame stores. A real system would also store dirty bits, page data, disk block id, latch state, and pin count. For this lab, the key, usage counter, and pin status are enough to demonstrate the replacement behavior.

## 4. How Page Access Works

When `getKey(key)` is called:

1. The cache is locked using a mutex.
2. The hash map checks whether the key is already present.
3. If the key exists, its `usageCount` is increased.
4. The page is marked as `pinned = true`.
5. If the key is missing, the function reports a miss.

This means a page that is actually used gets a stronger chance to survive the next sweep.

When `putKey(key)` is called:

1. If the key is already present, it is treated as another use of the same page.
2. If there is free space, the key is inserted into a new frame.
3. If the cache is full, `evictAndInsert(key)` is called.

So insertion and lookup both update the page's replacement information.

## 5. Clock Sweep Eviction Logic

The main replacement decision happens in `evictAndInsert`.

The clock hand moves circularly over the frames:

```text
frame 0 -> frame 1 -> frame 2 -> frame 0 -> ...
```

For each frame:

- If `usageCount > 0`, the algorithm decreases it and gives the page another chance.
- If `usageCount == 0` and the page is not pinned, the page can be evicted.
- After eviction, the new key is inserted into that frame and the hand moves forward.

This behavior is called "second chance" style replacement. A page is not removed immediately just because the clock hand reaches it. It first loses usage credit. Only after the page has no usage credit and is unpinned can it be chosen as a victim.

## 6. Background Sweep Thread

My implementation also has a background clock thread:

```cpp
void runClock()
```

Every two seconds, it:

1. Unpins pages that were previously marked as pinned.
2. Gradually reduces their `usageCount`.

This simulates the aging behavior of a buffer manager. A page that was used recently is protected for some time, but that protection is not permanent. If the page is not used again, its counter eventually becomes zero and it becomes a valid eviction candidate.

This is important because a database workload changes over time. A page that was useful earlier should not stay in memory forever if the workload has moved on.

## 7. Example From the Program

The program creates a cache with only three frames:

```cpp
ClockSweep<int> clockSweep(3);
```

Then it inserts:

```text
10, 20, 30
```

At this point the cache is full:

```text
[10, 20, 30]
```

Then page `10` is accessed:

```cpp
clockSweep.getKey(10);
```

This increases the usage information for page `10`. After waiting for the background sweep, the program inserts page `40`. Since the cache has only three frames, one page must be evicted.

The Clock Sweep algorithm does not blindly remove the oldest page. It checks usage count and pin status while moving the clock hand. Pages that were recently used get a second chance, while pages that have aged out become candidates for replacement.

## 8. What Is Sequential Flooding?

Sequential flooding happens when the workload scans a large sequence of pages that is bigger than the buffer pool.

Example with buffer size 3:

```text
Scan pages: 1, 2, 3, 4, 5, 6, 7, 8, ...
```

If a database is also repeatedly using some important pages, for example an index root or a hot metadata page, the scan can push those useful pages out of the buffer.

A simple LRU policy often performs poorly here because it treats the pages from the sequential scan as "recently used", even though each scan page may never be used again. As the scan continues, LRU keeps replacing older pages with newer scan pages. The buffer becomes filled with pages that are recent but not actually valuable.

This is called flooding because the one-time sequential pages flood the cache and remove pages that may be more useful.

## 9. Why Pure LRU Can Behave Badly

Consider a buffer pool of size 3.

Suppose page `A` is an important page used repeatedly, and then a sequential scan reads pages `1, 2, 3, 4, 5`.

Initial useful state:

```text
[A, B, C]
```

Sequential scan under LRU:

```text
Read 1 -> evict least recently used page
Read 2 -> evict another older page
Read 3 -> evict another older page
Read 4 -> evict page 1
Read 5 -> evict page 2
```

After the scan, the cache may contain only scan pages:

```text
[3, 4, 5]
```

The problem is not that LRU is always bad. The problem is that LRU assumes recency means usefulness. In a sequential scan, recency is misleading. The most recent pages are often the pages we are least likely to use again.

If page `A` is needed again after the scan, LRU may have already evicted it. That creates an extra miss and disk read for an important page.

## 10. Why Clock Sweep Is Better in This Scenario

Clock Sweep behaves better because it does not rely only on exact recency order. It uses a small amount of usage history.

In my implementation:

- A page that is hit gets `usageCount++`.
- A newly inserted page starts with `usageCount = 1`.
- The clock hand reduces usage counts before eviction.
- A page must reach `usageCount == 0` and also be unpinned before it is removed.

This means a repeatedly accessed page gets more protection than a page that was only touched once.

For a sequential scan:

```text
1, 2, 3, 4, 5, 6, ...
```

Each scanned page is usually inserted and then not used again. It receives only a small amount of usage credit. When the clock hand comes around later, its counter can be reduced to zero and it becomes easy to evict.

For a frequently used page:

```text
A, A, A, A
```

Each hit increases the usage count. So when the clock hand reaches `A`, it is less likely to be evicted immediately.

That is the key difference:

```text
LRU protects pages because they were recent.
Clock Sweep protects pages because they still have usage credit.
```

In sequential flooding, "recent" is not always meaningful. Usage credit is a better signal because scan pages are usually touched once, while useful pages are touched repeatedly.

## 11. Proof by Comparison

Assume:

```text
Buffer size = 3
Hot page = H
Sequential scan = 1, 2, 3, 4, 5
```

The hot page `H` is accessed multiple times before and during normal work.

### LRU Behavior

LRU keeps a strict order based on most recent access.

```text
Start:      [H, X, Y]
Read 1:    [X, Y, 1]    H may move toward eviction if not touched during scan
Read 2:    [Y, 1, 2]
Read 3:    [1, 2, 3]
Read 4:    [2, 3, 4]
Read 5:    [3, 4, 5]
```

After a long enough scan, the buffer contains scan pages. The hot page can be lost because LRU only remembers that the scan pages were more recent.

### Clock Sweep Behavior

Clock Sweep gives pages a second chance through `usageCount`.

```text
Start:      H has higher usageCount because it was used repeatedly
Read 1:     page 1 gets small usageCount
Read 2:     page 2 gets small usageCount
Read 3:     page 3 gets small usageCount
Sweep:      one-time pages lose credit quickly
Read 4:     a low-credit scan page is a better victim than H
Read 5:     another low-credit scan page can be replaced
```

The exact frame contents depend on where the clock hand is positioned, but the important behavior is clear: a page with repeated use has a better chance of surviving than one-time scan pages.

This is why Clock Sweep is often more suitable for DBMS buffer replacement. It is not a perfect algorithm, but it is more resistant to cache pollution caused by sequential scans.

## 12. Cost Comparison

LRU usually needs extra work to maintain an exact recency order. For every page hit, the page must be moved to the most recent position in a list or similar structure.

Clock Sweep is cheaper:

- It uses a circular pointer.
- It only updates small metadata like `usageCount` and `pinned`.
- It does not need to reorder the whole cache on every hit.
- Victim selection is done by advancing the hand until a suitable page is found.

This makes Clock Sweep practical for database systems where page accesses happen very frequently.

## 13. Limitations of This Simplified Implementation

This implementation is meant for learning, so it does not include every feature of a production DBMS buffer manager.

Some simplifications are:

- `pinned` is a boolean, while real systems normally use a pin count.
- There is no dirty page handling or write-back to disk.
- The background thread ages all pages at fixed time intervals.
- The program demonstrates the policy with integer keys instead of actual disk pages.
- It does not separate sequential scan pages from normal random-access pages using advanced DBMS techniques.

Even with these simplifications, the main Clock Sweep idea is visible: pages are aged gradually, recently or frequently used pages get a second chance, and eviction is decided using a moving clock hand.

## 14. Conclusion

This lab helped me understand that page replacement in a DBMS is not only about removing the oldest page. The replacement policy must match database workloads.

LRU works well when recent use predicts future use. However, during sequential flooding, that assumption breaks because a scan produces many recent pages that may not be useful again. Clock Sweep handles this situation better because it uses usage counters and a sweep pointer instead of only exact recency.

In my implementation, frequently accessed pages gain usage count, pinned pages are temporarily protected, and the clock hand evicts pages only after their usage count has aged down. This gives the algorithm better behavior for sequential flooding and also keeps the replacement logic simple enough for a database buffer manager.
