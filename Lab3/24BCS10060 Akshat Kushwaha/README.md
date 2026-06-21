# Lab 3 — Clock Sweep Page Replacement

**Name:** Akshat Kushwaha
**Roll No:** 24BCS10060

## What this lab is about

A database keeps a **buffer pool** in memory: a fixed number of slots ("frames")
that hold copies of disk pages so it doesn't have to read the disk every time.
When the pool is full and a new page is needed, one page has to be kicked out.
*Which* one to kick out is the page-replacement problem. PostgreSQL uses an
algorithm called **clock sweep**, and that's what I implemented here.

## Files

| File | What it does |
|---|---|
| `clock_sweep.cpp` | A `BufferPool` class with clock-sweep eviction + a demo trace |

## Build & run

```bash
g++ -std=c++17 -Wall -Wextra clock_sweep.cpp -o clock_sweep
./clock_sweep
```

## The idea

The perfect policy would be **LRU** (throw out the page used least recently), but
true LRU needs a list that you reorder on *every single access*, which is extra
work and causes lock contention in a real DB. Clock sweep is a cheaper
approximation of LRU.

Each frame keeps a small **usage count** (I cap mine at 3; PostgreSQL caps at 5).
There is a single **hand** that points at one frame, like the hand of a clock.

- On a **hit** (page already in the pool): bump that frame's usage count up
  (capped).
- On a **miss** with the pool full: starting at the hand, find a victim —
  - if the frame's usage count is `0`, evict it;
  - if it's `> 0`, subtract one (a "second chance") and move the hand forward.
  - keep going until a `0` is found.

So pages that get touched a lot keep a high usage count and survive several
sweeps, while pages touched once drop to 0 quickly and get evicted. That's how it
*approximates* "least recently used" without keeping an ordered list.

```
hand
 |
 v
[p7:u2] [p8:u2] [p9:u1] [p10:u1]
   |       |       |        |
need a victim, walk from hand:
 p7 usage 2 -> 1, skip
 p8 usage 2 -> 1, skip
 p9 usage 1 -> 0, skip
 p10 usage 1 -> 0, skip
 p7 usage 1 -> 0, skip
 ... first frame that is already 0 gets evicted
```

## What the demo shows

I feed the pool the access pattern `7 8 9 10 7 8 11 7 8 12 9 7`. Pages 7 and 8
are requested again and again, so their usage counts keep getting refreshed and
they stay resident. Pages like 9, 10, 11, 12 are touched rarely, drop to 0, and
get evicted first. The printed `state:` line after each request shows the frames
and where the hand is.

## Why PostgreSQL prefers clock sweep over LRU

| | LRU | Clock sweep |
|---|---|---|
| Quality | exact recency | approximate (good enough) |
| Data structure | linked list + map, reordered every access | just a circular array + a counter |
| Cost per access | bookkeeping + locking on every touch | only bump a counter |
| Scan flooding | a big scan can evict all hot pages | the usage cap protects hot pages |

The "scan flooding" row is a nice bonus: if a query scans a whole big table once,
each of those pages only gets usage count 1, so the genuinely hot pages (high
count) still survive the sweep.

## Key takeaways

- The buffer pool caches disk pages; eviction policy decides what to drop.
- Clock sweep approximates LRU with just a counter per frame and a moving hand —
  much cheaper than maintaining a true LRU list.
- A `usage_count` (instead of a single bit) tracks how "hot" a page is and gives
  frequently used pages several second chances.
- A brand-new page starts with usage 1 so it isn't evicted the instant it loads.
