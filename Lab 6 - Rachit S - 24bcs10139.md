# B-Tree C++ Implementation & Invariant Verification Report (Lab 6)

**Name:** Rachit S  
**Roll Number:** 24bcs10139  
**Course:** Advanced Database Management Systems (AdvDBMS)

---

## 1. Introduction & Database Indexing Context

In database storage engines (such as MySQL InnoDB, PostgreSQL, and SQLite), indexes are stored on disk in block-aligned structures. A standard Binary Search Tree (BST) or Red-Black Tree is poorly suited for disk storage because they have a low branching factor (2), leading to high tree height and excessive disk seek operations (I/O bottlenecks).

A **B-Tree** resolves this by being a self-balancing, multi-way search tree with a high branching factor (degree $t$). By storing many keys per node (often matching page boundaries, e.g., 4 KB), B-Trees minimize height and drastically reduce the number of disk accesses. 

In this lab, we:
1. Implemented a header-only template C++ B-Tree that stores key-value pairs (`Key` $\rightarrow$ `Value`), satisfying the requirements of a database page-map.
2. Completed a top-down, proactive splitting insertion algorithm.
3. Implemented a full B-Tree deletion (erasure) algorithm following CLRS Chapter 18 guidelines, ensuring nodes never drop below $t-1$ keys.
4. Programmed a comprehensive validation suite verifying key bounds, sorted order, child count consistency, and leaf depth alignment.

---

## 2. B-Tree Mathematical Properties & Invariants

A B-Tree of minimum degree $t \geq 2$ satisfies the following invariants:
1. **Root Bounds:** The root node has at least $1$ key if the tree is not empty.
2. **Key Count Bounds:** Every non-root node contains between $t - 1$ and $2t - 1$ keys.
3. **Child Count Bounds:** An internal node with $N$ keys always has exactly $N + 1$ children.
4. **Sorted Keys:** Within each node, keys are stored in strictly increasing order: $K_0 < K_1 < \dots < K_{N-1}$.
5. **Key Separation:** For any key $K_i$ in node $X$, the keys in the $i$-th child of $X$ are less than $K_i$, and the keys in the $(i+1)$-th child of $X$ are greater than $K_i$.
6. **Depth Invariant:** All leaf nodes exist at the exact same depth.

---

## 3. Algorithm Operations

### A. Proactive Splitting on Insertion
In a standard bottom-up insertion, a split propagates upward when a node overflows. This requires keeping track of the search path or maintaining parent pointers, which can cause recursive backtracking splits.

Our implementation uses **Proactive Splitting**:
- As we descend the tree looking for the insertion slot, if we visit any node that is currently full ($2t-1$ keys), we split it **immediately** before moving further down.
- This guarantees that the parent node of our current search target is never full, ensuring a split never has to propagate backward up the tree.

### B. Deletion (Erase)
Deletion follows the CLRS B-Tree deletion algorithm, which rebalances the tree on the descend phase to ensure the node we are visiting always has at least $t$ keys (except the root):
- **Case 1 (Leaf Hit):** If the key is in a leaf node, simply delete it.
- **Case 2 (Internal Hit):** If the key is in an internal node:
  - **Case 2a:** If the left sibling has $\geq t$ keys, find the predecessor key, delete it, and copy it to the current node.
  - **Case 2b:** If the right sibling has $\geq t$ keys, find the successor key, delete it, and copy it to the current node.
  - **Case 2c:** If both siblings have $t-1$ keys, merge them along with the parent's key into a single node of size $2t-1$, then delete the key from the merged child.
- **Case 3 (Descend Fix):** If the key is not present in the current node, we must descend. Before entering a child, if it has only $t-1$ keys:
  - **Case 3a:** Borrow a key from its immediate left or right sibling (performed via parent rotations).
  - **Case 3b:** If both siblings have $t-1$ keys, merge the child with one sibling and drop a key from the parent.

---

## 4. Complexity Analysis

