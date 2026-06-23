# Lab 3: ClockSweep Buffer Pool Replacement Algorithm

**Name:** Arman Barbhuiya  
**Roll Number:** 24bcs10196 
**Course:** Advanced Database Management Systems (AdvDBMS)

---

## 1. Overview
The **Clock Sweep** (also known as the Second-Chance) algorithm is a space-efficient approximation of the Least Recently Used (LRU) page replacement policy. It is widely used by storage engine buffer managers, including **PostgreSQL**, to evict pages from memory and free up frames when the buffer cache is full.

---

## 2. Eviction Logic & State Machine

1. **Circular Buffer Structure:** The buffer cache is represented as a circular array of frames.
2. **Clock Hand:** A pointer tracks the current frame index for replacement scans.
3. **Reference Bit:** Each frame stores a boolean reference bit (initialized to `true` when a page is inserted or accessed).
4. **Eviction Loop:**
   - When eviction is triggered:
     - If the page pointed to by the clock hand has `referenceBit == true`, the bit is cleared to `false` and the hand advances to the next frame. (Giving the page a *second chance*).
     - If the page has `referenceBit == false`, it is chosen as the **victim**. The page is evicted, the new page is loaded into the frame, its reference bit is set to `true`, and the clock hand is advanced.

```
                  [Clock Hand]
                       │
                       ▼
               +---------------+
         2     | Key: 30       |     0
      [Ref: 1] |               |  [Ref: 1]
               +---------------+
                      / \
                     |   |
               +---------------+
               | Key: 20       |
               |               |
               +---------------+
                       1
                    [Ref: 1]
```

---

## 3. Complexity Analysis

| Operation | Time Complexity (Average) | Time Complexity (Worst Case) | Space Complexity |
| :--- | :--- | :--- | :--- |
| **Lookup (`getKey`)** | $O(1)$ | $O(1)$ | $O(1)$ |
| **Insertion / Eviction (`putKey`)** | $O(1)$ | $O(N)$ (where $N$ is cache size, if all reference bits are 1) | $O(N)$ auxiliary storage |

---

## 4. Compilation & Execution

To compile and run the driver script:
```bash
g++ -std=c++17 lab3/main.cpp -o clock_test
./clock_test
```