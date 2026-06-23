# Lab 6 — B-Tree in C++

An interactive B-tree supporting insertion, search, and inorder traversal.
Insertion uses the standard top-down preemptive-split approach: any full
child on the descent path is split before recursing, and the root grows
upward only when it is itself full.

## Build

```bash
g++ main.cpp -o main
```

## Run

```bash
./main
```

The program first asks for the minimum degree `t` (must be at least 2).
Each non-root node then holds between `t-1` and `2t-1` keys.

## Menu

1. Insert a key
2. Search for a key
3. Display the tree in inorder
4. Quit

## Operations

- **Insert** — descends from the root, splitting any full child before
  going into it, so insertion at the leaf is always safe.
- **Search** — at each node, walks the keys until one is `>=` the target,
  then either matches it or recurses into the corresponding child.
- **Inorder display** — interleaves recursion into the children with
  printing of the keys, producing the keys in sorted order.