| Operation | Time Complexity (Average) | Time Complexity (Worst Case) | Space Complexity |
| :--- | :--- | :--- | :--- |
| **Search** | $O(\log_t N)$ | $O(\log_t N)$ | $O(\log_t N)$ |
| **Insertion** | $O(\log_t N)$ | $O(\log_t N)$ | $O(\log_t N)$ |
| **Deletion** | $O(\log_t N)$ | $O(\log_t N)$ | $O(\log_t N)$ |

*Here, $t$ is the minimum degree of the tree. The height of the B-Tree is bounded by $H \leq \log_t \left(\frac{N+1}{2}\right) + 1$.*

---

## 5. Test Suite Verification & Logs

The driver program `main.cpp` runs 5 comprehensive tests, including lookups, deletions, and a 5,000-operation randomized stress test against `std::map` (as an oracle). Below are the output logs:

```text
==================================================
  TEST 1: SMALL B-TREE (t=3) INSERTIONS
==================================================
Inserting Key: 41 -> Inception
Inserting Key: 17 -> Whiplash
Inserting Key: 76 -> Parasite
Inserting Key: 9 -> Memento
Inserting Key: 23 -> Dune
Inserting Key: 58 -> Oppenheimer
Inserting Key: 88 -> Joker
Inserting Key: 3 -> La La Land
Inserting Key: 12 -> Spirited Away
Inserting Key: 30 -> Goodfellas

Tree Size: 10
Tree Structure:
[23]
    [3 9 12 17] (leaf)
    [30 41 58 76 88] (leaf)

In-Order Traversal:
  3 : La La Land
  9 : Memento
  12 : Spirited Away
  17 : Whiplash
  23 : Dune
  30 : Goodfellas
  41 : Inception
  58 : Oppenheimer
  76 : Parasite
  88 : Joker

==================================================
  TEST 2: SEARCH & OVERWRITE OPERATIONS
==================================================
Contains(17) -> YES
Contains(23) -> YES
Contains(99) -> NO

Overwriting Key 58...
Value at 58: Oppenheimer (IMAX Edition) | Size: 10
Lookup of non-existent key 999 threw as expected: BTree::at - key not found

==================================================
  TEST 3: ERASURE & ROOT COLLAPSE
==================================================
Erasing Key: 3 ... SUCCESS | Current Size: 9
Erasing Key: 17 ... SUCCESS | Current Size: 8
Erasing Key: 41 ... SUCCESS | Current Size: 7
Erasing Key: 88 ... SUCCESS | Current Size: 6
Erasing Key: 23 ... SUCCESS | Current Size: 5

Tree Structure After Erasures:
[30]
    [9 12] (leaf)
    [58 76] (leaf)

==================================================
  TEST 4: SEQUENTIAL INSERTION (t=2, 2-3-4 tree)
==================================================
[4 8]
    [2]
        [1] (leaf)
        [3] (leaf)
    [6]
        [5] (leaf)
        [7] (leaf)
    [10 12 14]
        [9] (leaf)
        [11] (leaf)
        [13] (leaf)
        [15 16] (leaf)

==================================================
  TEST 5: LARGE-SCALE STRESS TEST AGAINST STL MAP ORACLE
==================================================
Running 5,000 random operations (insert/erase) on degree t=4 B-Tree...
Stress Test Complete. Tree size: 194 elements.
[VERIFICATION SUCCESS] In-order traversals of B-Tree and std::map match exactly.

==================================================
  ALL B-TREE IMPLEMENTATION TESTS PASSED!
==================================================
```

---

## 6. Compilation & Execution Instructions

### A. Direct MSVC Compilation (Windows)
Using a Visual Studio Developer Command Prompt:
```cmd
cd lab6
cl /EHsc /std:c++17 main.cpp /out:btree.exe
.\btree.exe
```

### B. Standard G++ Compilation (Linux/macOS)
```bash
cd lab6
g++ -std=c++17 main.cpp -o btree
./btree
```

### C. CMake Compilation
```bash
cd lab6
mkdir build
cd build
cmake ..
cmake --build .
# Run binary
./btree
```
