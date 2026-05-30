# B-Tree Lab 6

## Overview
This folder contains a C++ implementation of a B-Tree with insertion, search, and traversal.

## Files
- main.cpp: B-Tree implementation with a small demo in main().

## Build and Run

```bash
g++ -std=c++17 -O2 -Wall -Wextra -pedantic main.cpp -o btree
./btree
```

## Notes
- The B-Tree minimum degree is set to 3 in main().
- The demo inserts a fixed set of keys, prints traversal, and searches for one key.