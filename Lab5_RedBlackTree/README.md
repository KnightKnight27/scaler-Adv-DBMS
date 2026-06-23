# Lab 4 - Red-Black Tree and B-Tree Implementation

**Name:** Abhijit P
**Roll No:** 24bcs10175

## Objective

The objective of this lab was to implement two important balanced tree data structures used in database systems:

* Red-Black Tree
* B-Tree

The implementations demonstrate how balanced trees maintain efficient search, insertion, and deletion operations while keeping tree height under control.

---

## Red-Black Tree

A Red-Black Tree is a self-balancing Binary Search Tree that maintains balance using node colors and rotations.

### Properties

1. Every node is either Red or Black.
2. The root node is always Black.
3. No two consecutive Red nodes can exist.
4. Every path from a node to its descendant NULL nodes contains the same number of Black nodes.
5. New nodes are inserted as Red.

### Operations Implemented

* Insertion
* Left Rotation
* Right Rotation
* Recoloring
* Inorder Traversal

### Time Complexity

| Operation | Complexity |
| --------- | ---------- |
| Search    | O(log n)   |
| Insert    | O(log n)   |
| Traversal | O(n)       |

---

## B-Tree

A B-Tree is a balanced multi-way search tree widely used in database systems and file systems.

Unlike Binary Search Trees, a B-Tree node can contain multiple keys and multiple children.

The implementation uses a minimum degree:

```text
t = 3
```

### Operations Implemented

* Search
* Traverse
* Insert
* Insert Non-Full
* Split Child
* Delete
* Remove From Leaf
* Remove From Non-Leaf
* Borrow From Previous Sibling
* Borrow From Next Sibling
* Merge Nodes
* Root Shrinking After Deletion

---

## B-Tree Insertion

When a node becomes full:

1. The node is split.
2. The middle key moves to the parent.
3. Two child nodes are created.
4. The tree remains balanced.

This ensures that the height of the tree grows slowly even for large numbers of keys.

---

## B-Tree Deletion

Deletion is more complex than insertion.

The implementation supports:

### Deletion from Leaf

The key is directly removed.

### Deletion from Internal Node

The key is replaced with:

* Predecessor key, or
* Successor key

depending on child availability.

### Borrowing

If a child node contains fewer than the required number of keys, a key is borrowed from a sibling.

### Merging

If borrowing is not possible, sibling nodes are merged together.

---

## Sample Operations

### Inserted Keys

```text
10 20 5 6 12 30 7 17
```

### Deleted Keys

```text
6
7
12
```

### Search Example

```text
12
```

---

## Relation to Database Systems

Balanced tree structures are widely used in database engines.

### Red-Black Tree

Used in:

* Memory indexing
* Operating system schedulers
* Internal balanced collections

### B-Tree

Used in:

* Database indexes
* File systems
* Storage engines
* Query optimization structures

Most modern relational databases use B-Tree based indexes because they minimize disk access and maintain efficient performance for large datasets.

---

## Project Structure

```text
Lab4/
│
├── README.md
│
├── red_black_tree/
│   └── red_black_tree.cpp
│
└── b_tree/
    ├── BTree.h
    └── main.cpp
```

---

## Build Instructions

### Red-Black Tree

```bash
g++ red_black_tree.cpp -o rbtree
./rbtree
```

### B-Tree

```bash
g++ main.cpp -o btree
./btree
```

---

## Complexity Analysis

### Red-Black Tree

| Operation | Complexity |
| --------- | ---------- |
| Search    | O(log n)   |
| Insert    | O(log n)   |
| Rotation  | O(1)       |

### B-Tree

| Operation | Complexity |
| --------- | ---------- |
| Search    | O(log n)   |
| Insert    | O(log n)   |
| Delete    | O(log n)   |

---

## Conclusion

This lab demonstrated the implementation of Red-Black Trees and B-Trees in C++. Both data structures maintain balanced search trees and provide efficient insertion, search, and deletion operations.

The Red-Black Tree achieves balance through coloring and rotations, while the B-Tree achieves balance through node splitting, borrowing, and merging. These structures form the foundation of many modern database indexing systems.
