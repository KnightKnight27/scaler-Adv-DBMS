# Lab 5: Red-Black Tree Implementation

## Objective

To implement and analyze a Red-Black Tree — a self-balancing BST that guarantees O(log n) insertions and searches by maintaining balance through recoloring and rotations.

---

## Red-Black Tree Properties

Every valid Red-Black Tree must satisfy these at all times:

1. Every node is either **RED** or **BLACK**
2. The **root** is always **BLACK**
3. All **NIL leaf nodes** are BLACK (sentinel nodes)
4. A **RED node** cannot have a RED child (no consecutive reds)
5. Every path from a node to its descendant NIL leaves has the **same number of BLACK nodes** (black height)

---

## What the Code Does

### Task 1 — Initialization
Creates an empty tree with a `NIL` sentinel node (black). All null pointers point to this sentinel instead of `nullptr`, which simplifies edge-case handling.

### Task 2 & 3 — Insertion + Balancing
Every new node is inserted as RED (BST rules), then `fixInsert()` runs to restore RBT properties. Three cases are handled:

| Case | Condition | Fix |
|------|-----------|-----|
| 1 | Uncle is RED | Recolor parent, uncle → BLACK; grandparent → RED |
| 2 | Uncle is BLACK, node is inner child | Rotate parent toward root to convert to Case 3 |
| 3 | Uncle is BLACK, node is outer child | Rotate grandparent + recolor |

Each case is printed to stdout so you can follow every step.

### Task 4 — Search
Standard BST search. Prints the node's color and number of comparisons taken. Because the tree is balanced, search depth stays at O(log n).

### Task 5 — Inorder Traversal
Visits nodes in sorted order, printing each value with its color e.g. `10(R) 15(B) 20(B)`. This confirms the BST property is intact.

### Task 6 — Property Verification
After all insertions, checks:
- Root is BLACK
- Black height is consistent across all paths
- No two consecutive RED nodes exist

---

## How to Run

```bash
g++ -std=c++17 -Wall -o red_black_tree red_black_tree.cpp
./red_black_tree
```

---

## Sample Output

```
Inserting 30:
  [insert] 30 inserted as RED
    [fix] Case 3 recolor (mirror): parent 20 → BLACK, grandparent 10 → RED
    [rotate] LEFT rotate on 10

>>> Task 5: Inorder Traversal
  Inorder: 1(R) 5(B) 7(R) 10(R) 15(B) 20(B) 25(R) 30(B)

>>> Tree Structure
L----20(BLACK)
|    L----10(RED)
|    |    L----5(BLACK)
|    |    |    L----1(RED)
|    |    |    R----7(RED)
|    |    R----15(BLACK)
|    R----30(BLACK)
|         L----25(RED)

>>> Task 6: Property Verification
  Root is BLACK [OK]
  Black height consistent: YES [OK]
  No consecutive red nodes: YES [OK]
```

---

## Observations

- Inserting 10, 20, 30 in sorted order triggers a left rotation — without it the tree would become a linked list.
- A plain BST on sorted input degrades to O(n) search. RBT keeps it at O(log n).
- Recoloring alone is cheaper than rotation and handles the majority of fix cases.
- The NIL sentinel trick avoids writing special-case code for leaf edges everywhere.

---

## Complexity

| Operation | Average | Worst case |
|-----------|---------|------------|
| Insert    | O(log n)| O(log n)   |
| Search    | O(log n)| O(log n)   |
| Traversal | O(n)    | O(n)       |

---

## Author
Submitted as part of Lab 5 – Database Systems Lab  
Date: May 2026