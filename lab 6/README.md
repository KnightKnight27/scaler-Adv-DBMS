# Lab 6: B-Tree Implementation in C++

This folder contains a complete, robust, and clean implementation of a B-Tree in C++.

## Overview

A **B-Tree** is a self-balancing search tree designed to work well on direct-access secondary storage devices. It generalizes binary search trees by allowing nodes to have more than two children. B-Trees are widely used in databases and file systems because they reduce disk accesses.

### Key Properties

For a B-Tree of minimum degree $t \ge 2$:
1. **Keys and Children**:
   - Every node except the root has at least $t - 1$ keys and $t$ children.
   - Every node has at most $2t - 1$ keys and $2t$ children.
2. **Sorted Order**: The keys in each node are kept in sorted order.
3. **Height Balancing**: All leaf nodes are at the same depth.

---


## How to Compile and Run

### Prerequisites

Ensure you have a C++ compiler (`g++` or `clang++` supporting C++17) and `make` utility installed.

### Compilation

To compile the application, navigate to the `lab 6` folder in your terminal and run:

```bash
make
```

This compiles `main.cpp` and `btree.cpp` into a binary named `btree_demo`.

### Running the Program

To compile and immediately execute the program, run:

```bash
make run
```

Or execute the compiled binary directly:

```bash
./btree_demo
```

### Cleaning Build Artifacts

To remove the compiled binary and object files:

```bash
make clean
```

---

## Operations Provided in Demo

1. **Insert Key**: Inserts a key into the B-Tree in sorted order, splitting nodes proactively when they are full.
2. **Search Key**: Searches for a key, printing whether the key is present in the tree or not.
3. **In-order Traversal**: Outputs keys in sorted order.
4. **Visual Tree structure**: Prints a tree structure showcasing nodes, depth, and sibling relations using standard hierarchical characters.
5. **Automated Demo**: Inserts a sequence of values (`[10, 20, 30, 40, 50, 60, 70, 80, 90, 5, 15, 25]`) into a $t=3$ B-Tree, shows the resulting visual structure, and performs search operations.
