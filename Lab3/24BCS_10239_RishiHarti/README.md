# Lab 3: Clock Sweep Page Replacement Algorithm in C++
**Student:** Rishi Harti  
**Roll Number:** 24BCS10239  

---

## 1. Objective
Implement a multithreaded simulation of the **Clock Sweep (Second Chance)** buffer pool page replacement algorithm in C++, mirroring the cache eviction logic used in PostgreSQL's buffer manager.

---

## 2. Theoretical Background

The Clock Sweep algorithm approximates the **Least Recently Used (LRU)** eviction policy but with significantly lower runtime overhead and locking contention:

* **Data Structure**: Uses a circular buffer of fixed size (representing buffer pool frames) and a "clock hand" pointer.
* **Mechanism**:
  - Each frame holds a `referenceBit` (or a `usage_count` that is capped).
  - When a page is accessed (cache hit), its `referenceBit` is set to `true`.
  - When a page is requested but not cached (cache miss):
    1. The clock hand sweeps through frames sequentially.
    2. If a frame has its `referenceBit` set to `true`, the hand resets it to `false` (giving it a second chance) and moves to the next frame.
    3. If the hand encounters a frame with its `referenceBit` set to `false`, that frame is selected as the **victim**, evicted, and replaced with the new page.
    4. The clock hand advances past the victim frame.

### LRU vs. Clock Sweep Comparison:

| Dimension | LRU | Clock Sweep |
| :--- | :--- | :--- |
| **Data Structure** | Doubly-linked list + Hashmap | Circular array + Index hand pointer |
| **Time per Access** | `O(1)` (Requires lock contention on list updates) | `O(1)` (Lock-free/low-contention bit flips) |
| **Lock Contention** | **High**: Every read must modify and lock the list nodes | **Low**: Read operations only flip a bit; no re-ordering needed |

---

## 3. Implementation Details

Our implementation includes:
- **`ClockSweepCache<T>`**: Templated class managing frame states, lookups, and insertions.
- **Multithreading Protection**: Synchronizes page accesses with a `std::mutex` guard.
- **Background Sweeper**: A background thread running periodically to simulate age-out processes by resetting reference bits over time.

### Cache Eviction Visual Flow:
```
Clock Hand Sweeps Circularly:
  
      [Frame 0] (Ref=1) -> Clear Ref -> (Ref=0)
          ^
          |
  [Frame 3] (Ref=0)               [Frame 1] (Ref=1)
  *Victim! Evict!*                
          |
      [Frame 2] (Ref=0) -> Clear Ref -> (Ref=0)
```

---

## 4. Compilation & Verification

To compile and execute the simulation:

```bash
g++ -std=c++17 -pthread -o clocksweep code.cpp
./clocksweep
```

### Expected Execution Log:
```text
Inserting pages: 10, 20, 30, 40
Frame 0: 10 [R=1] <-- HAND
Frame 1: 20 [R=1]
Frame 2: 30 [R=1]
Frame 3: 40 [R=1]

Accessing page 20 (setting reference bit)
Frame 0: 10 [R=1] <-- HAND
Frame 1: 20 [R=1]
Frame 2: 30 [R=1]
Frame 3: 40 [R=1]

Inserting new page 50 (eviction expected)
Frame 0: 50 [R=1]
Frame 1: 20 [R=1]
Frame 2: 30 [R=1]
Frame 3: 40 [R=1] <-- HAND
```
This output demonstrates that the clock hand sweeps and correctly maintains hot pages in memory while evicting cold/old ones.
