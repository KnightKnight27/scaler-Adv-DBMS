# Lab 5 — Red-Black Tree

**Author:** Bibek Jyoti Charah · **Roll No:** 24bcs10112

A self-balancing binary search tree supporting **insert**, **search**, and
**delete**, all in `O(log n)`. The implementation is sentinel-based: a single
shared black `nil` node stands in for every empty child and for the root's
parent, so rotations and the delete fixup don't need special null handling.

## Red-black invariants

1. Every node is red or black.
2. The root is black.
3. A red node never has a red child.
4. Every root-to-`nil` path passes through the same number of black nodes
   (the *black-height*).

Insertion adds a red node and repairs rule 3 by recolouring / rotating up the
tree. Deletion splices the node (or its in-order successor) and, when a black
node is removed, runs `eraseFixup` to restore the black-height. `valid()`
re-checks rules 2–4 after every operation in the demo.

## Build & run

```bash
g++ -std=c++17 -Wall -Wextra -O2 -o rbtree Red_Black_Tree.cpp
./rbtree
```

The demo inserts a fixed key set, prints the in-order traversal with colours
(`(R)` / `(B)`), runs a couple of lookups, then deletes several keys —
verifying the tree stays a valid red-black tree after each delete.
