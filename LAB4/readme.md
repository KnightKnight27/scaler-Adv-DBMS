# Lab 4: Red-Black Tree & Full B-Tree in C++

**Name:** Rachit S  
**Roll Number:** 24bcs10139  
**Course:** Advanced Database Management Systems (AdvDBMS)

---

## 1. Overview
Self-balancing search trees are the core data structures used for database index engines. This lab contains the C++ implementations of:
1. A **Red-Black Tree** (in-memory self-balancing Binary Search Tree) where height is guaranteed to be $O(\log N)$ using node recoloring and pointer rotations.
2. A **B-Tree** (multi-way search tree designed for disk/block storage layouts) supporting dynamic page splits, key borrowing, and node merging.

---

## 2. Invariants & Structural Constraints

### Red-Black Tree Properties
1. Every node is either `RED` or `BLACK`.
2. The root of the tree is always `BLACK`.
3. Every leaf (`NIL` sentinel node) is `BLACK`.
4. If a node is `RED`, both of its children must be `BLACK` (no consecutive red nodes on any path).
5. For each node, all simple paths from the node to descendant leaves contain the same number of black nodes (uniform Black Height).

### B-Tree Invariants (Degree $t \geq 2$)
1. **Root Bounds:** The root contains at least 1 key (if the tree is non-empty).
2. **Key count bounds:** Every node (except root) has between $t-1$ and $2t-1$ keys.
3. **Child count bounds:** An internal node with $N$ keys always has exactly $N+1$ children.
4. **Sorted Keys:** Keys inside a node are kept in sorted order.
5. **Uniform Leaf Depth:** All leaf nodes are located at the exact same depth.

---

## 3. Comparative Analysis

| Feature | Red-Black Tree | B-Tree (Degree $t$) |
| :--- | :--- | :--- |
| **Primary Target** | In-Memory (CPU Cache optimization) | On-Disk / Block Storage (I/O optimization) |
| **Branching Factor**| 2 (Binary) | Large (often matches page size, e.g., $t=512$) |
| **Height** | $H \leq 2 \log_2(N + 1)$ | $H \leq \log_t \left(\frac{N+1}{2}\right) + 1$ (Extremely flat) |
| **Memory Locality**| Poor (Node pointer chasing) | Excellent (Keys are sequential in arrays) |
| **Rebalancing Logic**| Color flips and Tree rotations | Node splitting (insert) / Borrow & Merge (delete) |

---

## 4. Compilation & Verification

To compile the integrated test runner:
```bash
g++ -std=c++17 lab4/main.cpp -o tree_test
./tree_test
```
This script programmatically checks the invariants of both trees after every write operation, ensuring no regression takes place.