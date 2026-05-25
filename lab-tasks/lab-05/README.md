# Red-Black Tree — Complete C++ Implementation

A production-quality, self-balancing Binary Search Tree (BST) in a single
C++ file (`red_black_tree.cpp`), with a comprehensive demonstration and test
suite in `main()`.

---

## Table of Contents

1. [What Is a Red-Black Tree?](#1-what-is-a-red-black-tree)
2. [The Five Properties](#2-the-five-properties)
3. [File Structure](#3-file-structure)
4. [How to Compile & Run](#4-how-to-compile--run)
5. [Public API](#5-public-api)
6. [Algorithm Deep-Dive](#6-algorithm-deep-dive)
   - [Sentinel NIL Node](#sentinel-nil-node)
   - [Insertion](#insertion)
   - [Deletion](#deletion)
   - [Rotations](#rotations)
7. [Time & Space Complexity](#7-time--space-complexity)
8. [Visual Example](#8-visual-example)
9. [Test Coverage in main()](#9-test-coverage-in-main)

---

## 1. What Is a Red-Black Tree?

A **Red-Black Tree** is a height-balanced BST where every node carries an extra
"color" bit (RED or BLACK). The color constraints guarantee that the longest
root-to-leaf path is no more than **twice** the shortest one, keeping the tree
height bounded at **O(log n)** for n nodes.

This bound ensures that insert, delete, and search all run in **O(log n)**
worst-case time — unlike a plain BST, which degenerates to O(n) on sorted
input.

Red-Black Trees power:
- `std::map` and `std::set` in the C++ STL
- Java's `TreeMap` / `TreeSet`
- Linux kernel's completely fair scheduler (CFS)
- Many database index structures

---

## 2. The Five Properties

Every valid Red-Black Tree satisfies **all** five of these simultaneously:

| # | Property |
|---|----------|
| 1 | Every node is either **RED** or **BLACK**. |
| 2 | The **root** is always **BLACK**. |
| 3 | Every **NULL leaf** (represented by the NIL sentinel) is **BLACK**. |
| 4 | If a node is **RED**, both its children are **BLACK** (no two adjacent RED nodes). |
| 5 | For every node, all simple paths from it to descendant leaves contain the **same number of BLACK nodes** (the "black-height"). |

The implementation verifies all five properties at any point via
`isValidRBTree()`.

---

## 3. File Structure

```
red_black_tree.cpp
│
├── enum Color { RED, BLACK }          — Node color tag
│
├── struct Node                        — Tree node
│   ├── data   : int                   — Stored key
│   ├── color  : Color
│   └── left / right / parent : Node*
│
└── class RedBlackTree
    │
    ├── Private helpers
    │   ├── createNIL()                — Sentinel construction
    │   ├── rotateLeft(x)             — Left rotation  O(1)
    │   ├── rotateRight(y)            — Right rotation O(1)
    │   ├── insertFixup(z)            — Post-insert rebalance
    │   ├── transplant(u, v)          — Subtree replacement
    │   ├── minimum(x) / maximum(x)   — Subtree extremes
    │   ├── deleteFixup(x)            — Post-delete rebalance
    │   ├── inorder/preorder/         — Recursive traversals
    │   │   postorder helpers
    │   ├── validateHelper(node)      — RB-property checker
    │   ├── printHelper(...)          — ASCII tree printer
    │   └── destroyTree(node)         — Recursive destructor
    │
    └── Public interface
        ├── insert(key)
        ├── remove(key)
        ├── search(key)
        ├── getMin() / getMax()
        ├── successor(key) / predecessor(key)
        ├── height() / blackHeight() / size() / isEmpty()
        ├── inorder/preorder/postorder/levelOrder()
        ├── isValidRBTree()
        └── printTree() / printTraversals()
```

---

## 4. How to Compile & Run

**Requirements:** Any C++17-compatible compiler (GCC ≥ 7, Clang ≥ 5, MSVC 2017+).

```bash
# Compile
g++ -std=c++17 -Wall -Wextra -o rbt red_black_tree.cpp

# Run
./rbt
```

No external libraries or build the system required — it is a single self-contained
file.

---

## 5. Public API

### Mutating Operations

| Method | Description | Time |
|--------|-------------|------|
| `insert(int key)` | Insert a key (duplicates silently ignored) | O(log n) |
| `remove(int key)` | Delete a key (prints warning if not found) | O(log n) |

### Query Operations

| Method | Returns | Time |
|--------|---------|------|
| `search(int key)` | `bool` — whether the key exists | O(log n) |
| `getMin()` | Minimum key; throws if empty | O(log n) |
| `getMax()` | Maximum key; throws if empty | O(log n) |
| `successor(int key)` | Next larger key, or `-1` if none | O(log n) |
| `predecessor(int key)` | Next smaller key, or `-1` if none | O(log n) |
| `height()` | Longest root-to-leaf path length | O(n) |
| `blackHeight()` | Number of BLACK nodes on any root-to-leaf path | O(log n) |
| `size()` | Total node count | O(n) |
| `isEmpty()` | `true` if tree has no nodes | O(1) |

### Traversals

| Method | Order | Time |
|--------|-------|------|
| `inorder()` | Left → Node → Right (returns sorted sequence) | O(n) |
| `preorder()` | Node → Left → Right | O(n) |
| `postorder()` | Left → Right → Node | O(n) |
| `levelOrder()` | Breadth-first (BFS) | O(n) |

All traversals return `std::vector<int>`.

### Validation & Display

| Method | Description |
|--------|-------------|
| `isValidRBTree()` | Verifies all 5 RB-tree properties; returns `bool` |
| `printTree()` | ASCII tree with `(R)`/`(B)` color labels |
| `printTraversals()` | Prints all four traversals to stdout |

---

## 6. Algorithm Deep-Dive

### Sentinel NIL Node

Instead of using raw `nullptr`, the tree uses a single shared **NIL sentinel**
node that is always BLACK. Every "leaf" pointer in the tree points to this
sentinel rather than null.

**Why?** The rotation and fixup routines constantly access `node->color` and
`node->parent`. With a sentinel, these accesses are always valid — no
null-pointer dereferences, no branching on `nullptr`.

```
NIL->color  = BLACK
NIL->left   = NIL   (points to itself)
NIL->right  = NIL
NIL->parent = NIL
```

---

### Insertion

```
insert(key)
│
├── 1. Standard BST walk to find position
├── 2. Create new RED node z, link to parent
└── 3. Call insertFixup(z)
```

**insertFixup** handles the case where z's parent is also RED (violating
property 4). There are three cases, applied symmetrically for left- and
right-leaning parents:

```
While z's parent is RED:

  Case 1 — Uncle is RED
  ┌────────────────────────────────────────────┐
  │    G(B)          G(R)  ← move z up to G   │
  │   / \    ==>    / \                        │
  │  P(R) U(R)    P(B) U(B)                   │
  │  |                                         │
  │  z(R)                                      │
  └────────────────────────────────────────────┘

  Case 2 — Uncle is BLACK, z is inner child (triangle)
  ┌────────────────────────────────────────────┐
  │    G(B)          G(B)                      │
  │   / \    ==>    / \    then → Case 3       │
  │  P(R) U(B)    z(R) U(B)                    │
  │    \          /                            │
  │    z(R)      P(R)                          │
  │  (rotateLeft around P)                     │
  └────────────────────────────────────────────┘

  Case 3 — Uncle is BLACK, z is outer child (line)
  ┌────────────────────────────────────────────┐
  │      G(B)          P(B)                    │
  │     / \    ==>    / \                      │
  │   P(R) U(B)     z(R) G(R)                  │
  │   /                   \                    │
  │  z(R)                 U(B)                 │
  │  (rotateRight around G)                    │
  └────────────────────────────────────────────┘

Root is always painted BLACK at the end.
```

---

### Deletion

Deletion follows the CLRS algorithm (Cormen, Leiserson, Rivest, Stein):

```
remove(key)
│
├── Find node z
├── Determine node y to splice out and x to replace it
│   ├── Case A: z has no left child  → transplant z with z->right
│   ├── Case B: z has no right child → transplant z with z->left
│   └── Case C: z has two children
│       ├── Find in-order successor y = minimum(z->right)
│       ├── If y is not z's direct child, transplant y with y->right first
│       └── Transplant z with y, copy z's left subtree and color to y
└── If y was BLACK → call deleteFixup(x)
```

**deleteFixup** restores the black-height property. It handles four cases,
symmetrically for left/right:

```
Case 1 — Sibling w is RED
  → Recolor w & parent; rotateLeft(parent); reduces to Case 2/3/4.

Case 2 — w is BLACK, both of w's children are BLACK
  → Recolor w RED; move the "extra black" up to parent.

Case 3 — w is BLACK, w's near child is RED, far child is BLACK
  → Recolor & rotateRight(w); converts to Case 4.

Case 4 — w is BLACK, w's far child is RED
  → Recolor & rotateLeft(parent); done (x = root exits loop).
```

---

### Rotations

Both rotations run in **O(1)** and simply re-link three pointer pairs.

**Left Rotation** around x:
```
      x                  y
     / \      ==>       / \
    A   y             x   C
       / \           / \
      B   C         A   B
```

**Right Rotation** around y:
```
       y              x
      / \    ==>     / \
     x   C          A   y
    / \                / \
   A   B              B   C
```

---

## 7. Time & Space Complexity

| Operation | Time Complexity | Notes |
|-----------|----------------|-------|
| Insert | O(log n) | ≤ 2 rotations |
| Delete | O(log n) | ≤ 3 rotations |
| Search | O(log n) | Pure traversal |
| Min / Max | O(log n) | Follow left / right spine |
| Successor / Predecessor | O(log n) | |
| Height | O(n) | Full DFS |
| Size | O(n) | Full DFS |
| Traversals | O(n) | |
| isValidRBTree | O(n) | Full DFS |
| **Space (tree)** | **O(n)** | One node per key + 1 NIL sentinel |

The tree height is guaranteed ≤ **2 log₂(n+1)**, so all O(log n) bounds are
worst-case, not average-case.

---

## 8. Visual Example

Inserting `{10, 20, 30, 15, 25, 5, 1, 7, 40, 35}` in order:

```
R---- 20(B)
      L---- 10(R)
      |     L---- 5(B)
      |     |     L---- 1(R)
      |     |     R---- 7(R)
      |     R---- 15(B)
      R---- 30(R)
            L---- 25(B)
            R---- 40(B)
                  L---- 35(R)
```

- Root `20` is BLACK (property 2 ✓)
- Every RED node (`10`, `30`, `1`, `7`, `35`) has two BLACK children ✓
- Black-height = 2 on every root-to-leaf path ✓

---

## 9. Test Coverage in main()

The `main()` function runs six sections automatically:

| Section | What Is Tested |
|---------|---------------|
| **1. Insertions** | 10 keys; tree structure, all traversals, size, height, black-height, min, max, validity |
| **2. Search** | Found and not-found cases |
| **3. Successor & Predecessor** | Boundary nodes (min, max) and interior nodes |
| **4. Deletions** | Three deletions (root, leaf, one-child node); validity checked after each |
| **5. Edge Cases** | Duplicate insert (no-op), delete non-existent key, drain tree to empty |
| **6. Large Test** | Sequential insert 1–20 (worst-case for plain BST); height and validity verified |

Every section prints `Valid RBT? : YES` confirming that all five properties
hold after each operation.