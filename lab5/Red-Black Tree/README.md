# Lab 5 - Red Black Tree Implementation

## Student Details

**Name:** Patel Jash  
**Roll Number:** 24bcs10632

---

## Objective

The purpose of this exercise is to build a fully functional Red-Black Tree capable of:

- Node Insertion
- Value Searching
- Sorted Inorder Traversal
- Property Validation & Integrity Checking

---

## Theoretical Background

A Red-Black Tree functions as a self-adjusting Binary Search Tree. It sustains its balanced nature by applying strict coloring constraints and executing structural rotations when necessary.

### Core Constraints

1. Every existing node is categorized as either Red or Black.
2. The tree's root node must always be Black.
3. Two Red nodes cannot be adjacent (a Red parent cannot have a Red child).
4. Any downward path from a specific node to its NULL/NIL leaves must traverse the exact same number of Black nodes.
5. All NULL/NIL pseudo-leaves are strictly Black.

By enforcing these constraints, the tree ensures that its maximum depth is limited, guaranteeing O(log n) bounds on fundamental operations.

---

## Implemented Features

### Insertion

Adds new elements and dynamically repairs any violated Red-Black constraints via strategic recoloring and left/right rotations.

### Searching

Executes a standard Binary Search Tree traversal to locate specific values quickly.

### Inorder Traversal

Recursively processes the tree to output all elements in ascending sorted order.

### Integrity Validation

Systematically verifies that no Red-Black Tree rules have been broken after modifications.

---

## Compilation Instructions

```bash
g++ -std=c++20 main.cpp -o rbtree_executable
```