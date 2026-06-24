# Lab 5 — Red-Black Tree Implementation

> **Course:** Advanced DBMS
> **Author:** Mahir Abidi (24BCS10125)
> **Language:** C++17

A from-scratch implementation of a **Red-Black Tree (RBT)** — a self-balancing binary search tree where every node is colored red or black, and a small set of invariants keeps the tree height bounded at `O(log n)`. RBTs are the backbone of ordered in-memory indexes in many real systems: PostgreSQL's `rbtree.c`, Linux kernel's scheduler structures, and `std::map` / `std::set` in libstdc++ are all built on them.

---

## Table of Contents

1. [Files](#files)
2. [Build & Run](#build--run)
3. [Sample Output](#sample-output)
4. [Public API](#public-api)
5. [The Five Invariants](#the-five-invariants)
6. [Insertion Fixup — The Three Cases](#insertion-fixup--the-three-cases)
7. [Deletion Fixup — The Four Cases](#deletion-fixup--the-four-cases)
8. [Rotations](#rotations)
9. [The NIL Sentinel](#the-nil-sentinel)
10. [Complexity](#complexity)

---

## Files

| File | Purpose |
|------|---------|
| `RedBlackTree.h` | Class declaration — `Node`, `Color`, public API, private helpers |
| `RedBlackTree.cc` | Full implementation: insert, find, remove, rotations, fixups, BFS print |
| `main.cc` | Demo driver — insert, find, remove, print |
| `Makefile` | Builds with `g++ -std=c++17 -Wall -Wextra -O2` |
| `README.md` | This document |

---

## Build & Run

```bash
make          # compiles -> ./rbtree
make run      # compiles + runs the demo
make clean    # removes the binary
```

Manual build:

```bash
g++ -std=c++17 -Wall -Wextra -O2 -o rbtree main.cc RedBlackTree.cc
./rbtree
```

---

## Sample Output

```
Inserting: 50 25 75 10 30 60 80 5 15 70

Tree after inserts (level-order, format key:R/B):
  [50:B, 25:R, 75:R, 10:B, 30:B, 60:B, 80:B, 5:R, 15:R, nil, nil, nil, 70:R]

find(30)  -> found
find(70)  -> found
find(99)  -> not found

Removing 25, 60, 80 ...
Tree after removals:
  [50:B, 10:R, 75:B, 5:B, 30:B, 70:R, nil, nil, nil, 15:R]

find(25)  -> not found
find(50)  -> found
```

Reading the post-insert tree in level-order:

```
                  50:B
               /        \
           25:R           75:R
          /    \         /    \
       10:B   30:B    60:B   80:B
       /  \              \
     5:R  15:R           70:R
```

Every path from root to a nil leaf contains **3 black nodes** (50, then one of 10/30/60/80, then a nil) — the black-height invariant holds. No red node has a red child — invariant 4 holds.

---

## Public API

```cpp
class RedBlackTree {
public:
    RedBlackTree();
    ~RedBlackTree();

    void insert(int key);     // O(log n) — BST insert then fixInsert
    bool find(int key) const; // O(log n) — standard BST search
    void remove(int key);     // O(log n) — CLRS RBT-Delete; no-op if absent
    void print() const;       // level-order BFS, format: key:R / key:B
};
```

---

## The Five Invariants

A valid Red-Black Tree must satisfy **all five** of the following at all times:

1. Every node is either **red** or **black**.
2. The **root** is black.
3. Every **NIL leaf** is black. (We use a single shared sentinel node.)
4. A **red node never has a red child** — no two reds in a row.
5. Every path from any node down to a NIL descendant contains the **same number of black nodes** (the *black-height*).

Properties 4 and 5 together guarantee the tree height never exceeds `2 · log₂(n + 1)`.

---

## Insertion Fixup — The Three Cases

New nodes are always inserted **red**. This preserves property 5 (black-height unchanged) but may break property 4 (red parent + red child). `fixInsert(z)` walks the violation upward and repairs it using three cases, applied symmetrically for left- and right-leaning parents.

### Case 1 — Uncle is RED

Recolor parent and uncle to **black**, grandparent to **red**, then move `z` up to the grandparent and check again. The local subtree is fixed, but the grandparent's parent might now have a red-red violation.

```
      G(B)                  G(R)    <-- recurse here
      / \        ==>        / \
    P(R) U(R)            P(B) U(B)
    /                    /
   z(R)                z(R)
```

### Case 2 — Uncle is BLACK, z is the inner grandchild (zigzag)

A left-right (LR) or right-left (RL) pattern. Rotate around the parent to convert the zigzag into a straight line, making the old parent become `z` and falling through to Case 3.

```
   G(B)                G(B)
   / \      (LR)       / \
  P(R) U(B)   ==>   z(R)  U(B)      rotate left(P)
   \                /
    z(R)          P(R)
```

### Case 3 — Uncle is BLACK, z is the outer grandchild (straight line)

An LL or RR pattern. Recolor the parent **black** and grandparent **red**, then rotate around the grandparent. The subtree is fully fixed — no further upward propagation needed.

```
      G(B)                 P(B)
      / \      ==>         / \
    P(R) U(B)            z(R) G(R)
    /                          \
   z(R)                        U(B)
```

After the loop exits, we unconditionally set `m_root->color = BLACK` — this is the only place the root's color can ever be changed.

---

## Deletion Fixup — The Four Cases

Deletion follows the standard **CLRS** algorithm (Introduction to Algorithms, 3rd ed., Chapter 13):

1. **BST-delete** node `z`:
   - If `z` has at most one real child, splice it out via `transplant()`.
   - Otherwise, find `z`'s **in-order successor** `y` (minimum of its right subtree), splice `y` out of its current position, and copy it into `z`'s place, inheriting `z`'s color.
2. Let `x` be the node that moved into the vacated position. If the **original color** of the removed/moved node was **black**, call `fixDelete(x)` to repair the resulting black-height deficit.

`fixDelete(x)` walks up the tree. At each step, `x` carries a conceptual "extra black" that must be absorbed or pushed upward. It dispatches to four sibling-based cases (and their mirrors):

| Case | Condition | Action |
|------|-----------|--------|
| 1 | Sibling `w` is **red** | Rotate around parent, swap colors → converts to cases 2/3/4 |
| 2 | Both children of `w` are **black** | Recolor `w` red, move `x` up to parent |
| 3 | `w`'s far child is **black** (near child red) | Rotate around `w`, swap colors → converts to case 4 |
| 4 | `w`'s far child is **red** | Rotate around parent, recolor → done (set `x = root`) |

---

## Rotations

Rotations are the single structural operation used by both fixups. They preserve the BST ordering property while rewiring parent/child pointers.

### Left rotation on `x`

Promotes `x`'s right child `y` upward; `y`'s left subtree `b` is re-attached to `x`.

```
    x                  y
   / \      ==>       / \
  a   y              x   c
     / \            / \
    b   c          a   b
```

### Right rotation on `y`

The mirror: promotes `y`'s left child `x`; `x`'s right subtree `b` re-attaches to `y`.

```
    y                  x
   / \      ==>       / \
  x   c              a   y
 / \                    / \
a   b                  b   c
```

Both rotations run in **O(1)** — they only update a constant number of pointers.

---

## The NIL Sentinel

Instead of using raw `nullptr` for empty leaf slots, this implementation allocates a single **shared sentinel node** (`m_nil`) at construction time. Every empty pointer in the tree points to this one node, which is always colored **black**.

Benefits:
- Eliminates null-checks in every rotation and fixup step — we can always safely read `node->color`, `node->left`, etc.
- Simplifies the fixDelete sibling cases, which must read sibling children's colors even when the sibling has no real children.

`m_nil->parent` is never set meaningfully; only `m_nil->color = BLACK` and the fact that `m_nil != any real node` matter.

---

## Complexity

| Operation | Time | Space |
|-----------|------|-------|
| `find` | `O(log n)` | `O(1)` |
| `insert` | `O(log n)` | `O(1)` extra (fixup is iterative) |
| `remove` | `O(log n)` | `O(1)` extra |
| `print` (BFS) | `O(n)` | `O(n)` (queue) |
| Space total | — | `O(n)` |

The `O(log n)` height bound follows directly from invariants 4 and 5: the longest root-to-leaf path (alternating red and black) is at most twice the shortest (all black), and both are bounded by `log₂(n + 1)`.

---

> *Lab 5 — Advanced DBMS coursework, Scaler School of Technology*
