# Clock Sweep Page Replacement Algorithm in C++

This project implements the **Clock Sweep (Second Chance)** page replacement algorithm in C++.

The Clock Sweep algorithm is commonly used in operating systems for memory management and acts as an efficient approximation of the LRU (Least Recently Used) algorithm.

---

## Features

- Page hit detection
- Page fault handling
- Circular clock hand movement
- Second chance mechanism using reference bits
- Frame visualization after each page request

---

## Algorithm Overview

Each page frame contains:

- A page number
- A reference bit

### Rules

1. If a page is accessed and already exists:
   - Mark its reference bit as `1`
   - It is a **HIT**

2. If a page fault occurs:
   - Move the clock hand circularly
   - If reference bit = `1`
     - Set it to `0`
     - Skip that frame
   - If reference bit = `0`
     - Replace that page

---

## Build Instructions

### Step 1: Create Build Directory

```bash
mkdir build
cd build