# Lab 3 - Clock Sweep Page Replacement Algorithm

## Student Details

- Name: Ayushkumar Singh
- Roll Number: 24BCS10199

---

## Objective

The objective of this lab is to implement the Clock Sweep page replacement algorithm in C++.

The Clock Sweep algorithm is used in operating systems and database buffer managers to efficiently replace pages when memory becomes full. It improves upon FIFO by giving recently accessed pages a second chance before eviction.

---

## Algorithm Overview

Clock Sweep maintains:

- A fixed-size circular buffer
- A reference bit for every frame
- A moving clock hand pointer

### Working

1. When a page is accessed, its reference bit becomes `true`.
2. When inserting a new page into a full buffer:
   - The clock hand scans frames circularly.
   - If the reference bit is `true`, the algorithm clears it and gives the page a second chance.
   - If the reference bit is `false`, that page is evicted.
3. The new page is inserted into the selected frame.

---

## Features

- Generic templated implementation
- O(1) page lookup using `unordered_map`
- Fixed-size cache
- Second chance mechanism
- Page hit and miss detection
- Buffer visualization support

---

## Files Included

- `clock_sweep.cpp`
- `README.md`

---

## Compilation

### Linux / macOS

```bash
g++ -std=c++17 -Wall -Wextra -pedantic Lab3/24bcs10199_Ayushkumar_Singh/clock_sweep.cpp -o clock_sweep
./clock_sweep