# Clock Sweep Buffer Pool Replacement

**Course:** Advanced DBMS (Scaler)
**Author:** Praveen Kumar 24bcs10048
**Date:** 2026-05-17

---

## 1. Objective

Implement the **clock sweep algorithm** (also called the second-chance algorithm) in C++. This is the page replacement strategy PostgreSQL uses in its shared buffer pool to decide which cached page to evict when space is needed.

The goal is to understand why databases don't just use the OS page cache blindly, and how a DBMS manages its own buffer pool with a cheap approximation of LRU.

---

## 2. Build and Run

```bash
g++ -std=c++17 -O2 -o clock_sweep clock_sweep.cpp
./clock_sweep
```

The program creates a 4-slot buffer pool, runs a hardcoded workload of page requests, and prints a trace of every clock sweep decision.

---

## 3. Background: Why Does a DBMS Need Its Own Buffer Pool?

The OS already has a page cache. So why do databases like PostgreSQL maintain a separate buffer pool in user space?

1. **Eviction control.** The OS uses its own replacement policy (usually some variant of LRU). The DBMS knows its access patterns better -- for example, a sequential scan should not blow out the entire cache.

2. **Pin counting.** The DBMS needs to guarantee that a page stays in memory while a query is using it. The OS page cache does not provide this.

3. **Write ordering.** For crash recovery (WAL), the DBMS must control when dirty pages are flushed to disk. The OS can flush pages at any time, which would break recovery guarantees.

4. **Portability.** Relying on OS-specific caching behavior makes the DBMS non-portable.

---

## 4. The Clock Sweep Algorithm

### 4.1 Data Structures

Each slot in the buffer pool has a descriptor:

```
struct BufferDescriptor {
    page_id       : int     // which disk page is in this slot
    usage_count   : int     // how "hot" this page is
    dirty         : bool    // has the page been modified?
    valid         : bool    // is there a page loaded?
}
```

There is also a single **clock hand** -- an integer index that advances circularly through the pool.

### 4.2 On a Page Request

```
request_page(page_id):
    1. Scan pool for page_id
       -> Found?  Increment usage_count.  Return (HIT).

    2. Not found.  Need to evict.
       -> Call find_victim() to get a free slot.

    3. Load page_id into the victim slot.
       -> Set usage_count = 1, valid = true.
       -> Return (MISS).
```

### 4.3 Finding a Victim (the sweep)

```
find_victim():
    loop:
        slot = pool[clock_hand]

        if slot is empty:
            return clock_hand        // fast path

        if slot.usage_count == 0:
            return clock_hand        // evict this one

        slot.usage_count--           // second chance: decrement
        clock_hand = (clock_hand + 1) % pool_size
```

The key insight: a page with usage_count = 3 gets three chances to survive before it can be evicted. A page touched only once (usage_count = 1) gets only one chance. This approximates LRU without maintaining a linked list.

### 4.4 Diagram

```
                       clock hand
                          |
                          v
            +-----+-----+-----+-----+
  Slots:    |  0  |  1  |  2  |  3  |
            +-----+-----+-----+-----+
  Pages:    |  10 |  20 |  30 |  40 |
  Usage:    |  2  |  1  |  1  |  1  |
            +-----+-----+-----+-----+

  New page 50 arrives.  No empty slots.  Sweep begins:

  Step 1: slot 0, page 10, usage 2 -> 1  (skip)
  Step 2: slot 1, page 20, usage 1 -> 0  (skip)
  Step 3: slot 2, page 30, usage 1 -> 0  (skip)
  Step 4: slot 3, page 40, usage 1 -> 0  (skip)
  Step 5: slot 0, page 10, usage 1 -> 0  (skip)
  Step 6: slot 1, page 20, usage 0       (EVICT)

  Page 20 is evicted.  Page 50 loaded into slot 1.
```

Notice that page 10 survived because it had a higher usage count (it was accessed twice). This is the "second chance" in action.

---

## 5. How PostgreSQL Uses This

In PostgreSQL's source (`src/backend/storage/buffer/freelist.c`), the clock sweep is implemented in `StrategyGetBuffer()`. Some details:

- The buffer pool size is configured via `shared_buffers` (default 128 MB, typically set to 25% of RAM).
- Each buffer has a `usage_count` field capped at 5 (`BM_MAX_USAGE_COUNT`).
- The clock hand is a global variable (`nextVictimBuffer`) protected by a spinlock.
- When a backend needs a buffer, it calls `StrategyGetBuffer()` which sweeps the clock just like our implementation.
- If a page is pinned (in use by another query), the sweep skips it entirely.

