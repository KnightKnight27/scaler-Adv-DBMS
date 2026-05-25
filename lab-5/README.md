# Advanced Indexing Structures: Self-Balancing Red-Black Trees & Multi-Way B-Trees

## 1. Laboratory Objectives

This laboratory workspace focuses on the comprehensive design, implementation, and analysis of two foundational self-balancing indexing structures:
1. **Red-Black Tree (RBT)**: A color-bounded self-balancing Binary Search Tree (BST) optimized for in-memory mappings (used in C++ `std::map`, Linux kernel Virtual Memory Areas).
2. **B-Tree**: A self-balancing search tree designed to support highly efficient block-storage lookups and database indexing, with a specific focus on **non-full node insertion algorithms**.

Through this workspace, we explore the pointer manipulations, color constraints, node rotation cases of Red-Black Trees, and the multi-way page-partitioning splitting algorithms of B-Trees. This serves as a vital bridge between linear dynamic storage and disk-optimized block storage filesystems.

---

## 2. Structural Architecture & Workspace Manifest

The implementation has been organized into modular components. Unlike basic implementations, this framework provides distinct classes for both structures, unified by an interactive auditing suite in the driver module.

| File | Technical Classification | Role & Implementation Details |
| :--- | :---: | :--- |
| **`rbt.hpp`** | C++ Interface Header | Defines tree structure nodes, sentinel parameters, enum variables (`NodeColor`), and class methods for the Red-Black Tree. |
| **`rbt.cpp`** | RBT Source Implementation | Houses rotation mechanics, insertion fixup algorithms (Cases 1, 2, and 3), and diagnostic tree state checkers. |
| **`btree.hpp`** | C++ Interface Header | Outlines B-Tree node layout structures (`BTreeNode`) and multi-way directory parameters (`BTree`). |
| **`btree.cpp`** | B-Tree Source Implementation | Implements the **non-full insertion search and split-child logic** required for Lab 5. |
| **`main.cpp`** | Workspace CLI Driver | Provides an interactive command-line interface (CLI) to demonstrate batch insertions, diagnostics, searches, and traversals. |
| **`CMakeLists.txt`** | Compilation Script | Standardized CMake script compiling the entire workspace under C++20 specifications. |

---

## 3. Comparative Taxonomy: Red-Black Trees vs. AVL Trees

A critical theoretical distinction exists between the two major self-balancing binary search trees (RBT and AVL). Understanding these constraints guides real-world storage selections:

> [!NOTE]
> **Strict Height constraint ($|\text{height}(L) - \text{height}(R)| \le 1$)**: This strict balancing rule is the defining property of an **AVL Tree**, not a Red-Black Tree. Red-Black trees trade absolute height-balance for cheaper insertion/deletion operations, guaranteeing a maximum path height of $2 \log_2(n + 1)$.

```
   RED-BLACK TREE PATH VARIATION LIMIT
            
            (Grandparent) [BLACK]
               /         \
         (Parent) [RED]  (Uncle) [BLACK]
           /                 \
       (z) [RED]             (Leaf) [BLACK]
       
  * Longest path (2 red, 1 black) can be up to 2x the shortest path (1 black).
```

### Deep Architectural Comparison

| Characteristic | AVL Trees | Red-Black Trees |
| :--- | :--- | :--- |
| **Balancing Property** | Rigid Height Balancing: Left and right subtrees must have height differences of $\le 1$. | Color-based Balancing: Uniform path black height; no adjacent Red nodes are permitted. |
| **Absolute Worst-Case Height** | $\approx 1.44 \log_2(n)$ (highly compacted). | $\le 2 \log_2(n + 1)$ (slightly loose). |
| **Query Performance** | Faster retrieval times due to tighter height bounds. | Slightly slower lookup times compared to AVL. |
| **Insertion Penalty** | Maximum of **2 rotations** required to restore balance. | Maximum of **2 rotations plus color shifts** (fixup Case 1/2/3). |
| **Deletion Penalty** | Up to $O(\log n)$ rotations from violation propagation. | Restores balance in at most **3 rotations**. |
| **Industrial Applications** | Read-heavy database dictionaries or lookup tables. | Write-intensive structures, VM paging systems, C++ `std::map`, Java `TreeMap`. |

---

## 4. RBT Node Rotation Mechanics

To restructure nodes during an insertion violation without disrupting the ascending key sequence of a BST, the engine performs local **left** and **right** tree rotations.

```
          Left Node Rotation on Pivot Node [X]
          
             [X] (Pivot)                    [Y] (Right Child)
            /   \                          /   \
          (A)   [Y]         =====>       [X]   (C)
               /   \                    /   \
             (B)   (C)                (A)   (B)
```

* **Left Rotation (on X)**: Shifts node $Y$ (right child of $X$) up to replace $X$. The left subtree of $Y$ (originally $B$) is reassigned as the new right child of $X$.
* **Right Rotation (on Y)**: Shifts node $X$ (left child of $Y$) up to replace $Y$. The right subtree of $X$ is reassigned as the new left child of $Y$.

---

## 5. B-Tree Multi-Way Indexing & Non-Full Node Insertion

A **B-Tree** represents a generalized self-balancing search tree tailored for secondary block storage devices. In a B-Tree, nodes can contain multiple keys (up to $2t - 1$, where $t$ is the minimum degree) and multiple children (up to $2t$).

### Crucial Lab Task: Index Search for Insertion in a Non-Full Node
During insertion, if a node is **non-full** (contains fewer than $2t - 1$ keys), we must locate the correct slot index to place the new key while maintaining the B-Tree's strict sorted order. 

The search logic uses a sliding range search. It scans from right to left starting from the end of the keys array (`n - 1`). If the node is a **leaf**, greater keys are shifted to the right to make space for the incoming key. If the node is an **interior** node, the loop identifies the appropriate child pointer range to descend into:

