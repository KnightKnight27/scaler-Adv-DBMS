# Lab 3 - Clock Sweep / Second Chance Algorithm

## Name
Anushka Jain

## Roll Number
24BCS10193

---

## Objective

To implement the Clock Sweep (Second Chance) Page Replacement Algorithm in C++ using a fixed-size memory buffer.

---

## Introduction

Clock Sweep is a page replacement technique used in operating systems and database buffer management.

The algorithm maintains:
- a circular list of frames
- a reference bit for every page

When memory becomes full:
- pages with reference bit = 1 are given a second chance
- their reference bit is cleared
- the clock hand moves forward
- the first page with reference bit = 0 gets replaced

This approach is an optimized approximation of the Least Recently Used (LRU) policy.

---

## Features

- Fixed-size cache
- Circular clock hand movement
- Reference bit management
- Efficient page lookup using `unordered_map`
- Page insertion and replacement
- Console-based visualization

---

## Files Included

| File | Description |
|------|-------------|
| `clock_policy.cpp` | Main implementation |
| `README.md` | Documentation |

---

## Compilation and Execution

### Linux / macOS

```bash
g++ -std=c++17 clock_policy.cpp -o clock
./clock
```

### Windows PowerShell

```powershell
g++ -std=c++17 clock_policy.cpp -o clock.exe
.\clock.exe
```

---

## Sample Working

1. Insert pages into memory
2. Access pages to set reference bits
3. Insert new pages after memory becomes full
4. Clock hand scans pages
5. Pages with reference bit 1 receive second chance
6. First page with reference bit 0 gets replaced

---

## Time Complexity

| Operation | Complexity |
|-----------|------------|
| Insert | O(1) average |
| Search | O(1) |
| Replacement | O(n) worst case |

---
---

## Challenges Faced and Solutions

### 1. Managing Circular Traversal

One challenge was correctly moving the clock hand in a circular manner without going out of bounds.  
This was solved using modulo arithmetic:

```cpp
hand = (hand + 1) % capacity;
```

---

### 2. Handling Second Chance Logic

Initially, it was difficult to correctly decide when to clear the reference bit and when to replace a page.  
This was resolved by carefully checking:
- if `refBit == true` → give second chance and clear it
- if `refBit == false` → replace the page

---

### 3. Avoiding Duplicate Pages

Another issue was handling pages that already existed in memory.  
This was solved using `unordered_map` for O(1) lookup and updating the reference bit instead of inserting duplicates.

---

### 4. Maintaining Synchronization Between Frames and Hash Map

While replacing pages, old entries sometimes remained in the hash map.  
This was resolved by removing the old page from the map before inserting the new page.

```cpp
pageTable.erase(memory[hand].page);
```

## Conclusion

The Clock Sweep Algorithm efficiently manages page replacement using second-chance logic and circular traversal. It reduces the overhead of true LRU implementation while maintaining good practical performance.