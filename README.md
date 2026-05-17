# Clock Sweep Algorithm Implementation

This project demonstrates a simplified implementation of the Clock Sweep page replacement algorithm used in DBMS buffer management.

## Features

- Fixed size buffer pool
- Page insertion and access
- Reference bit tracking
- Circular clock hand traversal
- Page replacement using Clock Sweep

## Technologies Used

- C++
- STL
- CMake

## Algorithm

Clock Sweep is a page replacement strategy that approximates LRU.

Steps:
1. Clock hand scans pages
2. If reference bit = 1:
   - reset it to 0
   - continue scanning
3. If reference bit = 0:
   - evict page
   - insert new page

## Build

```bash
mkdir build
cd build
cmake ..
make
./storage_buffer