The cap at 5 prevents a single hot page from accumulating an arbitrarily high usage count, which would make it nearly impossible to evict even after it stops being accessed.

---

## 6. Clock Sweep vs LRU

| Aspect | Clock Sweep | LRU |
|--------|-------------|-----|
| Data structure | Circular array + single pointer | Doubly-linked list + hash map |
| Cost per access | O(1) to increment usage count | O(1) but requires list move |
| Cost per eviction | O(n) worst case (one full sweep) | O(1) -- evict from tail |
| Concurrency | Only the sweep needs synchronization | Every access requires list lock |
| Approximation | Approximate LRU | Exact LRU |
| Scan resistance | Moderate (usage count helps) | Poor (scan floods the list) |

PostgreSQL chose clock sweep because the concurrency cost of true LRU is too high. In a busy database server, hundreds of backends are accessing the buffer pool concurrently. Moving a node in a linked list on every buffer access would be a major contention point.

---

## 7. Sample Output

```
============================================================
  Clock Sweep Buffer Pool Replacement
============================================================

  Pool size: 4 slots

------------------------------------------------------------
  Request #1: page 10
  >> Page 10 : MISS -> loaded into slot 0

  Buffer Pool State  (clock hand -> slot 1)
  +------+--------+-------+-------+
  | Slot |  Page  | Usage | Dirty |
  +------+--------+-------+-------+
  |   0  |    10  |   1   |  no   |
  |   1  |   --   |  --   |  --   |
  |   2  |   --   |  --   |  --   |
  |   3  |   --   |  --   |  --   | <-- clock hand
  +------+--------+-------+-------+
------------------------------------------------------------
  Request #2: page 20
  >> Page 20 : MISS -> loaded into slot 1

------------------------------------------------------------
  Request #3: page 30
  >> Page 30 : MISS -> loaded into slot 2

------------------------------------------------------------
  Request #4: page 40
  >> Page 40 : MISS -> loaded into slot 3

------------------------------------------------------------
  Request #5: page 10
  >> Page 10 : HIT in slot 0 (usage now 2)

------------------------------------------------------------
  Request #6: page 50
    clock @0: page 10 usage 2 -> 1 (skip)
    clock @1: page 20 usage 1 -> 0 (skip)
    clock @2: page 30 usage 1 -> 0 (skip)
    clock @3: page 40 usage 1 -> 0 (skip)
    clock @0: page 10 usage 1 -> 0 (skip)
    evict page 20 from slot 1
  >> Page 50 : MISS -> loaded into slot 1

------------------------------------------------------------
  Request #7: page 60
    evict page 30 from slot 2
  >> Page 60 : MISS -> loaded into slot 2

------------------------------------------------------------
  Request #8: page 10
  >> Page 10 : HIT in slot 0 (usage now 1)

------------------------------------------------------------
  Request #9: page 70
    evict page 40 from slot 3
  >> Page 70 : MISS -> loaded into slot 3

------------------------------------------------------------
  Request #10: page 20
    clock @0: page 10 usage 1 -> 0 (skip)
    evict page 50 from slot 1
  >> Page 20 : MISS -> loaded into slot 1

------------------------------------------------------------
  Request #11: page 10
  >> Page 10 : HIT in slot 0 (usage now 1)

------------------------------------------------------------
  Request #12: page 30
    clock @2: page 60 usage 1 -> 0 (skip)
    clock @3: page 70 usage 1 -> 0 (skip)
    evict page 10 from slot 0
  >> Page 30 : MISS -> loaded into slot 0

============================================================

  Stats
    Total requests : 12
    Hits           : 3
    Misses         : 9
    Hit ratio      : 25.0%

============================================================
  Done.
============================================================
```

---

## 8. Files in This Submission

| File | Description |
|------|-------------|
| `clock_sweep.cpp` | C++ implementation of the clock sweep algorithm |
| `Makefile` | Build instructions |
| `Assignment.md` | This document |
| `README.md` | Quick-start guide |

---

## 9. References

- PostgreSQL source: `src/backend/storage/buffer/freelist.c` (StrategyGetBuffer)
- PostgreSQL source: `src/include/storage/buf_internals.h` (BufferDesc)
- Ramakrishnan, R. & Gehrke, J. *Database Management Systems*, Ch. 9 (Buffer Management)
- PostgreSQL docs: Buffer Manager, shared_buffers configuration
