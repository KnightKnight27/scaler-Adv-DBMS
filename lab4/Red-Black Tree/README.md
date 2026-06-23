# Lab 4 - Red Black Tree Implementation

## Student Details

**Name:** Jatin Chulet  
**Roll Number:** 24BCS10213

---

## Objective

To implement a Red-Black Tree and perform:

- Insertion
- Searching
- Inorder Traversal
- Validation of Red-Black Tree Properties

---

## Theory

A Red-Black Tree is a self-balancing Binary Search Tree that maintains balance using node colors and rotation operations.

### Properties

1. Every node is either Red or Black.
2. Root node is always Black.
3. Red nodes cannot have Red children.
4. Every path from a node to a NIL leaf contains the same number of Black nodes.
5. All NIL leaves are Black.

These properties ensure that the tree height remains balanced and operations execute in O(log n) time.

---

## Operations Implemented

### Insert

Inserts a new node and restores Red-Black properties using recoloring and rotations.

### Search

Performs standard Binary Search Tree lookup.

### Inorder Traversal

Displays nodes in sorted order.

### Validation

Checks whether all Red-Black Tree properties hold.

---

## Compilation

```bash
g++ -std=c++20 main.cpp -o rb_tree