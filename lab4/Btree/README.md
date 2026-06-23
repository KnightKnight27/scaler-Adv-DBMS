# Lab 6 - B-Tree Index Implementation

## Student Information

**Name:** Jatin Chulet
**Roll Number:** 24BCS10213

---

# Objective

The objective of this experiment is to implement a B-Tree data structure and perform various operations such as insertion, searching, deletion, and traversal.

The experiment also demonstrates how B-Trees maintain balance automatically and why they are widely used in database indexing and file systems.

---

# Introduction

A B-Tree is a self-balancing multi-way search tree that stores multiple keys in a single node.

Unlike Binary Search Trees, a B-Tree has a high branching factor, which reduces tree height and minimizes disk accesses. Due to these properties, B-Trees are commonly used in:

* Database Management Systems
* File Systems
* Storage Engines
* Indexing Structures

---

# Properties of B-Tree

For a B-Tree with minimum degree **t**:

1. Every node can contain at most **2t − 1 keys**.
2. Every non-root node contains at least **t − 1 keys**.
3. A node containing **k keys** has **k + 1 children**.
4. All leaf nodes exist at the same depth.
5. The tree remains balanced after every insertion and deletion.

---

# Operations Implemented

## 1. Insertion

* New keys are inserted in sorted order.
* If a node becomes full, it is split.
* The middle key is promoted to the parent node.
* Tree height increases only when the root is split.

---

## 2. Search

The search operation works similarly to Binary Search Trees.

Steps:

1. Compare the key with values inside the current node.
2. If found, return success.
3. Otherwise move to the appropriate child node.
4. Continue until the key is found or a leaf node is reached.

---

## 3. Deletion

Deletion handles several cases:

* Removing from a leaf node.
* Replacing with predecessor.
* Replacing with successor.
* Borrowing keys from siblings.
* Merging sibling nodes when necessary.

These operations ensure that B-Tree properties remain valid after deletion.

---

## 4. Level Order Traversal

Displays the structure of the B-Tree level by level.

This helps visualize:

* Tree height
* Node fan-out
* Distribution of keys

---

## 5. Inorder Traversal

Prints keys in sorted order.

This verifies that the B-Tree correctly maintains key ordering.

---

# Algorithm

### Insertion

1. Start from the root.
2. If root is full, split it.
3. Traverse to the correct child.
4. Insert key into a non-full node.
5. Maintain B-Tree properties.

### Search

1. Compare target key with keys in current node.
2. If found, return success.
3. Otherwise move to the correct child.
4. Repeat until key is found or search fails.

### Deletion

1. Locate the key.
2. Remove directly if in leaf.
3. Use predecessor or successor if key exists in internal node.
4. Borrow or merge nodes when required.
5. Maintain minimum degree constraints.

---

# Sample Input

Inserted Keys:

```text
15 25 5 35 45 10 20 30 40 50 60 12 18 28
```

Keys Removed:

```text
10 35
```

Search Queries:

```text
28
99
```

---

# Sample Output

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

# Time Complexity

| Operation | Complexity |
| --------- | ---------- |
| Search    | O(log n)   |
| Insert    | O(log n)   |
| Delete    | O(log n)   |
| Traversal | O(n)       |

---

# Applications of B-Trees

* Database indexing
* File system directories
* Storage engines
* Multi-level indexing
* Large-scale data retrieval systems

---

# Advantages

* Self-balancing structure
* Reduced tree height
* Efficient disk access
* Supports dynamic insertions and deletions
* Suitable for large datasets

---

# Conclusion

The B-Tree was successfully implemented with insertion, searching, deletion, and traversal operations.

The experiment demonstrated how B-Trees maintain balance through node splitting, borrowing, and merging operations. Due to their low height and efficient disk access patterns, B-Trees are widely used in modern database systems and storage engines.
