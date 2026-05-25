# Lab 5 - Red-Black Tree Implementation

## Objective

The objective of this lab is to implement a **Red-Black Tree (RBT)** from scratch in C++. This assignment serves as a foundation for understanding tree structures, balancing constraints, and node rotation mechanics, which are critical precursors to implementing advanced database storage structures like **B-Trees** and **B+ Trees**.

---

## Files Submitted

| File | Description |
|------|-------------|
| [rbt.hpp](file:///Users/kushalsacharya/Desktop/scaler-Adv-DBMS/lab%205/rbt.hpp) | Header file containing the RBT definitions, node structure, and class interface. |
| [rbt.cpp](file:///Users/kushalsacharya/Desktop/scaler-Adv-DBMS/lab%205/rbt.cpp) | Source file implementing rotation, insertion, fixup, and diagnostics logic. |
| [main.cpp](file:///Users/kushalsacharya/Desktop/scaler-Adv-DBMS/lab%205/main.cpp) | Interactive CLI driver program that runs demonstration sequences and audits the tree. |
| [CMakeLists.txt](file:///Users/kushalsacharya/Desktop/scaler-Adv-DBMS/lab%205/CMakeLists.txt) | CMake compilation script. |
| [README.md](file:///Users/kushalsacharya/Desktop/scaler-Adv-DBMS/lab%205/README.md) | Documentation, structural comparison, and rotation analysis. |

---

## Balanced Binary Search Trees: Red-Black vs. AVL

### Conceptual Clarification

The instructor's prompt states:
> "The strict requirement of a balanced BST is that the left and right subtrees can never have a height difference greater than 1."

This specific height constraint ($|\text{height}(L) - \text{height}(R)| \le 1$) is the balancing rule for **AVL Trees**. 

**Red-Black Trees** utilize a more relaxed balancing constraint based on node coloring, which bounds the tree height to at most $2 \log_2(n + 1)$. In a Red-Black tree, the height of the left and right subtrees at any node can differ by up to a factor of two.

#### Comparison Table

| Property | AVL Tree | Red-Black Tree |
|----------|----------|----------------|
| **Balancing Condition** | Strict height difference ($\le 1$) | Color-based paths (longest path $\le 2 \times$ shortest path) |
| **Max Height** | $\approx 1.44 \log_2(n)$ | $\le 2 \log_2(n + 1)$ |
| **Search Time Complexity** | $O(\log n)$ (faster due to tighter height) | $O(\log n)$ (slightly slower search than AVL) |
| **Insertion Rebalancing** | Max 2 rotations | Max 2 rotations + recoloring |
| **Deletion Rebalancing** | Up to $O(\log n)$ rotations | Max 3 rotations |
| **Real-world Use Cases** | Read-heavy lookups (dictionaries) | General-purpose databases, C++ `std::map`, Java `TreeMap` |

---

## Rotation Mechanics

Rotations are local operations on a Binary Search Tree that modify the tree's pointer structure while preserving the in-order traversal key sequence.

```
       LEFT ROTATION (on x)                      RIGHT ROTATION (on y)
       
           y = x->right                              x = y->left
           
            (Parent)                                  (Parent)
               |                                         |
               x                                         y
              / \      --- Left Rotate --->             / \
             A   y                                     x   C
                / \    <-- Right Rotate ---           / \
               B   C                                 A   B
```

- **Left Rotation (on x)**: Moves node $y$ (the right child of $x$) to $x$'s position. The left child of $y$ ($B$) becomes the new right child of $x$.
- **Right Rotation (on y)**: Moves node $x$ (the left child of $y$) to $y$'s position. The right child of $x$ ($B$) becomes the new left child of $y$.

### C++ Rotation Code Example
```cpp
void RedBlackTree::leftRotate(Node* x) {
    Node* y = x->right;
    x->right = y->left;
    if (y->left != NIL) y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == NIL) root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;
    y->left = x;
    x->parent = y;
}
```

---

## Insertion and Rebalancing (Fixup) Cases

When a new node $z$ is inserted, it is initially colored **RED**. This preserves the black height of all paths but can violate the rule that **no two RED nodes can be adjacent** (if $z$'s parent is also RED).

To resolve double-red violations, the tree inspects the color of $z$'s **parent** and $z$'s **uncle** (the parent's sibling) and handles one of three cases:

### Case 1: Uncle is RED (Recolor Only)
If the uncle is RED, the parent, uncle, and grandparent are recolored. No rotation is necessary, but the violation moves up to the grandparent.

```
      (Grandparent) [BLACK]                  (Grandparent) [RED]
         /         \                            /         \
   (Parent) [RED]  (Uncle) [RED]   ===>   (Parent) [BLACK] (Uncle) [BLACK]
     /                                      /
  (z) [RED]                              (z) [RED]
```
*Action*: Set Parent to BLACK, Uncle to BLACK, Grandparent to RED. Move pointer $z$ up to the Grandparent and repeat.

### Case 2: Uncle is BLACK, $z$ is a Right Child (Rotate Parent Left)
If the parent is a left child, the uncle is BLACK, and $z$ is a right child, we perform a Left Rotation around the parent to align the nodes linearly.

```
      (Grandparent) [BLACK]                  (Grandparent) [BLACK]
         /         \                            /         \
   (Parent) [RED]  (Uncle) [BLACK] ===>     (z) [RED]     (Uncle) [BLACK]
     \                                      /
     (z) [RED]                        (Parent) [RED]
```
*Action*: Rotate Parent Left. Pointer $z$ now points to the original parent node, transitioning the tree into Case 3.

### Case 3: Uncle is BLACK, $z$ is a Left Child (Recolor & Rotate Grandparent Right)
If the parent is a left child, the uncle is BLACK, and $z$ is a left child, we recolor the parent to BLACK, grandparent to RED, and rotate the grandparent right.

```
       (Grandparent) [BLACK]                    (Parent) [BLACK]
          /         \                            /         \
    (Parent) [RED]  (Uncle) [BLACK] ===>      (z) [RED]  (Grandparent) [RED]
      /                                                       \
   (z) [RED]                                                  (Uncle) [BLACK]
```
*Action*: Set Parent to BLACK, Grandparent to RED, and Rotate Grandparent Right. This resolves the double-red violation permanently.

---

## Verification & Execution Guide

### Compilation

Compile the project directly using `clang++` or `g++` (requires C++20 support):

```bash
clang++ -std=c++20 lab 5/rbt.cpp lab 5/main.cpp -o lab 5/rbt_demo
```

### Running the Driver

Run the compiled executable:

```bash
./lab 5/rbt_demo
```

### Demonstration Run

Upon startup, the program automatically inserts a sequence of keys (`10, 20, 30, 15, 25, 5, 1`) and displays the balanced tree layout:

```text
└── 20 [BLACK]
    ├── 30 [BLACK]
    │   └── 25 [RED]
    └── 10 [RED]
        ├── 15 [BLACK]
        └── 5 [BLACK]
            └── 1 [RED]
```

Executing the Diagnostics menu option `[5]` produces the audit details:
- **Red-Black Property Verification**: Checks for root color, no double-red adjacencies, and matching path black heights.
- **AVL Balance Verification**: Reports the height and balance factor ($h_L - h_R$) of every node in the tree.

---

## Conclusion

This lab illustrates the mechanics of self-balancing binary search trees. By managing node colors and employing tree rotations, the Red-Black tree maintains $O(\log n)$ search, insertion, and deletion complexity. This foundational logic maps directly to the node splitting, merging, and pointer redistribution routines utilized in database index structures like B-Trees.
