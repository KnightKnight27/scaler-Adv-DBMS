# Lab 6 - B-Tree Implementation

## Student Details
- **Name**: Rohan Ranjan
- **Roll Number**: 24BCS10428

## Purpose
The objective of this lab is to design and implement a standard B-Tree data structure in C++17. We demonstrate key features including node splitting (top-down), key searching, inorder sorted traversal, and level-order visualization to trace splitting activities.

## Files Included
- `btree.cpp`: A fully functional and robust C++17 B-Tree implementation with a minimum degree of 3.

## Compile And Run

To compile and execute the implementation on your system, use the following commands:

```bash
# Compile using C++17 standard
g++ -std=c++17 lab6/btree.cpp -o lab6/btree

# Execute the binary
./lab6/btree
```

*(Note: On Windows, use `.\lab6\btree.exe` to execute.)*

## Key Operations
- **Top-Down Insertion**: Inserts new keys into the tree, proactively splitting any full nodes along the path down to the leaves to maintain B-Tree balance.
- **Search**: Recursively performs binary search within nodes and moves down child pointers to retrieve existing or detect missing elements.
- **Sorted Traversal (Inorder)**: Traverses nodes recursively, producing sorted key outcomes.
- **Visual Level-Order Layout**: Utilizes a queue-based breadth-first traversal to print nodes at each level, proving tree balance.
- **Invariants Validation**: Validates that all node keys remain sorted, node sizes adhere to the minimum degree `t` constraint, and all leaf nodes reside at the exact same depth level.

## Core Concepts
A B-Tree stores multiple sorted keys per node, keeping the height shallow and optimizing disk block accesses or database indexing architectures. When a node becomes full (reaching $2t - 1$ keys), it undergoes splitting. The median key rises to the parent node, and the remaining left and right segments form two separate child nodes.
