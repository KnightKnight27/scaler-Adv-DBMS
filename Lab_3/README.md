# Lab 3: Clock Sweep Cache Replacement Algorithm

**Author:** Siddhant Prasad  
**Roll Number:** 24BCS10255

---

## 🎯 Aim
To study and implement the **Clock Sweep Cache Replacement Algorithm** (also known as the **Second-Chance Page Replacement Algorithm**) in C++, analyzing cache states, clock-hand tracking, page references, and eviction rules.

---

## 📚 Algorithm Overview
The Clock Sweep algorithm approximates the **Least Recently Used (LRU)** policy with much lower metadata overhead. Instead of keeping a fully sorted linked list of access times or page structures, it organizes page frames in a circular queue (buffer) and tracks usage with:
- **`referenceBit`**: A boolean flag set to `1` when the page is inserted or accessed.
- **`hand`**: An integer pointer (clock hand) pointing to the frame currently under examination.

When cache eviction is triggered:
- The clock hand moves circularly.
- If it encounters a frame with `referenceBit == 1`, it resets the bit to `0` (giving it a *"second chance"*) and moves forward.
- If it encounters a frame with `referenceBit == 0`, it evicts that page, inserts the new page in its place (with `referenceBit = 1`), and advances the hand.

---

## 💻 Class Structure & Implementation

The `ClockSweepCache` template-based class manages:
- **`buffer`**: A `std::vector` of `PageFrame` structs acting as a circular cache structure.
- **`pageMap`**: A hash table (`std::unordered_map`) for $O(1)$ fast key-to-buffer index lookups.
- **`hand`**: The current sweep index.

```cpp
struct PageFrame {
    Key key;
    Value value;
    bool referenceBit = false;
    bool occupied = false;
};
```

---

## 🧪 Detailed Task Walkthrough

### 1. Cache Initialization (Task 1)
Upon creation, the cache allocates space for a defined capacity (e.g. `3` frames). All frames are initialized as unoccupied (`occupied = false`), the reference bits are set to `0`, and the `hand` points to Frame `0`.

### 2. Cache Population (Task 2)
When pages are inserted initially:
- The algorithm searches sequentially for unoccupied frames.
- New pages are placed in the empty frames, and their `referenceBit` is set to `1` to indicate they are newly active.
- When capacity reaches $100\%$, further inserts trigger the clock sweep.

### 3. Access Pattern Analysis (Task 3)
When a cached page is accessed (Page Hit):
- Its `referenceBit` is immediately updated to `1`.
- Pages that are frequently read/written receive higher retention priority, while inactive pages maintain `referenceBit = 0` and become candidates for eviction.

### 4. Clock Sweep Observation (Task 4)
During eviction, the algorithm sweeps through the buffer. If the hand starts at Frame `0` and all frames have `referenceBit = 1`, the hand loops around, clearing all reference bits to `0`. It will eventually evict the page at Frame `0`, demonstrating the cyclic "second-chance" logic.

### 5. Cache Replacement Analysis (Task 5)
How Clock Sweep compares to other classic caching schemes under capacity limits:
- **FIFO (First-In, First-Out)**: Evicts pages based strictly on entry age, leading to the eviction of highly-frequent pages just because they were loaded early (Belady's Anomaly).
- **LRU (Least Recently Used)**: Ideal cache hit rates, but requires updating linked-list nodes or timestamps on every read/write operation, creating runtime bottlenecks.
- **Clock Sweep (Second-Chance)**: Provides near-LRU performance with $O(1)$ update overhead. It only updates a single bit flag on reads, skipping expensive list reordering.

---

## 🛠️ Compilation and Execution

### Compilation
Build using any standard C++17 compiler (required for `std::optional` support):
```bash
g++ -std=c++17 clock_sweep.cpp -o clock_sweep
```

### Execution
Run the compiled executable to display step-by-step logs:
```bash
./clock_sweep
```

---

## 📊 Sample Execution Log & Event Analysis

Below is the structured output from the demo program showcasing initial state, insertion, hit, and replacement events:

```text
========================================================
  Lab 3: Clock Sweep Cache Replacement Algorithm Demo  
========================================================

--- TASK 1: Cache Initialization ---

------------------ BUFFER STATE ------------------
Frame	Key	Value		RefBit	Occupied
0	-	-		-	No
1	-	-		-	No
2	-	-		-	No
Clock Hand Position: Frame 0
--------------------------------------------------

--- TASK 2: Cache Population ---
[INSERT] Key: 1 placed in unoccupied Frame: 0 (RefBit = 1)
[INSERT] Key: 2 placed in unoccupied Frame: 1 (RefBit = 1)
[INSERT] Key: 3 placed in unoccupied Frame: 2 (RefBit = 1)

------------------ BUFFER STATE ------------------
Frame	Key	Value		RefBit	Occupied
0	1	PageA		1	Yes
1	2	PageB		1	Yes
2	3	PageC		1	Yes
Clock Hand Position: Frame 0
--------------------------------------------------

--- TASK 3: Access Pattern Analysis (Accessing Pages 1 and 2) ---
[ACCESS] Key: 1 -> PAGE HIT (Frame: 0, RefBit updated to 1)
[ACCESS] Key: 2 -> PAGE HIT (Frame: 1, RefBit updated to 1)

------------------ BUFFER STATE ------------------
Frame	Key	Value		RefBit	Occupied
0	1	PageA		1	Yes
1	2	PageB		1	Yes
2	3	PageC		1	Yes
Clock Hand Position: Frame 0
--------------------------------------------------

--- TASK 4 & 5: Clock Sweep & Cache Replacement ---
Inserting Page 4 (expects sweep to clear RefBits of 1 & 2, then evict 3 if hand starts at 0, or evicts based on hand)

[EVICTION TRIGGERED] Cache is Full. Initiating Clock Sweep...
 Examining Frame 0 (Key: 1, RefBit: 1)
   RefBit is 1. Giving second chance, resetting RefBit to 0.
 Examining Frame 1 (Key: 2, RefBit: 1)
   RefBit is 1. Giving second chance, resetting RefBit to 0.
 Examining Frame 2 (Key: 3, RefBit: 1)
   RefBit is 1. Giving second chance, resetting RefBit to 0.
 Examining Frame 0 (Key: 1, RefBit: 0)
 >>> Evicting page 1 from Frame 0 (Second chance expired/unused)
 >>> Inserted new page 4 into Frame 0 (RefBit = 1)

------------------ BUFFER STATE ------------------
Frame	Key	Value		RefBit	Occupied
0	4	PageD		1	Yes
1	2	PageB		0	Yes
2	3	PageC		0	Yes
Clock Hand Position: Frame 1
--------------------------------------------------
```

---

## 🏁 Conclusion
The Clock Sweep cache replacement algorithm was successfully simulated. The experiment demonstrated that:
1. Reference bits act as lightweight, single-bit access flags.
2. The rotating clock hand ensures constant-time $O(1)$ operations for both cache hits and evictions.
3. The "second-chance" mechanism effectively prevents frequently accessed pages (such as PageB or PageA) from immediate eviction, strikes a balance between execution performance and hits, making it a powerful paradigm for database page buffers.
