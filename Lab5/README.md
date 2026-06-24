# Lab Assignment — Red-Black Tree in C++

**Course:** Data Structures and Algorithms / Advanced DBMS  
**Language:** C++17

---

# Overview

This repository contains a robust, from-scratch implementation of a **Red-Black Tree (RBT)**. A Red-Black Tree is a self-balancing binary search tree that guarantees `O(log n)` time complexity for basic dynamic-set operations such as search, insert, and delete.

This implementation strictly follows the standard CLRS (*Introduction to Algorithms*) methodology and utilizes a shared **Sentinel Node (`TNULL`)** instead of standard null pointers to handle leaf nodes and simplify boundary conditions.

---

# Directory Structure

| File | Description |
| :--- | :--- |
| `RedBlackTree.cpp` | Complete implementation of the Red-Black Tree including nodes, rotations, insertion fixup logic, and visual tree printing. |
| `README.md` | Documentation and analysis file. |

---

# Build and Execute

To compile and run the code from your terminal using GCC:

```bash
# Compile the source code
g++ -std=c++17 -Wall -Wextra -O2 -o rbtree RedBlackTree.cpp

# Execute the binary
./rbtree
```

---

# Sample Output

The program includes a built-in display function that visually prints the tree structure, showing parent-child relationships and node colors.

```text
Inserting elements: 55, 40, 65, 60, 75, 57

Red-Black Tree Structure:
R----55 (BLACK)
     L----40 (BLACK)
     R----65 (RED)
          L----60 (BLACK)
          |    L----57 (RED)
          R----75 (BLACK)
```

- `L` represents Left Child
- `R` represents Right Child

---

# The Five Red-Black Tree Properties

This implementation actively enforces all five universal Red-Black Tree rules:

1. Every node is either RED or BLACK.
2. The root is always BLACK.
3. Every leaf (`TNULL` sentinel node) is BLACK.
4. If a node is RED, both of its children must be BLACK.
5. For each node, all simple paths from the node to descendant leaves contain the same number of BLACK nodes (Black Height property).

---

# Implementation Highlights

## The Sentinel Node (`TNULL`)

Instead of using `nullptr` for empty child pointers, this implementation creates a single shared sentinel node called `TNULL`.

### Key Characteristics

- All newly inserted nodes point their left and right children to `TNULL`.
- `TNULL` is always colored BLACK.
- The tree root initially points to `TNULL`.

### Why Use a Sentinel Node?

Using a sentinel node eliminates many edge-case checks during rotations and insertion fixups.

For example:

```cpp
node->left->color
```

can be safely accessed without causing segmentation faults because `node->left` will reference `TNULL` rather than a raw null pointer.

This design simplifies the balancing logic and improves code robustness.

---

# Insertion Fixup Logic

Newly inserted nodes are always colored RED. If the parent node is also RED, the Red-Black Tree properties become violated and the `insertFixup()` function is triggered.

The algorithm handles three primary balancing cases (mirrored for left and right subtrees):

---

## Case 1 — Uncle is RED

### Situation
- Parent is RED
- Uncle is also RED

### Action
- Recolor parent to BLACK
- Recolor uncle to BLACK
- Recolor grandparent to RED
- Move upward to continue checking violations

---

## Case 2 — Triangle Formation

### Situation
- Uncle is BLACK
- Node forms a triangle structure

Example:
- Node is a right child
- Parent is a left child

### Action
- Perform a rotation around the parent
- Convert the structure into Case 3

---

## Case 3 — Line Formation

### Situation
- Uncle is BLACK
- Node and parent form a straight line

Example:
- Node and parent are both left children

### Action
- Recolor parent to BLACK
- Recolor grandparent to RED
- Rotate around the grandparent

---

# Time Complexity Analysis

| Operation | Average Time Complexity | Worst Case Time Complexity |
| :--- | :--- | :--- |
| Search | `O(log n)` | `O(log n)` |
| Insert | `O(log n)` | `O(log n)` |
| Delete | `O(log n)` | `O(log n)` |
| Space Complexity | `O(n)` | `O(n)` |

---

# Conclusion

This implementation demonstrates how Red-Black Trees maintain efficient balanced search performance using rotations, recoloring, and strict structural properties.

The use of a sentinel node (`TNULL`) significantly simplifies tree operations while preserving safety and consistency. Because the height of the tree remains logarithmic relative to the number of nodes, all major operations execute efficiently even for large datasets.