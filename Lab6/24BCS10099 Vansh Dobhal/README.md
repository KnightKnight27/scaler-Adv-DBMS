# Lab 6 — B-Tree Implementation (C++17)

**Name:** Vansh Dobhal  
**Roll No:** 24BCS10099  
**Course:** Advanced DBMS

This repository contains a header-only, templated B-Tree (`lab6::BTree<K, V, Compare>`) parameterised by minimum degree `t`. It follows the standard CLRS (Introduction to Algorithms) approach for B-Trees:
- Insertions perform a proactive split as they descend the tree, ensuring no upward passes are necessary.
- Deletions handle various edge cases (borrowing from siblings, merging, and predecessor/successor replacements) as described in CLRS chapter 18.3.

The implementation comes with a robust `validate()` method that checks all five fundamental B-Tree invariants after every structural modification.

## Why are we studying B-Trees?

The standard data structures for relational databases like PostgreSQL, SQLite, and InnoDB rely heavily on B-Trees (or B+ Trees) for indexing. 
A standard binary search tree (like a Red-Black Tree) scales poorly to disk because each node only contains a single key, leading to a tall tree where each level traversal might cost an expensive disk I/O.

A B-Tree of degree `t` can hold up to `2t - 1` keys per node. By choosing a `t` such that the node fits perfectly within a single database page (e.g., 4KB or 8KB), the tree's height drastically decreases. A B-Tree indexing millions of rows typically requires only 3 to 4 disk page fetches.

## The Invariants Validated

Our `validate()` method actively checks these properties:
1. Every node holds between `t - 1` and `2t - 1` keys (except the root, which can hold fewer).
2. Internal nodes with `k` keys always have exactly `k + 1` children.
3. Keys within every node are strictly sorted in ascending order.
4. The keys separate the ranges of subtrees (standard search tree property).
5. All leaf nodes appear at the exact same depth.

## Build and Run Instructions

You can use standard CMake to build and test the implementation:

```bash
mkdir build && cd build
cmake ..
make
./btree_demo
```

Alternatively, you can compile it directly via a C++17 compiler:
```bash
g++ -std=c++17 -Wall -Wextra -O3 main.cpp -o btree_demo
./btree_demo
```

## Stress Testing

The demo (`main.cpp`) includes several validation phases, from simple insertions and lookups to a comprehensive stress test. The stress test runs 8,000 randomized operations against a standard `std::map` oracle to ensure absolute correctness of sizes, searches, and in-order traversals.
