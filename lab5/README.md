# Lab 5 — Red-Black Tree Implementation in C++

## Overview

This lab implements a **Red-Black Tree (RBT)**, a self-balancing binary search tree where each node carries an extra bit representing its color (red or black). The color constraints ensure that the tree remains approximately balanced after insertions, guaranteeing **O(log n)** time complexity for search and insert operations.

Red-Black Trees are used extensively in practice — for example, the Linux kernel's CFS scheduler uses one to manage process scheduling, and `std::map` / `std::set` in the C++ STL are typically backed by an RBT internally.

---

## Red-Black Tree Properties

A valid Red-Black Tree must satisfy these invariants at all times:

| # | Property |
|---|----------|
| 1 | Every node is colored either **red** or **black** |
| 2 | The **root** is always **black** |
| 3 | No two adjacent (parent-child) nodes can both be **red** |
| 4 | Every path from the root to a NULL leaf contains the **same number of black nodes** (black-height) |
| 5 | All NULL (empty) leaves are considered **black** |

These properties together ensure that the longest path from root to leaf is at most **twice** the length of the shortest path, keeping the tree balanced.

---

## How Insertion Works

Inserting into an RBT is a two-phase process:

### Phase 1 — Standard BST Insert
We walk down the tree comparing keys, and place the new node at the appropriate leaf position. The new node is always colored **red** initially (since adding a red node doesn't violate the black-height property).

### Phase 2 — Fix Violations
After inserting a red node, we might violate property 3 (red-red parent-child). We fix this by walking up the tree and applying one of three cases:

- **Case 1 (Uncle is red):** Recolor the parent and uncle to black, grandparent to red, and move the problem up the tree.
- **Case 2 (Uncle is black, triangle shape):** Rotate the parent to convert into Case 3.
- **Case 3 (Uncle is black, line shape):** Rotate the grandparent and recolor to restore balance.

Finally, the root is always forced to black.

---

## Compilation & Running

```bash
cd lab5
g++ -o rbtree rbtree.cpp
./rbtree
```

### Expected Output

```
Inserting keys: 10, 20, 30, 15, 5

In-order traversal: 5(R) 10(B) 15(R) 20(B) 30(R) 
Black-height valid: Yes

Search 15: Found
Search 99: Not Found
```

The in-order traversal prints keys in sorted order along with their color (`R` = Red, `B` = Black), confirming the tree is correctly sorted and colored.

---

## Code Structure

```
lab5/
├── rbtree.cpp    # Complete RBT implementation (insert, search, verify)
└── README.md     # This file
```

### Key Components

| Component | Description |
|-----------|-------------|
| `RBNode` struct | Stores the key, color, and pointers to left/right/parent |
| `leftRotate()` / `rightRotate()` | Rotation operations that restructure the tree locally while preserving BST ordering |
| `balanceAfterInsert()` | Walks up from the newly inserted node and fixes any red-red violations using recoloring and rotations |
| `insert()` | Standard BST insert followed by violation fixing |
| `search()` | Iterative BST lookup — O(log n) |
| `verifyBlackHeight()` | Recursively checks that all root-to-leaf paths have equal black-height |

---

## Time Complexity

| Operation | Complexity |
|-----------|------------|
| Insert    | O(log n)   |
| Search    | O(log n)   |
| Rotation  | O(1)       |

The balancing fix after insertion requires at most **O(log n)** recolorings and at most **2 rotations**, making the overall insert cost O(log n).
