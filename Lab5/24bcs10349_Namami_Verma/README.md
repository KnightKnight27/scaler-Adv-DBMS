# Lab 5 — Red-Black Tree Implementation (C++)

## Overview

This project implements a **Red-Black Tree (RBT)** in C++. A Red-Black Tree is a self-balancing Binary Search Tree (BST) that guarantees **O(log n)** time complexity for insertion and search operations by enforcing a set of structural coloring properties.

---

## Objectives

- Implement a Red-Black Tree data structure in C++
- Perform insertion with automatic balancing
- Understand and apply left/right tree rotations
- Maintain all Red-Black Tree invariants after every insertion

---

## Red-Black Tree Properties

Every valid Red-Black Tree satisfies these four properties:

1. Each node is colored either **RED** or **BLACK**
2. The root node is always **BLACK**
3. No two consecutive **RED** nodes may appear on any path (a red node cannot have a red parent or red child)
4. Every path from any node down to its NULL leaf descendants contains the same number of **BLACK** nodes

---

## Project Structure

```
.
├── Makefile
├── README.md
├── RedBlackTree.h      # Class declaration and node structure
├── RedBlackTree.cc     # Implementation of RBT operations
└── main.cc             # Driver program and test cases
```

---

## Compile and Run

### Using Makefile

```bash
make
./rbt
```

### Manual Compilation

```bash
g++ main.cc RedBlackTree.cc -o rbt
./rbt
```

---

## Sample Output

```
Inorder Traversal: 10 15 20 25 30
```

---

## Implementation Details

### Features Implemented

- Node structure with RED/BLACK color field
- BST-based insertion
- Left rotation
- Right rotation
- Post-insertion violation fix (`fixInsert`)
- Inorder traversal

### Rotations

**Left Rotation** — used when a right-leaning imbalance is detected.  
**Right Rotation** — used when a left-leaning imbalance is detected.

Rotations restructure the tree locally without breaking the BST ordering property.

### Insertion Fix Cases

After inserting a new RED node, the tree checks for violations and resolves them using one of three cases based on the **uncle node**:

| Case | Condition | Action |
|------|-----------|--------|
| 1 | Uncle is RED | Recolor parent, uncle → BLACK; grandparent → RED; move up |
| 2 | Uncle is BLACK, node forms a triangle | Rotate to convert to Case 3 |
| 3 | Uncle is BLACK, node forms a line | Rotate at grandparent + recolor |

---

## Time Complexity

| Operation | Complexity |
|-----------|------------|
| Insertion | O(log n)   |
| Search    | O(log n)   |
| Rotation  | O(1)       |

---

## Notes

- The root is always recolored to BLACK after every insertion
- Rotations preserve BST ordering while restoring balance
- This implementation follows the standard Cormen et al. (CLRS) Red-Black Tree algorithm


---

## Author

**Name:** Namami Verma
**Lab:** Lab 5