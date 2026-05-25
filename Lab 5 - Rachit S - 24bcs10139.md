# Red-Black Tree C++ Implementation & Verification Report (Lab 5)

**Name:** Rachit S  
**Roll Number:** 24bcs10139  
**Course:** Advanced Database Management Systems (AdvDBMS)

---

## 1. Problem Statement & Objectives

A **Red-Black Tree (RBT)** is a self-balancing binary search tree (BST) where each node contains an extra bit representing its color (`RED` or `BLACK`). It is used to ensure that the tree remains balanced during insertions and deletions, preventing the worst-case $O(N)$ operations of a standard BST and guaranteeing $O(\log N)$ time complexity for search, insertion, and deletion.

In database index systems (such as in-memory databases like Redis or directory mappings), self-balancing trees are essential for efficient index updates.

The objective of this lab is to:
1. Implement a complete, template-based Red-Black Tree in C++ supporting insertion, deletion, searching, traversal, and visual printing.
2. Develop a programmatic verification module that enforces the five RBT properties after every modification.
3. Verify the execution using step-by-step test sequences (insertions, search hits/misses, and complex node deletions).

---

## 2. Red-Black Tree Properties & Balancing Logic

A binary search tree is a valid Red-Black Tree if it satisfies the following five invariants:
1. **Property 1:** Every node is either `RED` or `BLACK`.
2. **Property 2:** The root of the tree is always `BLACK`.
3. **Property 3:** Every leaf node (represented by a sentinel `NIL` node) is `BLACK`.
4. **Property 4:** If a node is `RED`, then both of its children must be `BLACK`. (No two consecutive `RED` nodes are allowed on any path).
5. **Property 5:** For each node, all simple paths from that node to descendant leaves (`NIL`) contain the same number of black nodes (known as the **Black Height** of the node).

### Pointer Rotations
Rotations are local operations that modify the tree structure while preserving the binary search tree property (in-order traversal remains sorted). 

```text
       Left Rotate (on x)                  Right Rotate (on y)
       
           (y)                                     (x)
          /   \     [Left Rotate]                 /   \
        (x)    C    <------------               A     (y)
       /   \                                         /   \
      A     B       ------------>                   B     C
                    [Right Rotate]
```

- **Left Rotation:** Moves node `y` (right child of `x`) to the parent position, while `x` becomes the left child of `y`, and the left child of `y` (`B`) is reassigned as the right child of `x`.
- **Right Rotation:** Moves node `x` (left child of `y`) to the parent position, while `y` becomes the right child of `x`, and the right child of `x` (`B`) is reassigned as the left child of `y`.

---

## 3. Complexity Analysis

| Operation | Time Complexity (Average) | Time Complexity (Worst Case) | Space Complexity |
| :--- | :--- | :--- | :--- |
| **Search** | $O(\log N)$ | $O(\log N)$ | $O(1)$ |
| **Insertion** | $O(\log N)$ | $O(\log N)$ | $O(1)$ (auxiliary rotations) |
| **Deletion** | $O(\log N)$ | $O(\log N)$ | $O(1)$ (auxiliary rotations) |
| **Space Complexity** | $O(N)$ | $O(N)$ | $O(N)$ |

*Note: Since the height $H$ of a Red-Black Tree with $N$ nodes is mathematically guaranteed to be at most $2 \log_2(N + 1)$, all path-based search and structural operations run in strictly logarithmic time.*

---

## 4. Implementation Architecture

Our implementation is divided into two primary C++ source files:

1. **`RedBlackTree.hpp`**: Contains the class template definition and implementation:
   - A shared static `NIL` sentinel node representing leaf boundaries (minimizing memory overhead).
   - Core structural functions: `leftRotate()`, `rightRotate()`, `transplant()`, and `minimum()`.
   - Balanced Modification: `insert()` with `insertFixup()`, and `remove()` with `deleteFixup()`.
   - Verification Engine: `verifyRBTProperties()` programmatically validates the tree invariants by traversing the active tree and checking root coloring, parent-child red collisions, and path-wise black-height consistency.
   - Visualization: `printTree()` renders a hierarchical ASCII tree structure in the terminal using ANSI colors.

2. **`main.cpp`**: The test harness that performs:
   - Successive insertions to test balancing via recoloring and single/double rotations.
   - Programmatic verification checks after every change.
   - Node lookups (searches).
   - Multi-case node deletions (deleting leaf nodes, nodes with single children, nodes with double children, and the root node).
   - Re-insertion testing to check dynamic regrowth.

