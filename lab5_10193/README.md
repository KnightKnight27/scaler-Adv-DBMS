# Lab 5 - Red Black Tree Implementation

## Student Details
- **Name:** Anushka
- **Roll Number:** 10193

---

## Objective
To implement a Red-Black Tree in C++ with:

- Insert operation
- Search operation
- Left and Right Rotations
- Fix-up procedure after insertion
- Validation of all Red-Black Tree properties

---

## Features Implemented

### 1. Insert Operation
Adds nodes while maintaining BST property.

### 2. Search Operation
Searches for a key in O(log n).

### 3. Rotations
- Left Rotation
- Right Rotation

Used to rebalance the tree.

### 4. Fix-Up Procedure
Handles all insertion violation cases:

- Case 1: Uncle is Red
- Case 2: Triangle formation
- Case 3: Line formation

### 5. Validation Function
Checks:

- Root is black
- No consecutive red nodes
- Equal black height
- BST ordering

---

## Red-Black Tree Properties

1. Every node is either Red or Black
2. Root is always Black
3. Null leaves are Black
4. Red nodes cannot have Red children
5. Every path has same black height

---

## Sample Test Data

Inserted Values:

41, 38, 31, 12, 19, 8, 25, 50, 60, 55

---

## Expected Output

Search 25: 1  
Search 99: 0  
Tree Valid: 1

---

## Compilation

```bash
g++ rbtree.cpp -o rbtree
./rbtree
```

---

## Learning Outcome

This lab helped in understanding:

- Self-balancing binary search trees
- Tree rotations
- Red-Black balancing logic
- Validation of tree invariants