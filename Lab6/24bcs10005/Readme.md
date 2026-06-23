# Lab 6 — B-Tree Implementation

**Course:** Advanced DBMS
**Author:** Veda Varshit N
**Roll No:** 24BCS10005

---

# Overview

This project presents a complete implementation of a **B-Tree** data structure developed entirely from scratch using C++. B-Trees are widely used in database engines and file systems because they efficiently manage large volumes of data while minimizing disk access operations.

Unlike binary search trees, a B-Tree stores multiple keys inside a single node and allows each node to have several children. This design keeps the tree height very small, making operations such as insertion, deletion, and searching extremely efficient even for massive datasets.

The program provides a menu-driven command-line interface supporting:

* Insertion of keys
* Deletion of keys
* Searching for keys
* Tree visualization using inorder and level-order traversal

---

# Table of Contents

1. Project Files
2. Compilation & Execution
3. Example Execution
4. Fundamental B-Tree Rules
5. Insertion Mechanism
6. Deletion Mechanism
7. Searching & Traversal
8. Time Complexity

---

# 1. Project Files

| File Name   | Description                                                                           |
| ----------- | ------------------------------------------------------------------------------------- |
| `main.cpp`  | Contains the complete B-Tree implementation, node structure, algorithms, and CLI menu |
| `Makefile`  | Automates compilation, execution, and cleanup                                         |
| `README.md` | Documentation explaining the project and implementation details                       |

---

# 2. Compilation & Execution

## Using Makefile

```bash
make
```

Builds the executable file named `btree`.

```bash
make run
```

Compiles and launches the application.

```bash
make clean
```

Deletes generated binaries and temporary build files.

---

## Manual Compilation

```bash
g++ -std=c++17 -Wall -Wextra -O2 -o btree main.cpp
./btree
```

---

## Minimum Degree

When the program starts, the user must provide the minimum degree `t` of the B-Tree.

Condition:

```text
t >= 2
```

The value of `t` controls:

* Maximum keys per node
* Minimum keys per node
* Maximum children per node
* Overall branching factor of the tree

---

# 3. Example Execution

```text
Enter minimum degree (t >= 2): 3
```

After inserting:

```text
10, 20, 30, 40, 50, 5, 15, 25, 35, 45
```

Level-order representation:

```text
L0: [30]
L1: [5, 10, 15, 20, 25]   [35, 40, 45, 50]
```

Inorder traversal:

```text
5 10 15 20 25 30 35 40 45 50
```

After deleting key `30`:

```text
L0: [25]
L1: [5, 10, 15, 20]   [35, 40, 45, 50]
```

In this case, the predecessor key `25` replaces the deleted internal key.

---

# 4. Fundamental B-Tree Rules

For a B-Tree with minimum degree `t`:

1. Keys inside every node remain sorted in ascending order.
2. Every non-root internal node contains at least `t - 1` keys.
3. A node can store at most `2t - 1` keys.
4. A node with `k` keys always contains exactly `k + 1` children.
5. All leaf nodes appear at the same depth, ensuring the tree remains balanced.
6. Child subtrees maintain strict key boundaries relative to parent keys.

---

# 5. Insertion Mechanism

Insertion follows a **top-down splitting strategy**.

Whenever traversal encounters a full node containing:

```text
2t - 1 keys
```

the node is split immediately before continuing further.

## Split Process

* The median key moves upward into the parent node.
* Remaining keys are divided equally between two child nodes.
* Each resulting node contains `t - 1` keys.

This proactive splitting guarantees that insertion never descends into a full node.

If the root node becomes full:

* A new root is created
* Tree height increases by one
* Balance is preserved automatically

---

# 6. Deletion Mechanism

Deletion in a B-Tree is more complex because the minimum key constraints must always be maintained.

Before recursion proceeds into a subtree, the algorithm ensures the target child has at least `t` keys.

---

## Case 1 — Deleting from a Leaf Node

If the target key exists in a leaf node:

* The key is removed directly.

---

## Case 2 — Deleting from an Internal Node

If the key exists in an internal node:

### a) Use Predecessor

If the left child contains at least `t` keys:

* Replace the target key with its predecessor
* Recursively delete the predecessor

### b) Use Successor

If the right child contains at least `t` keys:

* Replace the target key with its successor
* Recursively delete the successor

### c) Merge Children

If both children contain only `t - 1` keys:

* Merge the two children
* Pull the separating parent key downward
* Continue deletion recursively in the merged node

---

## Case 3 — Key Not Yet Found

While descending:

* If the next child contains only `t - 1` keys,
  the algorithm first repairs it by:

### Borrowing

Take a key from an adjacent sibling through the parent.

### Merging

Combine two sibling nodes if borrowing is impossible.

This guarantees recursive calls never descend into an underflow node.

---

# 7. Searching & Traversal

## Search Operation

The search algorithm scans keys within the current node:

* If the key matches, search succeeds.
* Otherwise, traversal moves to the correct subtree based on key ranges.

Because the tree height remains logarithmic, searching is highly efficient.

---

## Inorder Traversal

Inorder traversal prints keys in globally sorted order:

```text
Left Subtree → Key → Right Subtree
```

---

## Level-Order Traversal

Level-order traversal uses Breadth-First Search (BFS) with a queue.

This visualization clearly shows:

* Tree levels
* Node distribution
* Structural balance
* Splits and merges

---

# 8. Time Complexity

For a B-Tree containing `n` keys and minimum degree `t`:

| Operation | Complexity   |
| --------- | ------------ |
| Height    | O(log_t n)   |
| Search    | O(t log_t n) |
| Insert    | O(t log_t n) |
| Delete    | O(t log_t n) |

---

# Conclusion

This implementation demonstrates how B-Trees efficiently maintain balanced multi-way search structures while supporting fast dynamic updates.

Because of their shallow height and optimized node organization, B-Trees are heavily used in:

* Database indexing
* File systems
* Storage engines
* Disk-based search structures

The project provides practical insight into advanced balanced tree algorithms, node splitting, merging strategies, and efficient hierarchical storage management.
