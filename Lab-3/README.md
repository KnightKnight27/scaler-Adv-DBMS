# Lab 3 — Clock Sweep Buffer Pool Replacement Algorithm

**Course:** Advanced DBMS  
**Author:** Vedanshu Nishad (24BCS10285)  
**Language:** C++17

---

## Objective

Implement PostgreSQL's **Clock Sweep** buffer pool eviction algorithm. This is a practical approximation of LRU (Least Recently Used) that avoids the overhead of maintaining a full ordered list.

---

## Background: Why Clock Sweep?

### The Problem with LRU
- **Full LRU:** Every access requires updating a linked list (O(1) but constant overhead)
- **For a buffer pool with thousands of frames:** This overhead becomes significant
- **PostgreSQL's solution:** Clock Sweep — approximates LRU with minimal overhead

### The Clock Sweep Concept
```
Imagine a clock face with N buffer frames arranged in a circle.
A "clock hand" points to the next frame to inspect.

When we need to evict a frame:
1. Check the frame pointed by the hand
2. If its usage_count > 0: 
   - Decrement usage_count (give it a "second chance")
   - Advance the hand
3. If its usage_count == 0 (and not pinned):
   - Evict this frame
   - Insert the new page here
   - Advance the hand
```

---

## Files

| File | Purpose |
|------|---------|
| `clock_sweep.cpp` | Full implementation with Frame struct, BufferPool class |
| `CMakeLists.txt` | Build configuration |
| `README.md` | This document |

---

## Build & Run

```bash
mkdir -p build
cd build
cmake ..
make
./clock_sweep
```

---

## Implementation Details

### Frame Structure
```cpp
struct Frame {
    int  page_id;      // -1 = empty
    int  usage_count;  // 0 to 5 (5 is max in PostgreSQL)
    bool pinned;       // cannot evict if pinned
};
```

### Key Methods

#### `fetch(int page_id)` — Fetch a page into the buffer pool
```
1. If page already in pool:
   - Increment usage_count (capped at 5)
   - Return frame index
   
2. If empty frame available:
   - Load page into empty frame
   - Set usage_count = 1
   - Return frame index
   
3. All frames occupied:
   - Run clock sweep to find victim
   - Evict victim frame
   - Load new page
```

#### `clocksweep()` — Find a victim frame to evict
```
1. Start from current hand position
2. Loop through frames:
   - If pinned: skip and advance hand
   - If usage_count > 0: 
     - Decrement usage_count (second chance)
     - Advance hand
   - If usage_count == 0:
     - Return this frame as victim
3. Advance hand for next eviction
```

#### `pin(page_id)` / `unpin(page_id)` — Lock frames from eviction
- Pinned frames cannot be evicted (used during page access)
- Common pattern: pin page while reading it, unpin when done

---

## Sample Output

```
=== ClockSweep Buffer Pool Demonstration ===

--- Scenario 1: Fill buffer pool ---
[LOAD] page 1 into empty frame 0
[LOAD] page 2 into empty frame 1
[LOAD] page 3 into empty frame 2
[LOAD] page 4 into empty frame 3

--- Buffer Pool State (hand=0) ---
Frame   Page ID    Usage   Pinned
----------------------------------
0       1          0       NO
1       2          0       NO
2       3          0       NO
3       4          0       NO

--- Scenario 2: Re-access page 1 (increases usage) ---
[HIT]  page 1 in frame 0 usage=1
[HIT]  page 1 in frame 0 usage=2

--- Scenario 3: Load new page 5 (triggers clock sweep) ---
[SWEEP] Frame 0 (page 1) gets second chance, usage=1
[SWEEP] Frame 1 (page 2) selected as victim
[EVICT] page 2 from frame 1, loaded page 5

--- Scenario 4: Heavy workload (mix of access patterns) ---
[HIT]  page 3 in frame 2 usage=1
[HIT]  page 3 in frame 2 usage=2
[HIT]  page 2 in frame ... MISS (page 2 was evicted, reloading)
```

---

## Algorithm Analysis

### Time Complexity
- **Fetch (hit):** O(1) — Direct lookup in hash map
- **Fetch (miss, eviction needed):** O(N) worst case — May sweep entire buffer pool
- **In practice:** O(1) amortized — Most frames have usage_count > 0, so clock hand moves fast

### Space Complexity
- **O(N)** where N = number of frames
- Hash map page_to_frame: O(N)
- Frames vector: O(N)

