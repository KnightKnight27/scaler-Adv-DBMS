# Lab 5 — Red-Black Tree Implementation (C++)

## Overview

This project implements a self-balancing **Red-Black Tree (RBT)** in C++. A Red-Black Tree is a specialized Binary Search Tree (BST) that maintains dynamic balance after every insertion and deletion, guaranteeing an upper bound of **$O(\log n)$** time complexity for search, insertion, and deletion operations.

Balance is maintained by coloring each node **RED** or **BLACK** and enforcing structural properties during insertions via tree rotations and color adjustments.

---

## Objectives

- Implement a robust **Red-Black Tree** structure in C++.
- Implement automatic self-balancing using a post-insertion adjustment routine (`fixInsert`).
- Implement core binary tree restructuring primitives: **Left Rotation** and **Right Rotation**.
- Maintain all Red-Black Tree structural invariants after every element insertion.

---

## Red-Black Tree Invariants

Every valid Red-Black Tree must satisfy these four structural properties:

1. **Node Coloring**: Every node is either **RED** or **BLACK**.
2. **Root Property**: The root node is always **BLACK**.
3. **Red Invariant**: Consecutive RED nodes are prohibited (a RED node cannot have a RED parent or a RED child).
4. **Black Height Invariant**: Every path from a node to any of its descendant `NULL` (sentinel) leaves must contain the exact same number of **BLACK** nodes.

---

## Project Structure

```
.
├── Makefile            # Automates compilation and cleanup
├── README.md           # Project documentation and details
├── RedBlackTree.h      # Class definition and Node structure declarations
├── RedBlackTree.cc     # Implementations of rotations, insertion, and balancing
└── main.cc             # Test runner, properties verification, and main driver
```

---

## Technical Details

### Rotations
Rotations are localized pointer-restructuring operations that preserve the standard Binary Search Tree sorting property ($Left < Parent \le Right$) while modifying the tree's height profile to restore balance.

* **Left Rotation**: Restructures a right-leaning node sequence.
* **Right Rotation**: Restructures a left-leaning node sequence.

```
       Left Rotation (around x)              Right Rotation (around y)
       
            (x)                                      (y)
           /   \                                    /   \
          α    (y)      ===================>      (x)    γ
              /   \     <===================     /   \
             β     γ                             α    β
```

### Self-Balancing Insertion
After standard BST insertion places a new node (always initially colored **RED**), a violation of invariant 3 (consecutive RED nodes) may occur. We identify the correct action to resolve this violation based on the color of the node's **uncle**:

| Case | Condition | Action |
| :--- | :--- | :--- |
| **Case 1** | Uncle is **RED** | Recolor parent and uncle $\to$ **BLACK**; grandparent $\to$ **RED**; advance pointer up to grandparent. |
| **Case 2** | Uncle is **BLACK**, node forms a **triangle** | Rotate parent in the opposite direction of the node to transform into **Case 3**. |
| **Case 3** | Uncle is **BLACK**, node forms a **line** | Recolor parent $\to$ **BLACK**, grandparent $\to$ **RED**; rotate grandparent. |

---

## Time Complexity

| Operation | Average Case | Worst Case |
| :--- | :--- | :--- |
| **Insertion** | $O(\log n)$ | $O(\log n)$ |
| **Search** | $O(\log n)$ | $O(\log n)$ |
| **Rotation** | $O(1)$ | $O(1)$ |

---

## Compilation and Execution

### Using Makefile (Recommended)

To compile and link the project automatically:
```bash
make
```

To run the compiled executable:
```bash
./rbt
```

To clean intermediate object files and binary artifacts:
```bash
make clean
```

### Manual Compilation

Alternatively, compile the C++ source files directly:
```bash
g++ -Wall -Wextra -std=c++17 main.cc RedBlackTree.cc -o rbt
./rbt
```

---

## Sample Execution Output

Below is the expected terminal output showing dynamic step-by-step insertions, hierarchical structures, invariant assertions, and final sorted inorder traversal:

```text
===========================================
  Red-Black Tree (RBT) Verification Lab   
===========================================

Inserting elements step-by-step:
-> Inserting: 10
10 (BLACK)
✓ RBT Invariants: Valid

-> Inserting: 15
10 (BLACK)
    └── 15 (RED)
✓ RBT Invariants: Valid

-> Inserting: 20
15 (BLACK)
├── 10 (RED)
└── 20 (RED)
✓ RBT Invariants: Valid

-> Inserting: 25
15 (BLACK)
├── 10 (BLACK)
└── 20 (BLACK)
    └── 25 (RED)
✓ RBT Invariants: Valid

-> Inserting: 30
15 (BLACK)
├── 10 (BLACK)
└── 25 (BLACK)
    ├── 20 (RED)
    └── 30 (RED)
✓ RBT Invariants: Valid

-------------------------------------------
Final Tree Traversal
-------------------------------------------
Inorder Traversal: 10 15 20 25 30 

Visualizing Balanced Tree Structure:
15 (BLACK)
├── 10 (BLACK)
└── 25 (BLACK)
    ├── 20 (RED)
    └── 30 (RED)

Verification Completed Successfully! All RBT Invariants Maintained.
===========================================
```

---

## Author

* **Name**: Rohan Ranjan
* **Lab**: Lab 5
