# Clock Sweep Buffer Manager (C++)

## Overview

This project implements a simplified version of the Clock Sweep page replacement algorithm used in database buffer pool management systems.

The Clock Sweep algorithm is commonly used in DBMS systems as an efficient approximation of the Least Recently Used (LRU) replacement policy.

---

## Features

- Fixed-size buffer pool
- Circular clock hand traversal
- Reference bit tracking
- Efficient page lookup using hash map
- Automatic page eviction
- Buffer visualization output

---

## Project Structure

```bash
storage_buffer/
│── main.cpp
│── CMakeLists.txt
│── README.md
```

---

## Working of Clock Sweep

Each frame in the buffer contains:

- Page ID
- Reference bit
- Validity flag

### Replacement Logic

1. New pages are inserted into empty frames.
2. Accessed pages get their reference bit set to `1`.
3. When the buffer becomes full:
   - The clock hand scans frames circularly.
   - If reference bit = `1`
     - reset it to `0`
     - move ahead
   - If reference bit = `0`
     - evict the page
     - insert the new page

This gives recently used pages a "second chance".

---

## Build Instructions

### Using CMake

```bash
mkdir build
cd build
cmake ..
make
./storage_buffer
```

---

## Sample Operations

- Page insertion
- Page hit detection
- Page eviction
- Buffer state printing

---

## Technologies Used

- C++17
- STL Containers
- CMake Build System
