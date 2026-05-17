# Clock Sweep Implementation in C++

This project implements the Clock Sweep page replacement algorithm using C++ templates and multithreading.

## Features

- Generic implementation using templates
- Fixed cache size
- Clock Sweep replacement policy
- Background thread using `std::thread`
- Thread-safe operations using `std::mutex`

## Files

- `main.cpp` → Clock Sweep implementation
- `CMakeLists.txt` → Build configuration

## Build

```bash
mkdir build
cd build
cmake ..
make
./db_engine
```

## Working

The cache stores keys along with a reference bit.

- `putKey()` inserts a key
- `getKey()` accesses a key and marks it as recently used
- When cache becomes full:
  - Pages with reference bit `1` get a second chance
  - Pages with reference bit `0` are replaced

This follows the Clock Sweep algorithm used in database systems for page replacement.
