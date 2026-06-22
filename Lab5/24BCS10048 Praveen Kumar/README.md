# Red-Black Tree

A C++ implementation of the **red-black tree** (CLRS chapter 13) with insert, erase, search, and in-order traversal. Every mutation verifies all 5 red-black properties.

## What It Does

1. **Inserts** key-value pairs and restores balance via fix-up rotations and recolouring
2. **Erases** keys and fixes any black-height deficit
3. **Searches** in O(log n) with a simple BST traversal
4. **Checks invariants** after every operation in the demo

## Quick Start

```bash
# Build
make

# Run
make run
```

Or manually:

```bash
g++ -std=c++17 -O2 -o rb_tree rb_tree.cpp
./rb_tree
```

## Red-Black Properties

| # | Property |
|---|----------|
| 1 | Every node is RED or BLACK |
| 2 | The root is BLACK |
| 3 | Every NIL leaf is BLACK |
| 4 | No RED node has a RED child |
| 5 | Every root-to-NIL path has the same number of BLACK nodes |

## Documentation

See [Assignment.md](Assignment.md) for the full writeup: insert/delete fix-up cases, rotation diagrams, complexity analysis, and DBMS context.

Praveen Kumar
24bcs10048

## Requirements

- g++ with C++17 support
