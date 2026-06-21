<div align="center">

# 🌳 Lab Session 4: Red-Black Tree & Full B-Tree in C++
### Implementing Self-Balancing In-Memory Trees & Page-Aligned On-Disk Database Indices

[![C++](https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://www.kernel.org/)

</div>

---

## 👨‍🎓 Student Details
- **Name:** Siddhant Prasad
- **Roll Number:** 24BCS10255

---

## 🎯 Objective
Implement a Red-Black Tree (a self-balancing binary search tree used for in-memory indexes) and a full B-Tree (the actual on-disk index structure used by databases like PostgreSQL, MySQL, and SQLite) supporting insert, merge (split promotion), search, and delete with underflow merging.

---

## 🔴 Part 1: Red-Black Tree

A Red-Black Tree is a self-balancing binary search tree that maintains $O(\log n)$ height by satisfying four structural invariants:
1. **Node Color**: Every node is either `RED` or `BLACK`.
2. **Root Rule**: The root of the tree is always `BLACK`.
3. **No Red-Red Violations**: No two consecutive red nodes are allowed on any path (a red node's parent must be black).
4. **Black Height Invariant**: Every path from a node to its NULL descendants contains the same number of black nodes.

### Implementation Details
The implementation is located in [rbt.cpp](file:///c:/Users/Siddhant/OneDrive/Desktop/scaler-Adv-DBMS/Lab_4/rbt.cpp). It features:
- **Left and Right Rotations**: Pointers are swapped to rebalance subtrees locally.
- **`fix_insert`**: Handles node insertions. Fixes double-red conflicts using recoloring (Case 1) or rotations (Cases 2 & 3).
- **`fix_delete`**: Restores the black-height invariant when black nodes are removed.

### Compile and Run Red-Black Tree
```bash
g++ -std=c++17 rbt.cpp -o rbt
./rbt
```

---

## 🌳 Part 2: Full B-Tree (Order $t$)

A B-Tree is a self-balancing search tree designed to optimize read and write operations on block storage devices. 
- **Capacities**:
  - Every internal node holds between $t-1$ and $2t-1$ keys and has between $t$ and $2t$ child pointers.
  - Sibling nodes are balanced dynamically through splits and merges.
- **Operations**:
  - **Proactive Split**: When traversing downward, any full node ($2t-1$ keys) is split proactively. The median key is promoted to the parent.
  - **Borrow & Merge**: During deletion, if a child contains fewer than $t$ keys, it borrows a key from its sibling (via the parent) or merges with its sibling, preventing underflow.

### Implementation Details
The implementation is located in [btree.cpp](file:///c:/Users/Siddhant/OneDrive/Desktop/scaler-Adv-DBMS/Lab_4/btree.cpp).
It supports:
- **`split_child`**: Splitting a full child node and promoting the median key.
- **`insert_non_full`**: Recurse down the tree to insert a key, splitting full nodes along the path.
- **`delete_key`**: Implements predecessor/successor swaps, sibling borrowing, and merging.

### Compile and Run B-Tree
```bash
g++ -std=c++17 btree.cpp -o btree
./btree
```

---

## ⚖️ Red-Black Tree vs. B-Tree

| Property / Dimension | 🔴 Red-Black Tree | 🌳 B-Tree (Order $t$) |
| :--- | :--- | :--- |
| **Primary Storage Target** | Volatile RAM (In-memory). | Persistent Disk (On-disk). |
| **Node Size & Fanout** | 1 key per node (Binary branching factor = 2). | Multi-key nodes (Up to $2t-1$ keys and $2t$ children). |
| **Tree Height** | Tall: $H \approx 2 \log_2(n + 1)$ | Short: $H \approx \log_t(n)$ — much flatter for large $t$. |
| **Database Usage** | In-memory indexes, `std::map`, transaction cache. | Persistent tables/indexes (PostgreSQL, SQLite, MySQL InnoDB). |
| **Cache Friendliness** | Poor (pointer chasing causes frequent CPU cache misses). | Excellent (sequential keys are stored sequentially in a single node). |
| **Restructuring Mechanism**| Color flips and rotations. | Sibling borrows, splits, promotions, and merges. |

---

## 🏁 Key Takeaways
- **Rotations vs. Splits**: Red-Black Trees maintain balance through simple pointer rotations and color changes, which are ideal for low-overhead in-memory structures. B-Trees use node splitting and merging to prevent disk-write fragmentation.
- **Disk I/O Minimization**: B-Trees minimize disk seeks by matching the node size to the filesystem page size. For example, PostgreSQL's index pages are `8 KB` by default, allowing a single disk seek to read a B-Tree node containing hundreds of sorted keys.
- **Sequential Locality**: The dense multi-key packing of B-Tree nodes makes search operations cache-friendly, avoiding pointer-chasing latency.
- **B-Tree Deletion Complexity**: Deleting from a B-Tree requires proactive checks during descent. If a child node has fewer than $t$ keys, it must borrow from a sibling or merge with it to preserve the lower-bound properties.