---

## 5. Verification Results & Output

Running the compiled driver program yields the following trace, demonstrating correct tree balancing and successful programmatic property checks at each stage:

```text
==================================================
          RED-BLACK TREE TEST SUITE
==================================================

--- Inserting Elements step-by-step ---
Inserting Key: 15 (Fifteen)
Inserting Key: 10 (Ten)
...
Inserting Key: 30 (Thirty)

--- Final Tree Structure ---
└── 15 (B): Fifteen
    ├── 10 (R): Ten
    │   ├── 5 (B): Five
    │   │   ├── 3 (R): Three
    │   │   └── 8 (R): Eight
    │   └── 12 (B): Twelve
    │       ├── 11 (R): Eleven
    │       └── 14 (R): Fourteen
    └── 20 (R): Twenty
        ├── 18 (B): Eighteen
        │   ├── 16 (R): Sixteen
        │   └── 19 (R): Nineteen
        └── 25 (B): Twenty-five
            ├── 22 (R): Twenty-two
            └── 30 (R): Thirty

--- In-Order Traversal (Sorted order) ---
3:Three 5:Five 8:Eight 10:Ten 11:Eleven 12:Twelve 14:Fourteen 15:Fifteen 16:Sixteen 18:Eighteen 19:Nineteen 20:Twenty 22:Twenty-two 25:Twenty-five 30:Thirty 
[VERIFICATION] SUCCESS: All Red-Black Tree properties successfully verified.

--- Search Operations ---
Key 12 found! Value: Twelve
Key 15 found! Value: Fifteen
Key 30 found! Value: Thirty
Key 99 NOT found!

--- Deleting Elements step-by-step ---

Deleting Key: 3
Key 3 deleted successfully.
└── 15 (B): Fifteen
    ├── 10 (R): Ten
    │   ├── 5 (B): Five
    │   │   └── 8 (R): Eight
    │   └── 12 (B): Twelve
    ...
[VERIFICATION] SUCCESS: All Red-Black Tree properties successfully verified.

Deleting Key: 12
Key 12 deleted successfully.
...
[VERIFICATION] SUCCESS: All Red-Black Tree properties successfully verified.

Deleting Key: 20
Key 20 deleted successfully.
...
[VERIFICATION] SUCCESS: All Red-Black Tree properties successfully verified.

Deleting Key: 15 (Root node)
Key 15 deleted successfully.
└── 16 (B): Sixteen
    ├── 10 (R): Ten
    │   ├── 5 (B): Five
    │   │   └── 8 (R): Eight
    │   └── 14 (B): Fourteen
    │       ├── 11 (R): Eleven
    └── 22 (R): Twenty-two
        ├── 18 (B): Eighteen
        │   └── 19 (R): Nineteen
        └── 25 (B): Twenty-five
            └── 30 (R): Thirty
[VERIFICATION] SUCCESS: All Red-Black Tree properties successfully verified.

--- Inserting values back to verify regrowth ---
└── 16 (B): Sixteen
    ├── 10 (R): Ten
    │   ├── 5 (B): Five
    │   │   ├── 3 (R): Three (Re-inserted)
    │   │   └── 8 (R): Eight
    │   └── 14 (B): Fourteen
    │       ├── 11 (R): Eleven
    │       └── 15 (R): Fifteen (Re-inserted)
    └── 22 (R): Twenty-two
        ├── 18 (B): Eighteen
        │   └── 19 (R): Nineteen
        └── 25 (B): Twenty-five
            └── 30 (R): Thirty
[VERIFICATION] SUCCESS: All Red-Black Tree properties successfully verified.

==================================================
    ALL RED-BLACK TREE TESTS COMPLETED SUCCESSFULLY!
==================================================
```

---

## 6. How to Compile & Run

### Method A: Direct Compilation (G++ / clang++)
Navigate to the `red_black_tree` directory and run:
```bash
g++ -std=c++17 main.cpp -o rbt
./rbt
```

### Method B: CMake Build
```bash
mkdir build
cd build
cmake ..
cmake --build .
# Run the generated binary
./red_black_tree
```

### Method C: MSVC Compilation (Windows)
Using a Visual Studio Developer Command Prompt:
```cmd
cl /EHsc /std:c++17 main.cpp
main.exe
```
