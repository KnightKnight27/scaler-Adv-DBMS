# Lab 5: Red-Black Tree Implementation

**Student:** Talin Daga (24bcs10321)

## Objective
Implement and analyse a Red-Black Tree — a self-balancing BST that guarantees  
O(log n) insertion and search by enforcing five structural invariants via  
rotations and recolouring after every insertion.

## Files

| File | Description |
|------|-------------|
| `rbt.c` | Complete C implementation — 6 tasks, step-by-step fixup logging |

## Build & Run

```bash
gcc -Wall -Wextra -o rbt rbt.c
./rbt
```

---

## Red-Black Tree Properties

| # | Property |
|---|----------|
| P1 | Every node is RED or BLACK |
| P2 | The root is BLACK |
| P3 | All NIL sentinel leaves are BLACK |
| P4 | A RED node's children are both BLACK (no two consecutive RED nodes) |
| P5 | Every simple path from a node to a descendant NIL leaf contains the same number of BLACK nodes (uniform black-height) |

---

## Tasks Demonstrated

| Task | Description |
|------|-------------|
| 1 | Tree init — NIL sentinel created, all five RBT invariants stated |
| 2 | Insert 10, 20, 30, 15, 25, 5, 1, 7, 12 with per-step BST-place + fixup logs |
| 3 | Balancing summary — case reference, rotation count, recolour count |
| 4 | Search 15, 25 (found); 11, 35 (not found) — exact comparison paths shown |
| 5 | Inorder traversal (sorted output), level-order layout, sideways layout |
| 6 | Automatic verification of P1–P5 + BST ordering; uniform black-height printed |

---

## Fixup Cases

| Case | Trigger | Action |
|------|---------|--------|
| **1** | Uncle is RED | Recolour parent + uncle → BLACK, grandparent → RED; move `z` up |
| **2** | Uncle BLACK, `z` is inner child | Rotate parent toward grandparent → converts to Case 3 |
| **3** | Uncle BLACK, `z` is outer child | Rotate grandparent + recolour; violation resolved locally |
| **1m/2m/3m** | Mirror (parent is right child) | Symmetric operations |

---

## Sample Output — Final Tree (9 insertions: 10 20 30 15 25 5 1 7 12)

```
  Level 0: 20(B)
  Level 1: 10(R)  30(B)
  Level 2: 5(B)  15(B)  25(R)
  Level 3: 1(R)  7(R)  12(R)

  Inorder: 1(R), 5(B), 7(R), 10(R), 12(R), 15(B), 20(B), 25(R), 30(B)

  Rotations done  : 1
  Recolorings done: 10
  Black-height    : 3  (all paths)
```

---

## Observations (fill in after running)

| Metric | Observed Value |
|--------|---------------|
| Insert that triggered a rotation | |
| Fixup case used for insert 30 | |
| Fixup case used for insert 15 | |
| Fixup case used for insert 1 | |
| Total rotations | |
| Total recolourings | |
| Verified black-height | |
| Comparisons to find key 15 | |
| Comparisons to search key 35 | |

---

## Analysis Questions

1. **What are the five Red-Black Tree properties and why is each needed?**

2. **What happens to the tree structure when insert 30 is executed?  
   Which case applies and which rotation is performed?**

3. **Why does Case 1 not require any rotation?**

4. **Why does Case 2 always fall through to Case 3?**

5. **How does the sentinel NIL node simplify boundary checks in the code?**

6. **What is the black-height of the final tree, and how is it verified?**

7. **Compare the worst-case search performance of an RBT vs an unbalanced BST.**

8. **If the same 9 values were inserted into a plain BST in sorted order,  
   what would the tree look like, and what would search cost be?**

---

## Complexity Reference

| Operation | RBT | Unbalanced BST (worst) |
|-----------|-----|----------------------|
| Insert | O(log n) | O(n) |
| Search | O(log n) | O(n) |
| Space | O(n) | O(n) |
| Rotation per insert | O(1) amortised | — |
