# B-Tree Implementation in C++

## Overview

This assignment implements a B-Tree data structure in C++.

A B-Tree is a self-balancing multi-way search tree commonly used in:

- Databases
- File systems
- Indexing systems

This implementation supports:

1. Insertion
2. Search
3. Inorder Traversal

---

## Features

- Dynamic node splitting
- Balanced tree maintenance
- Efficient searching
- Sorted traversal output

---

## B-Tree Properties

For a B-Tree of minimum degree `t`:

- Every node can contain at most `2t - 1` keys.
- Every internal node can have at most `2t` children.
- All leaf nodes are at the same level.
- Keys inside each node are stored in sorted order.

---

## Files
