# Lab 5 — Red-Black Tree Implementation

> **Course:** Advanced DBMS
> **Author:** Vanditabyaa Dwivedi (24BCS10505)
> **Language:** C++17

A from-scratch implementation of a **Red-Black Tree** — a self-balancing binary search tree where every node carries a color (red or black) and a small set of invariants forces the tree to stay within `O(log n)` height. RBTs are the backbone of in-memory ordered indexes in many databases (PostgreSQL's `RBTree`, Linux kernel scheduling structures, `std::map` / `std::set` in libstdc++).

---

## Table of Contents
1. [Files](#files)
2. [Build & Run](#build--run)
3. [Sample Output](#sample-output)
4. [Public API](#public-api)
5. [The Five Invariants](#the-five-invariants)
6. [Insertion Fixup — The Four Cases](#insertion-fixup--the-four-cases)
7. [Deletion Fixup](#deletion-fixup)
8. [Rotations](#rotations)
9. [Notes on the Sentinel `NIL` Node](#notes-on-the-sentinel-nil-node)
10. [Complexity](#complexity)

---

## Files

| File | Purpose |
|------|---------|
| `RedBlackTree.h` | Class declaration — `Node`, `Color`, public API, private helpers. |
| `RedBlackTree.cc` | Full implementation: insert, find, remove, rotations, fixups, BFS print. |
| `main.cc` | Small driver that exercises insert / find / remove and prints the tree. |
| `Makefile` | Builds the `rbtree` binary with `g++ -std=c++17 -Wall -Wextra -O2`. |
| `README.md` | This document. |

---

## Build & Run

```bash
make          # builds ./rbtree
make run      # builds and runs the demo
make clean    # removes the binary
```

Manual build:
```bash
g++ -std=c++17 -Wall -Wextra -O2 -o rbtree main.cc RedBlackTree.cc
./rbtree
```

To enable verbose case-dispatch logging during fixup, uncomment `#define DEBUG` near the top of `RedBlackTree.cc` and rebuild.

---

## Sample Output

```
Inserting: 10 20 30 15 25 5 1 8 40 35

Tree after inserts (level-order, suffix R = red, B = black):
[20B, 10R, 30R, 5B, 15B, 25B, 40B, 1R, 8R, null, null, null, null, 35R]

find(15) -> found
find(25) -> found
find(99) -> not found

Removing 20, 5, 30 ...
Tree after removals:
[25B, 10R, 35B, 8B, 15B, null, 40R, 1R]
```

The print format is **LeetCode-style level-order BFS**, with each node tagged `R` (red) or `B` (black). `null` denotes an empty `NIL` leaf slot. Reading the first output:

```
                 20B
               /     \
            10R       30R
           /   \     /   \
         5B    15B  25B   40B
        /  \                /
       1R   8R            35R
```

---

## Public API

```cpp
class RedBlackTree {
public:
    RedBlackTree();
    ~RedBlackTree();                 // frees every node + the NIL sentinel

    bool find(int val);              // O(log n) lookup
    void insert(int val);            // O(log n) insert; duplicates go left
    void remove(int val);            // O(log n) delete; no-op if not present

    void print();                    // level-order BFS dump
};
```

---

## The Five Invariants

A Red-Black Tree must satisfy **all five** of the following at all times:

1. Every node is colored either **red** or **black**.
2. The **root** is black.
3. Every **`NIL` leaf** is black. (The sentinel node has color `black`.)
4. A **red node never has a red child** ("no two reds in a row").
5. Every path from any node down to a `NIL` descendant contains the **same number of black nodes** (the *black-height*).

Together, properties 4 and 5 guarantee the tree's height is at most `2 · log₂(n + 1)`.

---

## Insertion Fixup — The Four Cases

New nodes are always inserted **red**. This may violate property 4 (red parent + red child), but never property 5 (black-height is unchanged). `fixTree(z)` walks the violation upward, dispatching to one of four cases:

### Case 0 — Parent is black (or `z` is root)

No violation. Done immediately.

### Case 1 — Parent **and** uncle are red

Recolor both parent and uncle to **black**, and grandparent to **red**. The local subtree is now valid, but the grandparent may have created a fresh violation higher up — recurse on the grandparent.

```
   G(B)              G(R)         <- recurse here
   / \      ==>      / \
  P(R) U(R)        P(B) U(B)
   \                \
    z(R)             z(R)
```

### Case 2 — Parent red, uncle black, `z` is the **inner** grandchild (LR or RL)

Rotate around the parent to convert this "zigzag" into Case 3's "straight line", then handle as Case 3.

```
   G(B)               G(B)
   / \                / \
  P(R) U(B)   ==>   z(R) U(B)        (after rotateLeft(P))
   \                /
    z(R)          P(R)
```

### Case 3 — Parent red, uncle black, `z` is the **outer** grandchild (LL or RR)

Recolor parent to **black**, grandparent to **red**, then rotate around the grandparent. The subtree's black-height is preserved and the red-red violation is gone.

```
       G(B)              P(B)
       / \               / \
     P(R) U(B)   ==>   z(R) G(R)
     /                       \
    z(R)                      U(B)
```

After fixup, we set `m_Root->color = black` as a final safety net — this is the only operation in the algorithm that can ever change the root's color.

---

## Deletion Fixup

Deletion follows standard **CLRS** RBT-Delete:

1. **BST-delete** the target node `z`:
   - If `z` has fewer than two real children, splice it out via `transplant()`.
   - Otherwise, find `z`'s **in-order successor** `y` (minimum of right subtree), splice `y` into `z`'s position, and inherit `z`'s color.
2. Let `y` be the node that was actually removed from the tree (or moved into a new position), and `x` be the node that took its place.
3. If `y`'s **original color was black**, the tree now has a "double-black" deficit on the path through `x` — call `fixDelete(x)` to repair it.

`fixDelete(x)` walks up the tree, distinguishing four mirrored sibling-based cases:

| Case | Sibling `w` | Action |
|------|-------------|--------|
| 1 | `w` is red | Recolor `w` black, parent red, rotate to make `w`'s child the new sibling. Reduces to one of cases 2/3/4. |
| 2 | `w` is black, both of `w`'s children are black | Recolor `w` red; move the deficit up to `x`'s parent. |
| 3 | `w` black, `w`'s near child red, far child black | Recolor and rotate around `w` to make `w`'s far child red. Reduces to case 4. |
| 4 | `w` black, `w`'s far child red | Final fix: recolor and rotate around the parent. The loop terminates. |

The loop terminates either by reaching the root or by entering case 4. Finally we paint `x` black to clear any residual extra-black.

---

## Rotations

Both fixups rely on the two standard rotations:

### `rotateLeft(x)`
```
       x                   y
      / \                 / \
     a   y      ==>      x   c
        / \             / \
       b   c           a   b
```

### `rotateRight(x)`
```
        x                  y
       / \                / \
      y   c     ==>      a   x
     / \                    / \
    a   b                  b   c
```

Each rotation is `O(1)` and rewires exactly three parent pointers and three child pointers, while preserving the BST in-order traversal.

---

## Notes on the Sentinel `NIL` Node

This implementation uses a single shared **sentinel** node (`NIL`) instead of `nullptr` for empty leaves. Benefits:

- The deletion fixup naturally reads `x->parent` even when `x` is a leaf — `NIL->parent` is set during `transplant` for exactly this reason.
- All `NIL` comparisons reduce to a pointer check, and `NIL->color` is **black** so the invariants hold without special-casing.
- It removes a class of `nullptr` dereferences that plague hand-rolled RBT code.

The sentinel is allocated in the constructor and freed in the destructor along with the rest of the tree (via post-order traversal in `destroy()`).

---

## Complexity

| Operation | Time | Space |
|-----------|------|-------|
| `find`    | `O(log n)` | `O(1)` |
| `insert`  | `O(log n)` amortized — at most `O(log n)` recolors + `O(1)` rotations | `O(1)` extra |
| `remove`  | `O(log n)` — at most `O(log n)` recolors + `O(1)` rotations (≤ 3) | `O(1)` extra |
| `print`   | `O(n)` | `O(n)` |

The strict `O(log n)` height guarantee — at most `2 · log₂(n + 1)` — is what makes RBTs preferable to plain BSTs for index structures.

---

> *Submitted as part of Lab 5 — Advanced DBMS coursework.*
