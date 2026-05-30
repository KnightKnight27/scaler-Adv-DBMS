# Lab 6 — B-Tree Implementation

**Tanishq Singh | 24BCS10303**

---

## What this is

A B-Tree built from scratch in C++17 with a minimum degree `t` that you pick at startup. Supports insert, search, delete, inorder traversal, and level-order (BFS) display. The whole thing is one file — `main.cpp` — with an interactive menu.

---

## Files

```
Lab-6/
├── main.cpp    ← Node struct + BTree class + interactive main()
├── Makefile    ← make / make run / make clean
└── README.md   ← this file
```

---

## Build & Run

```bash
cd Lab-6
make run
```

Or manually:

```bash
g++ -std=c++17 -Wall -Wextra -O2 -o btree main.cpp
./btree
```

Compiles clean — no warnings with `-Wall -Wextra`.

---

## Sample Session

```
Enter minimum degree t (>= 2): 3

(insert 10, 20, 30, 40, 50, 5, 15, 25, 35, 45)

Level-order:
L0:  [30]
L1:  [5, 10, 15, 20, 25]  [35, 40, 45, 50]

Inorder: 5 10 15 20 25 30 35 40 45 50

(delete 30 — key is internal, left child has 5 keys >= t so predecessor 25 replaces it)
L0:  [25]
L1:  [5, 10, 15, 20]  [35, 40, 45, 50]

(delete 10 — straight leaf removal)
L0:  [25]
L1:  [5, 15, 20]  [35, 40, 45, 50]
```

The level-order view shows you the actual tree shape. The inorder always comes out sorted — that's the easiest way to verify the structure is correct.

---

## B-Tree Invariants

For a B-Tree of minimum degree `t >= 2`:

1. Every node stores its keys in sorted order.
2. A non-root node has between `t-1` and `2t-1` keys. The root can have as few as 1.
3. A node with `k` keys has exactly `k+1` children (leaf nodes have zero).
4. All leaves sit at the same depth — the tree is always height-balanced.
5. For an internal node with keys `K1 < K2 < ... < Kk`, the subtree rooted at child `i` contains only keys in `(K_{i-1}, K_i)`.

Height is at most `log_t((n+1)/2)`, so even with a billion keys and `t = 100`, the tree is only about 5 levels deep.

---

## Insert — Preemptive Split

On the way down from root to the target leaf, any full child (one with `2t-1` keys) gets split before we descend into it. That way the leaf always has room when we arrive — no backtracking needed.

### splitChild(parent, i)

Child `ch[i]` of `parent` is full. The median key moves up into `parent`, and the right half of `ch[i]` becomes a new sibling `ch[i+1]`:

```
Before (t=3, child has 5 keys):
    parent [... P ...]
              |
        child [a, b, M, c, d]

After splitChild(parent, i):
    parent [... P, M ...]
              /     \
      child [a, b]  sibling [c, d]
```

The only time the tree grows in height is when the root is full. A new root is created, the old root becomes its only child, and then it gets split immediately. This is why B-trees grow upward — leaves stay at the same level.

---

## Search

Standard: scan keys left-to-right until one is `>= k`. If it equals `k`, return found. Otherwise recurse into the matching child pointer (or return not-found at a leaf).

```
O(t * log_t n) time, O(log_t n) disk reads
```

---

## Delete — Three Cases

Delete is the tricky one. The key rule: **before descending into any child, make sure it has at least `t` keys.** This guarantees that when we remove a key, the node still has `t-1` keys — the invariant holds without any backtracking.

### Case 1 — Key is in a leaf node

Just remove it. The pre-descent fill guarantee means the leaf has at least `t` keys, so after removal it has `t-1` — still valid.

### Case 2 — Key is in an internal node

The key sits between child `L` (left) and child `R` (right):

- **2a** — `L` has `>= t` keys: find the predecessor (the largest key in `L`'s subtree), copy it up to replace the deleted key, then recursively delete the predecessor from `L`.

- **2b** — `R` has `>= t` keys: symmetric — use the successor.

- **2c** — Both `L` and `R` have exactly `t-1` keys: merge them. Pull the separator key down into `L`, append all of `R`'s keys and children to `L`, delete `R`. Now `L` has `2t-1` keys and the deleted key is inside it — recurse into `L`.

### Case 3 — Key is not in this node (need to descend)

Find the child `ch[i]` whose subtree must contain the key. If `ch[i]` only has `t-1` keys, top it up first:

- **Borrow from left sibling** (if it has `>= t` keys): the parent's separator goes into `ch[i]`'s front, the sibling's last key goes up to the parent.

  ```
  Before:
      parent [..., K, ...]
              /        \
    sib [a, b, c]    ch[i] [d, e]

  After rotateRight:
      parent [..., c, ...]
              /        \
      sib [a, b]    ch[i] [K, d, e]
  ```

- **Borrow from right sibling** (if it has `>= t` keys): symmetric rotate in the other direction.

- **Merge**: if both siblings have exactly `t-1` keys, merge `ch[i]` with one sibling, pulling the parent separator down. Now `ch[i]` has `2t-1` keys and the parent has one fewer key and one fewer child.

If a merge causes the root to become empty, the root is discarded and its only child becomes the new root — that's how the tree shrinks in height.

---

## Complexity

| Operation | Time | Disk I/O |
|-----------|------|----------|
| Insert | `O(t * log_t n)` | `O(log_t n)` |
| Search | `O(t * log_t n)` | `O(log_t n)` |
| Delete | `O(t * log_t n)` | `O(log_t n)` |

For `t = 100` and `n = 10^9`: height ≈ 5, each lookup is at most 5 reads. That's why B-trees are used in every major database (Postgres, MySQL/InnoDB, SQLite, Oracle) for on-disk indexes — each node fits one disk page and the tree stays shallow.

---

## Why B-Trees for Databases

A normal BST can degenerate to `O(n)` for sorted inserts. An AVL or RBT stays balanced but nodes hold only one key, so you need `log_2 n` disk reads — at 10^9 keys that's ~30 reads per lookup, each potentially a different page.

A B-tree with `t = 100` puts 199 keys in one node. Every node access is one disk read. Height stays at ~5 for a billion keys. And because the data is sorted within each node and the tree is always balanced, range queries just walk a contiguous chain of pages.

B+ trees (used in Postgres, MySQL) are a variant where only leaf nodes hold records and leaves are linked — that makes range scans even cheaper since you don't have to go back up the tree.
