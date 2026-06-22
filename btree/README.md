# B-Tree Module

This folder contains a simple C++ implementation of a B-tree data structure.

## Contents

- `BTree.h` - generic B-tree template with insertion, search, and traversal support.
- `main.cpp` - demo program that inserts values, traverses the tree, and searches for keys.
- `CMakeLists.txt` - build configuration for the B-tree demo executable.

## Build

From the `btree` folder:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Run

```bash
./btree_demo
```

## Features

- supports a configurable minimum degree `t`
- ordered traversal of stored keys
- search operation in B-tree structure
- automatic node split when a node becomes full

## Notes

This implementation is designed for learning and demonstration. It is not optimized for disk-based storage or concurrency.