### Why It Works

1. **Recently accessed pages:** Have higher usage_count, get second chances
2. **Infrequently accessed pages:** Eventually get usage_count = 0, get evicted
3. **Fair distribution:** The clock sweeps uniformly, not biased to any region
4. **Pinned pages:** Never evicted (critical for correctness)

---

## Design Decisions

### 1. Usage Count Range (0-5)
- PostgreSQL uses 0-5 range (not just 0-1)
- More granularity = better distinguishing between hot and cold pages
- Trade-off: Minimal, since decrement happens once per eviction sweep

### 2. Pinning Mechanism
- Pages are pinned while being accessed by a transaction
- Prevents eviction while transaction holds a reference
- Critical for correctness: cannot evict a page someone is reading

### 3. Hand Advancement
- Hand always advances after each decision
- Ensures all frames get fair consideration
- Prevents starvation

---

## Trade-offs vs LRU

| Aspect | LRU | Clock Sweep |
|--------|-----|-------------|
| **Implementation** | Linked list | Array + clock hand |
| **Access time** | O(1) to update list | O(1) to update usage_count |
| **Eviction time** | O(1) remove from list | O(N) worst case sweep |
| **Memory overhead** | Next/prev pointers per frame | Just usage_count per frame |
| **Accuracy** | Perfect LRU | Approximation of LRU |
| **Predictability** | Random eviction | Deterministic sweep |

**PostgreSQL's choice:** Clock Sweep trades perfect LRU accuracy for simplicity and better cache locality.

---

## Connection to Database Systems

### PostgreSQL Implementation
- Located in: `src/backend/storage/buffer/bufmgr.c`
- Uses 5-level usage count (0-5, PostgreSQL calls it `usage_count`)
- Buffer frames called "buffer descriptors"
- Runs in separate background thread for clock sweep

### When Does Eviction Happen?
1. Buffer pool is full AND need to load a new page
2. Background process runs periodic evictions
3. Checkpoint process may trigger bulk eviction

### Real-World Scenarios
```
SCENARIO 1: Sequential scan of large table
- Pages accessed once, usage_count stays low
- Quickly evicted to make room
- New pages loaded for next batch

SCENARIO 2: Index lookup with repeated access
- Hot pages accessed multiple times
- High usage_count, multiple second chances
- Stay in buffer pool longer

SCENARIO 3: Hash join with large inner table
- Inner table pages accessed frequently
- Very high usage_count
- Compete to stay in buffer pool
```

---

## Testing the Implementation

### Test 1: Basic Load/Eviction
```cpp
pool.fetch(1);  // Load page 1
pool.fetch(2);  // Load page 2
pool.fetch(3);  // Load page 3
pool.fetch(4);  // Load page 4 (fills pool)
pool.fetch(5);  // Evicts something, loads page 5
```

### Test 2: Re-access Pattern
```cpp
pool.fetch(1);
pool.fetch(1);  // Increment usage_count for page 1
pool.fetch(2);  // Page 1 gets more chances before eviction
```

### Test 3: Pinning
```cpp
pool.pin(1);
pool.fetch(5);  // Cannot evict page 1 (pinned)
```

---

## Key Learnings

1. **Buffer pool eviction is critical for DBMS performance**
   - Wrong eviction policy can cause thrashing
   - Clock sweep is simple but effective

2. **Trade-offs in system design**
   - Perfect LRU is expensive to maintain
   - Approximate algorithms often work better in practice

3. **Predictability matters**
   - Clock sweep's deterministic behavior helps debugging
   - Cache locality improved by sequential sweeping

4. **PostgreSQL chooses pragmatism**
   - Not the theoretically perfect solution
   - Works well for real-world workloads
   - Minimal implementation complexity

---

## Follow-up Experiments

1. **Simulate different workloads:** Sequential, random, locality-biased access patterns
2. **Measure hit rate:** Track cache hits vs misses
3. **Compare with LRU:** Implement true LRU and compare performance
4. **Vary buffer pool size:** See how eviction rate changes
5. **Parallel evictions:** Multiple threads accessing buffer pool simultaneously

---

## References

- PostgreSQL Source: `src/backend/storage/buffer/bufmgr.c`
- CMU 15-445 Database Systems Course - Buffer Pools lecture
- "Database Internals" by Alex Petrov - Chapter on Buffer Management
- Linux kernel clock sweep for page reclamation: `mm/vmscan.c`

