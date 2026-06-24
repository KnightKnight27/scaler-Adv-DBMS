# Lab 6 — B-Tree in C++17

A from-scratch implementation of a **B-Tree** — the workhorse data structure of every major relational database's on-disk index (Postgres, MySQL, SQLite, Oracle). Unlike a binary tree, a B-tree node holds up to `2t-1` keys and has up to `2t` children, which keeps the tree shallow and cache-friendly even for millions of keys.

This lab supports the four core operations — **insert, search, delete, display** — through an interactive menu.

---

## Table of Contents
1. [Files](#files)
2. [Build & Run](#build--run)
3. [Sample Session](#sample-session)
4. [The B-Tree Invariants](#the-b-tree-invariants)
5. [Insertion — Preemptive Split](#insertion--preemptive-split)
6. [Search](#search)
7. [Deletion — Three Cases](#deletion--three-cases)
8. [Display](#display)
9. [Complexity](#complexity)
10. [Why B-Trees Matter for Databases](#why-b-trees-matter-for-databases)

---

## Files

| File | Purpose |
|------|---------|
| `main.cpp` | Full implementation: `Node` struct, `BTree` class, interactive `main()`. |
| `Makefile` | `make` to build, `make run` to run, `make clean` to remove the binary. |
| `README.md` | This document. |

---

## Build & Run

```bash
make          # builds ./btree
make run      # builds and runs the interactive menu
make clean    # removes the binary
```

Manual build:
```bash
g++ -std=c++17 -Wall -Wextra -O2 -o btree main.cpp
./btree
```

Compiles cleanly with **no warnings** under `-Wall -Wextra`.

The program first asks for the **minimum degree `t`** (must be ≥ 2). Each non-root node will then hold between `t-1` and `2t-1` keys, and have between `t` and `2t` children.

---

## Sample Session

With `t = 3` and inserting `10, 20, 30, 40, 50, 5, 15, 25, 35, 45`, the program prints the tree after each operation. The final structure:

```
B-Tree (level-order):
L0: [30]
L1: [5, 10, 15, 20, 25]  [35, 40, 45, 50]

B-Tree (inorder): 5 10 15 20 25 30 35 40 45 50

(delete 30 — predecessor 25 is promoted)
L0: [25]
L1: [5, 10, 15, 20]  [35, 40, 45, 50]

(delete 10 — straight leaf removal)
L0: [25]
L1: [5, 15, 20]  [35, 40, 45, 50]
```

The level-order view shows the tree by depth; the inorder view shows the keys in sorted order — both confirm the structure is a valid B-tree.

---

## The B-Tree Invariants

For a B-tree of minimum degree `t ≥ 2`:

1. Every node holds keys in **sorted order**.
2. A non-root node has between `t-1` and `2t-1` keys. The **root** may have as few as 1 key.
3. A node with `k` keys has exactly `k+1` children (or is a leaf).
4. All **leaves are at the same depth** — the tree is perfectly balanced.
5. For an internal node with keys `K₁ < K₂ < ... < Kₖ`, the i-th child's keys all lie in the open interval `(Kᵢ₋₁, Kᵢ)`.

These invariants together force the tree's height to be at most `log_t((n+1)/2)`.

---

## Insertion — Preemptive Split

The algorithm walks the tree top-down. **Any full child encountered on the way is split before descending into it.** This guarantees that whenever we reach a leaf, it has room for the new key — no upward rebalancing is needed.

### `splitChild(parent, idx)`

When a child has exactly `2t-1` keys, splitting it produces two siblings of `t-1` keys each, and promotes the **median** key up into the parent.

```
Before split (t=3, child is full with 5 keys):

         parent  [ ... K ... ]
                       |
              child [a, b, M, c, d]

After splitChild(parent, idx):

         parent  [ ... K, M ... ]
                      / \
            child [a, b]   sibling [c, d]
```

The median `M` becomes a new key in the parent. Because the parent itself was guaranteed non-full before the call (otherwise it would have been split on the descent), this is always safe.

### Root growth

The only way the tree gets taller is at the **root**:

```cpp
if (root is full) {
    Node* s = new internal node;
    s->kids.push_back(old root);
    splitChild(s, 0);
    root = s;
}
```

So a B-tree grows **upward**, never downward, which is what keeps all leaves at the same depth.

---

## Search

Standard for a sorted node + recursive descent: scan the keys until one is `≥ target`. If it equals the target, return true; otherwise recurse into the corresponding child (or return false if we're at a leaf).

```cpp
bool search(Node* node, int key) {
    int i = 0;
    while (i < node->keys.size() && key > node->keys[i]) ++i;
    if (i < node->keys.size() && node->keys[i] == key) return true;
    if (node->isLeaf) return false;
    return search(node->kids[i], key);
}
```

---

## Deletion — Three Cases

Deletion is the trickiest B-tree operation. The recursive descent follows a careful rule: **before descending into a child, ensure that child has at least `t` keys.** This way, when we finally remove a key, the local node is "safe" (has at least `t-1` keys remaining).

### Case 1 — Key is in this node, and this node is a leaf
Just erase the key. No structural changes.

### Case 2 — Key is in this node, and this node is internal

Let the key sit between children `Y` (left) and `Z` (right).

- **Case 2a** — If `Y` has at least `t` keys: find the **predecessor** (max of `Y`'s subtree), copy it into the current node in place of the deleted key, then recursively delete the predecessor from `Y`.
- **Case 2b** — Else if `Z` has at least `t` keys: symmetric, using the **successor**.
- **Case 2c** — Else (both `Y` and `Z` have exactly `t-1` keys): **merge** them (and the separator key) into a single child with `2t-1` keys, then recurse into that merged child to delete the key.

### Case 3 — Key is not in this node (descend into a child)

Pick the child `X_i` whose subtree must contain the key. If `X_i` has only `t-1` keys, top it up first:

- **Case 3a — Borrow:** If a neighbouring sibling has ≥ `t` keys, rotate one key through the parent into `X_i`.

  ```
  Borrow-from-prev (idx = i):
       parent ... K_i-1 ...
              /         \
       prev [..., a, b]   X_i [c, d]

       parent ... b ...
              /     \
        prev [..., a]   X_i [K_i-1, c, d]
  ```

- **Case 3b — Merge:** If both siblings have only `t-1` keys, merge `X_i` with one sibling (pulling the parent's separator down).

After the fill, descend.

This guarantees that any node we ever delete a key from has at least `t-1 + 1 = t` keys before the delete, so the invariants are preserved without backtracking.

If the **root** loses its only key during a recursive merge, we discard the empty root and promote its sole child — that's how the tree gets shorter.

---

## Display

Two views are provided:

- **Inorder (`display`)** — recursively interleaves traversal into children with printing of each key. Produces the keys in sorted order. Useful to verify correctness.
- **Level-order (`displayLevels`)** — BFS over the tree, grouping nodes by depth. Useful to see the actual structure: which keys are siblings, which are on which level, and whether the tree is shallow.

---

## Complexity

For a B-tree of minimum degree `t` holding `n` keys, the height is bounded by `h ≤ log_t((n+1)/2)`. Each operation touches `O(h)` nodes; each node visit does `O(t)` work (linear scan over the keys).

| Operation | Time | Disk I/O (assuming one node = one page) |
|-----------|------|---|
| `search` | `O(t · log_t n)` | `O(log_t n)` reads |
| `insert` | `O(t · log_t n)` | `O(log_t n)` reads + writes |
| `delete` | `O(t · log_t n)` | `O(log_t n)` reads + writes |

For typical database values (e.g. `t ≈ 100` giving 200-way branching), even a billion keys fit in a tree of height ~5.

---

## Why B-Trees Matter for Databases

B-trees became the dominant on-disk index structure precisely because of invariant 4 and the wide branching factor:

- **Shallow trees:** even at scale, a search visits only a handful of nodes. For a B-tree with `t = 100` and a billion keys, height ≤ 5 — so any lookup is at most 5 disk reads.
- **Page-aligned nodes:** each node is sized to one disk page (typically 4 KB or 8 KB), so every node touch is exactly one I/O. The internal `O(t)` scan happens in fast memory.
- **Balanced by construction:** because all leaves stay at the same depth, query latency is predictable — no degenerate `O(n)` worst case like an unbalanced BST.
- **Range scans are cheap:** an inorder walk visits each key once, and pages are read sequentially.

PostgreSQL, MySQL (InnoDB), SQLite, Oracle, SQL Server — all default to B-trees (or the closely related B⁺-tree) for primary and secondary indexes.
