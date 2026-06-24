# LAB 4: Tree Implementations — Red-Black Tree & B-Tree

## Overview
This lab implements two foundational balanced trees in C++:
1. **Red-Black Tree**: A binary search tree with self-balancing properties guaranteed by node coloring (Red/Black) and rotation rules.
2. **B-Tree**: A self-balancing search tree generalisation that allows nodes to have more than two children, optimized for block storage and database indexing.

---

## Files

| File | Purpose |
|------|---------|
| `RedBlackTree.cpp` | C++ implementation of a Red-Black Tree with insertion, deletion, balancing fixes, and inorder traversal. |
| `BTree.cpp` | C++ implementation of a B-Tree supporting insert, split, borrow, merge, delete, search, and inorder traversal. |

---

## 1. Red-Black Tree Implementation Details
* **Balancing Rules**:
  * Every node is either Red or Black.
  * The root and leaf nodes (NIL) are Black.
  * If a node is Red, then both its children are Black (no two adjacent red nodes).
  * Simple paths from node to descendant leaves contain the same number of black nodes.
* **Insertion**: Done as a standard BST insertion with the new node colored Red. It triggers recoloring and rotations if any Red-Red conflicts are introduced (`fix_insert`).
* **Deletion**: Replaces the deleted node using a standard BST approach and fixes black-height violations by adjusting color distributions and executing rotations (`fix_delete`).

---

## 2. B-Tree Implementation Details
* **Properties**:
  * Node capacity is bounded by the minimum degree $T$ (where $T \geq 2$).
  * Interior nodes have between $T$ and $2T$ children.
  * Leaf nodes are all at the same level.
* **Split**: When a child node is full ($2T - 1$ keys), it splits into two nodes of size $T-1$ with the median key promoted to the parent.
* **Borrow & Merge**: Deleting keys from a node requires maintaining the $T-1$ key minimum constraint.
  * If a sibling has at least $T$ keys, a key is **borrowed**.
  * If sibling nodes have $T-1$ keys, they are **merged** along with their separating parent key.

---

## 3. Compilation & Execution

To compile and run the Red-Black Tree:
```bash
g++ -std=c++17 -o rb_tree RedBlackTree.cpp
./rb_tree
```

To compile and run the B-Tree:
```bash
g++ -std=c++17 -o b_tree BTree.cpp
./b_tree
```
