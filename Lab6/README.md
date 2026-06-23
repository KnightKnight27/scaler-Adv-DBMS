# Advanced DBMS Lab 6: B-Tree Implementation

This repository contains my C++ implementation of a B-Tree from scratch, done as part of Lab 6 for the Advanced DBMS course. B-Trees are self-balancing search trees commonly used in databases (like MySQL and PostgreSQL) and file systems because they help minimize disk I/O.

## What's Included

- `btree.cpp`: The main source code. It includes the `BTreeNode` and `BTree` classes, plus a simple terminal-based interactive menu to test the operations.
- `makefile`: A makefile to compile the code easily.
- `README.md`: This file!

## How to Run

I've included a makefile, so compiling and running the code is pretty straightforward. Open your terminal and use these commands:

```bash
make          # Compiles the code and creates an executable named 'btree'
make run      # Compiles and runs the program immediately
make clean    # Cleans up the compiled binary
```

If you prefer to compile it manually:
```bash
g++ -std=c++17 -Wall -Wextra -O2 -o btree btree.cpp
./btree
```

## Features

When you run the program, it will ask you to enter the minimum degree (t) for the B-Tree. Once that's set, you can use the interactive menu to do the following:

1. **Insert:** Add new keys to the B-Tree. The code automatically handles node splitting when nodes get full.
2. **Search:** Look up specific keys efficiently.
3. **Traverse:** Print out all the keys currently in the tree in ascending order.

## B-Tree Properties Maintained

My implementation ensures that all standard B-Tree rules are maintained during operations:
- Every node (except the root) has between `t - 1` and `2t - 1` keys.
- Nodes are split proactively when they reach their maximum capacity (`2t - 1` keys) before new keys are inserted.
- All leaf nodes stay at exactly the same depth.
- Keys inside nodes are always sorted in ascending order.
- An internal node with `k` keys always has `k + 1` children.

## Complexity

Here is a quick overview of the time and space complexity for operations on a B-Tree with `n` keys and minimum degree `t`:

- **Search:** Time `O(t * log_t n)`, Space `O(log_t n)`
- **Insertion:** Time `O(t * log_t n)`, Space `O(log_t n)`
- **Traversal:** Time `O(n)`, Space `O(log_t n)`
