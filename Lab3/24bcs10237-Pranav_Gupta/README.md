# Lab Assignment 3: Clock Sweep Buffer Replacement Policy

**Name:** Pranav Gupta  
**Roll No:** 24BCS10237  

---

## 1. Overview and Objectives
The objective of this lab is to implement and analyze the **Clock Sweep Buffer Replacement Policy** (an approximation of the Least Recently Used (LRU) policy) in a thread-safe, multi-threaded C++ database buffer manager environment. 

### Why Clock Sweep?
Standard LRU requires maintaining a sorted linked list of pages and constantly re-ordering elements on every hit/access. Under high multi-threaded database workloads, this creates severe lock contention. 
The Clock Sweep (CLOCK) algorithm avoids this by:
1. Organizing buffers in a circular logical ring.
2. Tracking a simple `usageBit` (or reference counter) per buffer.
3. Advancing a logical "clock hand" to find a victim with `usageBit == false`.
4. Achieving $O(1)$ complexity with extremely low concurrency overhead.

---

## 2. Architecture & Implementation

Our implementation introduces a robust, thread-safe template-based `BufferPool` class.

### Key Components

1. **Frame Metadata:**
   ```cpp
   struct Frame {
       K      pageId    = K{};
       bool   usageBit  = false;
       bool   occupied  = false;
   };
   ```
2. **Thread Safety (Synchronization):**
   - Implemented using a `mutable std::mutex mtx;` to protect all shared state (`frames`, `pageTable`, `hand`, `running`).
   - All public methods (`fetch()`, `load()`, `printBuffer()`) lock the mutex using `std::lock_guard` for safety.

3. **Asynchronous Ticker (Sweeper Thread):**
   - Spawns a background thread running `sweepLoop()`.
   - Periodically (every `200ms`), the clock hand ticks forward asynchronously. If it encounters an occupied page with a set `usageBit`, it resets the bit to `false` (simulating page cooling/aging over time).

4. **Correct Eviction Logic:**
   - When placing a page in a newly allocated, unoccupied slot, the clock hand is correctly advanced, preventing premature cooling of newly loaded pages.

---

## 3. Compilation & Execution

### Build Command
Compile the source code with standard C++17 and thread support:
```bash
g++ -std=c++17 -pthread main.cpp -o app
```

### Execution Log & Step-by-Step Analysis

```
=== Loading pages 10, 20, 30 ===
[LOAD]  Page 10 -> frame 0
[LOAD]  Page 20 -> frame 1
[LOAD]  Page 30 -> frame 2

--- Buffer State (hand=0) ---
  Frame[0] : page=10  usage=1
  Frame[1] : page=20  usage=1
  Frame[2] : page=30  usage=1
-----------------------------------
```
*   **Observation:** Pages 10, 20, and 30 are loaded into empty frames 0, 1, and 2 respectively. Because of our correct clock hand advancement fix, all newly loaded pages successfully retain their initial `usage=1`, and the hand points to `0`.

```
=== Fetching page 10 ===
[HIT]  Page 10 found at frame 0

--- Buffer State (hand=0) ---
  Frame[0] : page=10  usage=1
  Frame[1] : page=20  usage=1
  Frame[2] : page=30  usage=1
-----------------------------------
```
*   **Observation:** Fetching page 10 succeeds with a `[HIT]` and keeps its `usage=1`.

```
=== Loading page 40 (eviction expected) ===
[EVICT] Removing page 10 from frame 0
[LOAD]  Page 40 -> frame 0

--- Buffer State (hand=1) ---
  Frame[0] : page=40  usage=1
  Frame[1] : page=20  usage=0
  Frame[2] : page=30  usage=0
-----------------------------------
```
*   **Observation:** When loading page 40 under capacity pressure (size=3):
    1. The hand sweeps from frame 0, sees `usage=1`, clears it to `0`, and moves to frame 1.
    2. It sees frame 1 has `usage=1`, clears it to `0`, and moves to frame 2.
    3. It sees frame 2 has `usage=1`, clears it to `0`, and wraps around back to frame 0.
    4. At frame 0, `usage` is now `0`. It evicts page 10 and loads page 40. The hand correctly advances to `1`.

```
=== Fetching page 20 ===
[HIT]  Page 20 found at frame 1

=== Waiting 500ms for background sweeper to run ===
[TICKER] Page 20 in frame 1 cooled down (usage=0)
```
*   **Observation:** Fetching page 20 is a hit. We sleep for `500ms`. In the background, the sweeper thread ticks past frame 1 containing page 20 and cools it down asynchronously, demonstrating correct thread synchronization and active page aging.

```
=== Loading pages 50, 60 ===
[EVICT] Removing page 20 from frame 1
[LOAD]  Page 50 -> frame 1
[EVICT] Removing page 30 from frame 2
[LOAD]  Page 60 -> frame 2

--- Buffer State (hand=0) ---
  Frame[0] : page=40  usage=0
  Frame[1] : page=50  usage=1
  Frame[2] : page=60  usage=1
-----------------------------------
```
*   **Observation:** Loading page 50 immediately evicts page 20 because its usage bit was already cooled down asynchronously to `0` by our background ticker! Next, loading page 60 sweeps and evicts page 30.

---

## 4. Conclusion
This implementation demonstrates a robust, production-like Clock Sweep Buffer replacement policy matching modern DBMS architectures like PostgreSQL. By combining clean eviction sweeping with thread-safe mutex guards and a background sweeper thread, the buffer manager efficiently balances high performance with safe concurrent page eviction.