```
            Searching Key Slot for Key k = 25 in Node
            
            Keys Array: [ 10 | 20 | 30 | 40 ]   (Current n = 4)
                                     i
            
            1. Initialize i = n - 1 (i = 3, keys[3] = 40 > 25) -> Shift 40 right -> i = 2
            2. keys[2] = 30 > 25 -> Shift 30 right -> i = 1
            3. keys[1] = 20 < 25 -> Stop Loop!
            4. Insert key k at index i + 1 (index 2).
            
            Result Node: [ 10 | 20 | 25 | 30 | 40 ]
```

### Technical C++ Implementation of Non-Full Insertion (`btree.cpp`)
```cpp
void BTreeNode::insertNonFull(int k) {
    int i = n - 1; // Start scanning keys from right to left

    if (leaf) {
        // --- leaf Node Key Placement & Shift ---
        // Scan backwards to find the exact index position where key 'k' belongs.
        // Shift larger keys to the right to maintain strictly sorted order.
        while (i >= 0 && keys[i] > k) {
            keys[i + 1] = keys[i]; // Shift key right
            i--;
        }

        // Insert new key into the sorted slot
        keys[i + 1] = k;
        n = n + 1;
    } else {
        // --- Interior Node Descending Child Selection ---
        // Scan keys backwards to find which child index C[i] corresponds to key 'k'
        while (i >= 0 && keys[i] > k) {
            i--;
        }
        i++; // Target child is at index i+1 (offset by current i value)

        // If the target child is already full, split it
        if (C[i]->n == 2 * t - 1) {
            splitChild(i, C[i]);

            // After split, the middle key rises. We inspect which of the
            // split halves will receive the new key insertion.
            if (keys[i] < k) {
                i++;
            }
        }
        C[i]->insertNonFull(k);
    }
}
```

---

## 6. Red-Black Tree Rebalancing Mechanics

When a new node $z$ is inserted in the Red-Black Tree, it is always colored **RED**. This preserves black heights across all paths but can trigger a **Double-Red Violation** if $z$'s parent is also RED. 

To resolve these violations, the tree inspects the color of $z$'s **parent** and $z$'s **uncle** (the parent's sibling) and processes one of three balancing cases:

### Case 1: Uncle color is RED (Recoloring Only)
If the uncle is RED, the parent, uncle, and grandparent are recolored. No rotation is necessary, but the violation is pushed up to the grandparent.

```
       (Grandparent) [BLACK]                  (Grandparent) [RED]
         /         \                            /         \
   (Parent) [RED]  (Uncle) [RED]   ===>   (Parent) [BLACK] (Uncle) [BLACK]
     /                                      /
  (z) [RED]                              (z) [RED]
```
* **Resolution**: Color Parent and Uncle to **BLACK**, and Grandparent to **RED**. Update pointer $z$ to point to the Grandparent and repeat fixup loop.

---

### Case 2: Uncle color is BLACK, $z$ is a Right Child (Left Rotate Parent)
If the parent is a left child, the uncle is BLACK, and $z$ is a right child, we perform a Left Rotation around the parent to align the nodes in a straight line.

```
      (Grandparent) [BLACK]                  (Grandparent) [BLACK]
         /         \                            /         \
   (Parent) [RED]  (Uncle) [BLACK] ===>     (z) [RED]     (Uncle) [BLACK]
     \                                      /
     (z) [RED]                        (Parent) [RED]
```
* **Resolution**: Call Left Rotation on the Parent. Update pointer $z$ to point to the original parent node, transitioning the tree structure into Case 3.

---

### Case 3: Uncle color is BLACK, $z$ is a Left Child (Recolor & Right Rotate Grandparent)
If the parent is a left child, the uncle is BLACK, and $z$ is a left child, we recolor the parent to BLACK, grandparent to RED, and rotate the grandparent right.

```
       (Grandparent) [BLACK]                    (Parent) [BLACK]
          /         \                            /         \
    (Parent) [RED]  (Uncle) [BLACK] ===>      (z) [RED]  (Grandparent) [RED]
      /                                                       \
   (z) [RED]                                                  (Uncle) [BLACK]
```
* **Resolution**: Color Parent to **BLACK**, Grandparent to **RED**, and perform a Right Rotation on the Grandparent. This permanently resolves the Double-Red violation.

---

## 7. Verification & Workspace Execution Guide

The workspace includes a complete C++ compiler setup and demo executor.

### Manual Compilation
Compile the project directly using `g++` or `clang++` with C++20 support:
```bash
g++ -std=c++20 "lab 5/rbt.cpp" "lab 5/btree.cpp" "lab 5/main.cpp" -o "lab 5/indexing_demo"
```

### Running the CLI Workspace
Execute the compiled binary:
```bash
./"lab 5/indexing_demo"
```

### Interactive Demo Walkthrough
Upon startup, the program automatically populates the workspaces with standard insertion sequences:
* **RBT standard keys**: `10, 20, 30, 15, 25, 5, 1`
* **B-Tree standard keys**: `10, 20, 30, 40, 50, 6, 17, 22`

You can run diagnostics directly from the interactive tree menus (for example, option `4` in the Red-Black Tree menu) to verify balancing heights, tree structures, and AVL comparisons.


---

## 8. Conclusion

This laboratory successfully bridges the gap between self-balancing Binary Search Trees and disk-optimized Multi-Way B-Trees. Through strict color management and tree rotations, the Red-Black Tree guarantees optimal $O(\log n)$ CPU access speeds. In parallel, B-Trees utilize node splitting and sorted range search to minimize disk block access latency, serving as the core engine powering industrial databases like MySQL, PostgreSQL, and SQLite.