# Lab 5 - B-Tree Index Implementation

## Student Information

**Name:** Patel Jash
**Roll Number:** 24bcs10632

---

# Objective

The primary objective of this experiment is to construct a B-Tree data structure and implement its core operational mechanisms, including key insertion, node searching, value deletion, and level-order traversal.

Furthermore, this exercise illustrates how a B-Tree intuitively maintains internal balance, highlighting the reasons it serves as a foundational index structure in contemporary file systems and database software.

---

# Introduction

A B-Tree is defined as a self-balancing, multi-way search tree where individual nodes have the capability to store an array of multiple keys.

Unlike typical Binary Search Trees, B-Trees feature a significantly higher branching factor. This trait directly reduces the overall vertical height of the tree, which subsequently minimizes expensive disk I/O operations. Therefore, B-Trees are the preferred choice for:

* Relational Database Management Systems (RDBMS)
* Operating System File Directories
* Persistent Storage Engines
* Block-based Indexing Architectures

---

# Core Properties of B-Trees

Assuming a B-Tree is defined with a minimum degree parameter of **T**:

1. A given node can hold a maximum of **2T − 1** elements (keys).
2. Every internal node (excluding the root) is required to contain at least **T − 1** elements.
3. If an internal node holds **k** elements, it must point to exactly **k + 1** child nodes.
4. All terminating leaf nodes must align perfectly at the same horizontal depth.
5. The structure dynamically re-balances itself following any insertion or deletion event.

---

# Implemented Operations

## 1. Key Insertion

* Newly inserted values are always placed in sorted ascending order.
* When a node hits its maximum capacity, an automatic split occurs.
* The median value from the split node is pushed upwards into its parent node.
* The overall height of the tree only increases when the root node is split.

---

## 2. Key Search

The search process mirrors the logic of standard Binary Search Trees.

Execution Flow:
1. Compare the requested key against the sequence of values in the current node.
2. If there's a match, return a success flag immediately.
3. If not found, navigate down the specific child pointer whose value range contains the target.
4. Keep iterating until the target is located or a leaf node is exhaustively checked.

---

## 3. Key Deletion

The deletion routine accommodates multiple complex scenarios:

* Erasing a value directly from a leaf node.
* Replacing a target value with its inorder predecessor.
* Replacing a target value with its inorder successor.
* Resolving underflow by borrowing elements from adjacent sibling nodes.
* Fusing two sibling nodes together if borrowing is not an option.

These actions guarantee that strict B-Tree properties are consistently upheld post-deletion.

---

## 4. Breadth-First (Level Order) Traversal

Prints the internal hierarchy of the B-Tree one level at a time.

This visualization aids in understanding:
* The current height of the tree.
* The branching fan-out behavior of nodes.
* The physical distribution of keys across the structure.

---

## 5. Inorder Traversal

Outputs all stored elements sequentially in strict sorted order, providing verification that the tree successfully maintains its logical order.

---

# Algorithmic Workflows

### Insertion Logic

1. Initiate the process at the root node.
2. Split the root immediately if it is fully packed.
3. Traverse downwards to the applicable child pointer.
4. Insert the new element into a node that has available space.
5. Re-evaluate to confirm all B-Tree rules remain unbroken.

### Search Logic

1. Evaluate the search term against the keys stored in the active node.
2. Output a success message if they match.
3. Alternatively, proceed to the relevant child branch.
4. Recursively repeat until resolved or the search concludes.

### Deletion Logic

1. Identify the exact node holding the target key.
2. If it's a leaf, perform a direct removal.
3. For internal nodes, swap the key with its logical predecessor/successor.
4. If a node drops below the minimum degree, borrow or merge to fix the underflow.
5. Continuously verify the minimum degree constraint throughout.

---

# Sample Execution

### Data Inserted:

```text
15 25 5 35 45 10 20 30 40 50 60 12 18 28
```

### Data Removed:

```text
10 35
```

### Search Executions:

```text
28
99
```

---

# Output Trace

```text
B-Tree Index Implementation
Patel Jash | 24bcs10632

Inserted Keys: 15 25 5 35 45 10 20 30 40 50 60 12 18 28 

Tree Structure By Levels:
L0: [25] 
L1: [10 15] [35 45] 
L2: [5] [12] [18 20] [28 30] [40] [50 60] 

Sorted Keys: 5 10 12 15 18 20 25 28 30 35 40 45 50 60 

Search 28 = Found
Search 99 = Not Found

Removing 10 and 35...
L0: [25] 
L1: [15] [45] 
L2: [5 12] [18 20] [28 30 40] [50 60] 
Sorted Keys After Deletion: 5 12 15 18 20 25 28 30 40 45 50 60 
```

---

# Complexity Metrics

| Operation | Time Complexity |
| --------- | --------------- |
| Search    | O(log n)        |
| Insert    | O(log n)        |
| Delete    | O(log n)        |
| Traversal | O(n)            |

---

# Industrial Applications

* Maintaining relational database indexes
* Organizing hierarchical file system directories
* Constructing robust persistent storage engines
* Developing scalable multi-level indexing frameworks
* Powering systems optimized for massive-scale data retrieval

---

# Key Benefits

* Naturally self-balancing properties
* Considerably shallower tree heights
* Optimized for infrequent, expensive disk operations
* Handles dynamic insertions and deletions gracefully
* Perfectly suited for managing huge, unstructured datasets

---

# Summary

A fully functional B-Tree was effectively implemented, encompassing key insertion, searching capabilities, deletion mechanics, and multi-mode traversals.

The results demonstrated the B-Tree's inherent capacity to sustain structural equilibrium through dynamic node splitting, sibling borrowing, and node merging. Thanks to its minimized height and highly efficient disk access logic, the B-Tree remains the gold standard in modern database architecture and enterprise storage platforms.
