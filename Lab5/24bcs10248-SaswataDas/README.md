# Lab 5 — Red-Black Tree (C++)

**Name.** Saswata Das
**Roll No.** 24BCS10248

CLRS-style red-black BST with `insert`, `search`, `inorder`, sideways
pretty-printer, and an invariant checker.

## File

| File | Purpose |
|---|---|
| `RB_tree.cpp` | `RBTree` class + `main()` demo |

## Algorithm

A red-black tree is a balanced BST where every node carries one extra bit
(its colour) and the following four invariants hold at all times:

1. **Root is black.**
2. **No red node has a red child** (no two red nodes in a row).
3. **Every root-to-NIL path has the same number of black nodes**
   (uniform black-height).
4. **In-order traversal is sorted** (standard BST order).

These rules together guarantee tree height <= 2*log2(n+1), so `insert` and
`search` are both **O(log n)** worst-case.

### Insert

1. Plain BST insert, paint the new node **red**.
2. Run `repairAfterInsert`. The new node is red, so the only invariant it can
   break is rule 2 (red-red against its parent). Three cases:

   - **Case 1 -- uncle is red:** recolour parent and uncle black, grandparent
     red, walk up with `z = grandparent` and re-check.
   - **Case 2 -- uncle is black, z is the "inner" grandchild (zig-zag):**
     rotate parent to turn the zig-zag into a straight line, then fall
     through to case 3.
   - **Case 3 -- uncle is black, z is the "outer" grandchild (zig-zig):**
     recolour parent black + grandparent red, then rotate grandparent. Done.

   Each iteration either terminates (case 2/3) or moves two levels up the
   tree (case 1), giving the O(log n) bound. Finally repaint root black.

### `checkInvariants()`

Run after each demo to confirm all four invariants hold simultaneously.
`computeBlackHeight()` returns the uniform black-height or `-1` if any subtree
breaks rules 2 or 3; `verifyBstOrder()` recursively checks rule 4 with running
`(lo, hi)` bounds. Catches any future bug as a single boolean.

## Build & run

```bash
cd Lab5/24bcs10248-SaswataDas
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 RB_tree.cpp -o rb_tree
./rb_tree
```

## Sample run

```
Inserting: 12 24 36 18 30 6 3 9 48 42

Tree (right-rotated 90 deg; R=red, B=black):
        48(B)
            42(R)
    36(R)
        30(B)
24(B)
        18(B)
    12(R)
            9(R)
        6(B)
            3(R)

Inorder (must be sorted): 3 6 9 12 18 24 30 36 42 48

Searches:
  search(18) -> found
  search(99) -> miss
  search(3) -> found
  search(50) -> miss

RB invariants hold: yes
```

The pretty-print is rotated 90 degrees clockwise: the **root sits at the leftmost
column**, right children appear *above* their parent, left children *below*.
Reading top-to-bottom is equivalent to reading the tree right-to-left in the
normal orientation, so the inorder line `3 6 9 ... 48` is the same sequence
you'd get walking the printed picture from bottom to top.

The final `assert(ok)` plus the `inorder == sorted(keys)` assert catch any
regression on future edits.
