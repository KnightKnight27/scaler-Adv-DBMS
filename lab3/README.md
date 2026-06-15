# Lab 3: Clock Sweep Cache Replacement Algorithm

**Student:** Talin Daga (24bcs10321)

## Objective
Study the Clock Sweep page replacement policy — a practical approximation of LRU used in database buffer managers (e.g., PostgreSQL's `shared_buffers`).

## Files

| File | Description |
|------|-------------|
| `clock_sweep.c` | Full demonstration of the Clock Sweep algorithm in C |

## Build

```bash
gcc -Wall -Wextra -o clock_sweep clock_sweep.c
```

## Run

```bash
./clock_sweep
```

---

## How the Algorithm Works

```
Cache frames:  [ Frame 0 | Frame 1 | Frame 2 | Frame 3 | Frame 4 ]
                                                           ^
                                                      clock hand

Each frame stores:  page_id | ref_bit (0 or 1) | data

On ACCESS (hit):   ref_bit = 1  (page marked as recently used)
On LOAD   (miss):  ref_bit = 0  (page starts cold; must be accessed to become warm)

Clock Sweep (when cache is full):
  Advance hand →
    if ref_bit == 1:  clear to 0, continue  (second chance given)
    if ref_bit == 0:  EVICT this page        (cold page, no second chance)
```

## Tasks Demonstrated

| Task | Description |
|------|-------------|
| 1 | Cache initialization — all frames empty, hand at 0 |
| 2 | Load pages 1–5 (fills cache), all start with ref_bit=0 |
| 3 | Access pages 1, 3, 5 repeatedly — they earn ref_bit=1; pages 2, 4 stay cold |
| 4 | Request page 6 — sweep starts, page 1 gets second chance, page 2 evicted |
| 5 | Request pages 7, 8 — further evictions with hand continuing from last position |
| 6 | Summary: hit rate, miss rate, eviction count, comparison with FIFO/LRU |

## Observations (fill in after running)

| Metric | Observed Value |
|--------|---------------|
| Cache capacity (frames) | 5 |
| Pages loaded in Task 2 | 1 – 5 |
| ref_bit of pages 2, 4 after Task 3 | |
| ref_bit of pages 1, 3, 5 after Task 3 | |
| Frame where page 2 was evicted | |
| Page evicted when page 6 inserted | |
| Page evicted when page 7 inserted | |
| Page evicted when page 8 inserted | |
| Total cache hits | |
| Total cache misses | |
| Total evictions | |
| Hit rate (%) | |

## Analysis Questions

1. **What is the Clock Sweep algorithm and how does it differ from pure LRU?**

2. **What does the reference bit (ref_bit) represent?**

3. **Why do newly loaded pages start with ref_bit = 0 in this implementation?**

4. **What is the "second chance" mechanism?**

5. **Which pages were evicted and why? Does the result match your prediction?**

6. **What would FIFO have evicted instead? Is that better or worse?**

7. **Why does PostgreSQL use Clock Sweep instead of LRU for its buffer manager?**

8. **How does access frequency affect a page's survival in the cache?**

## Comparison with Other Policies

| Aspect | Clock Sweep | FIFO | LRU |
|--------|-------------|------|-----|
| Data structure | Circular array + hand | Queue | Timestamp / stack |
| Second chance | Yes | No | N/A |
| Recency awareness | Approximate | None | Exact |
| Hit overhead | O(1) — just set ref_bit | O(1) | O(1) to O(n) |
| Eviction overhead | O(n) worst, O(1) amortised | O(1) | O(1) |
| Used in | PostgreSQL, OS page cache | Simple embedded systems | Some DBMS, OS variants |
