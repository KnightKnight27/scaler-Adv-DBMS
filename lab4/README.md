# Lab 4 — Tree-Based Index Structures (Red-Black Tree & B-Tree)

## Student Information

**Name:** Jatin Chulet
**Roll Number:** 24BCS10213

---

# Objective

The objective of this laboratory is to implement two important balanced tree data structures used in database systems and indexing:

1. **Red-Black Tree (RBT)** — a self-balancing Binary Search Tree.
2. **B-Tree** — a multi-way balanced search tree widely used in databases and file systems.

The experiment demonstrates insertion, searching, traversal, balancing mechanisms, and validation of structural properties.

---

# Introduction

Efficient indexing structures are essential for fast data retrieval in database systems.

Two widely used balanced tree structures are:

### Red-Black Tree

A Red-Black Tree is a self-balancing Binary Search Tree that maintains balance using node colors and rotation operations.

It provides:

* O(log n) insertion
* O(log n) search
* O(log n) deletion

and is commonly used in in-memory indexing structures.

### B-Tree

A B-Tree is a self-balancing multi-way search tree that stores multiple keys within a node.

Due to its high branching factor, the tree height remains small, reducing disk accesses and making it ideal for:

* Database indexing
* File systems
* Storage engines
* Disk-based search structures

---

# Part A — Red-Black Tree Implementation

## Theory

A Red-Black Tree maintains balance through coloring rules.

### Properties

1. Every node is either Red or Black.
2. Root node is always Black.
3. Red nodes cannot have Red children.
4. Every path from a node to a NIL leaf contains the same number of Black nodes.
5. All NIL leaves are Black.

These properties guarantee logarithmic height.

---

## Operations Implemented

### Insert

* Inserts a new key.
* Performs recoloring when necessary.
* Applies left and right rotations to restore balance.

### Search

* Standard Binary Search Tree lookup.

### Inorder Traversal

* Prints keys in sorted order.

### Validation

* Verifies all Red-Black Tree properties.
* Confirms tree correctness after insertion operations.

---

## Red-Black Tree Time Complexity

| Operation | Complexity |
| --------- | ---------- |
| Search    | O(log n)   |
| Insert    | O(log n)   |
| Delete    | O(log n)   |
| Traversal | O(n)       |

---

# Part B — B-Tree Index Implementation

## Theory

A B-Tree is a self-balancing multi-way search tree optimized for storage systems.

Unlike Binary Search Trees, each node can contain multiple keys and multiple children.

---

## Properties of B-Tree

For minimum degree **t**:

1. Every node contains at most **2t − 1 keys**.
2. Every non-root node contains at least **t − 1 keys**.
3. A node containing **k keys** has **k + 1 children**.
4. All leaf nodes exist at the same level.
5. The tree remains balanced after insertion and deletion.

---

## Operations Implemented

### Insertion

* Inserts keys in sorted order.
* Splits nodes when they become full.
* Promotes median key to parent.
* Maintains balance automatically.

### Search

* Searches within node keys.
* Traverses to the appropriate child.
* Continues until key is found or search fails.

### Deletion

Handles:

* Leaf deletion
* Internal node deletion
* Borrowing from siblings
* Merging nodes
* Successor replacement
* Predecessor replacement

### Level Order Traversal

Displays tree structure level by level.

### Inorder Traversal

Prints keys in sorted order.

---

## B-Tree Algorithms

### Insertion

1. Start from root.
2. Split root if full.
3. Move to appropriate child.
4. Insert into non-full node.
5. Preserve B-Tree properties.

### Search

1. Compare key with node contents.
2. Return success if found.
3. Otherwise move to correct child.
4. Continue recursively.

### Deletion

1. Locate key.
2. Delete directly if leaf.
3. Replace with predecessor or successor when required.
4. Borrow or merge nodes if underflow occurs.
5. Maintain minimum degree constraints.

---

## Sample Input

### Inserted Keys

```text
15 25 5 35 45 10 20 30 40 50 60 12 18 28
```

### Keys Removed

```text
10 35
```

### Search Queries

```text
28
99
```

---

## Sample Output

```text
B-Tree Index Implementation
Jatin Chulet | 24BCS10213

Inserted Keys:
15 25 5 35 45 10 20 30 40 50 60 12 18 28

Tree Structure By Levels:
L0: [25]
L1: [10 15] [35 45]
L2: [5] [12] [18 20] [28 30] [40] [50 60]

Sorted Keys:
5 10 12 15 18 20 25 28 30 35 40 45 50 60

Search 28 = Found
Search 99 = Not Found

Removing 10 and 35...

Sorted Keys After Deletion:
5 12 15 18 20 25 28 30 40 45 50 60
```

---

## B-Tree Time Complexity

| Operation | Complexity |
| --------- | ---------- |
| Search    | O(log n)   |
| Insert    | O(log n)   |
| Delete    | O(log n)   |
| Traversal | O(n)       |

---

# Applications

## Red-Black Tree

* STL map and set implementations
* Memory indexing
* Scheduling systems
* Ordered dictionaries

## B-Tree

* Database indexing
* File systems
* Storage engines
* Large-scale search systems
* Multi-level indexing

---

# Advantages

## Red-Black Tree

* Guaranteed logarithmic height
* Efficient updates
* Fast search operations
* Suitable for in-memory structures

## B-Tree

* Reduced tree height
* Efficient disk access
* High branching factor
* Ideal for large datasets
* Supports dynamic updates

---

# Conclusion

This laboratory successfully implemented two important balanced indexing structures: the Red-Black Tree and the B-Tree.

The Red-Black Tree demonstrated balancing through rotations and recoloring, while the B-Tree maintained balance through node splitting, borrowing, and merging operations.

Both structures provide efficient O(log n) search and update operations and form the foundation of modern database indexing and storage systems.
