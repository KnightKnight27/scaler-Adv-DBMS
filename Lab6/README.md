# Lab 6 — B-Tree (C++)

- **Role Number:** 24BCS10345
- **Name:** Ansh Mahajan
- **Date:** 2026-06-22

## Aim

Implement a B-Tree in C++ with insertion, search and traversal, exposed through
an interactive menu, and understand how a balanced multi-way search tree keeps
all leaves at the same depth by splitting full nodes automatically.

## B-Tree rules (minimum degree `t`)

For a B-tree of minimum degree `t` (here `t = 3`):

- Every node (except the root) holds between `t-1` and `2t-1` keys — so 2 to 5
  keys per node in this build.
- An internal node with `k` keys has exactly `k+1` children.
- Keys within a node are kept sorted; a search or insert chooses the child
  whose key range brackets the target.
- **All leaves are at the same depth.** The tree only ever gets taller by
  splitting the root, which is why it stays balanced for free.

## How insertion stays balanced (proactive splitting)

This implementation uses the top-down scheme from CLRS:

1. If the **root** is full (`2t-1` keys), create a new empty root above it and
   split the old root in two. This is the *only* operation that increases the
   height, and it does so for the whole tree at once — so every leaf stays on
   the same level.
2. Descending toward the correct leaf, **before** stepping into any child that
   is already full, split it: its median key is promoted into the parent and
   its right half becomes a new sibling (`splitChild`).
3. Because the child we descend into is therefore never full, the actual insert
   into the leaf (`insertNonFull`) can never overflow and never has to back up.

`search` is the natural recursion: scan the sorted keys of a node, return on an
exact match, otherwise descend into the bracketing child (or report "not found"
at a leaf). `inorder` interleaves children and keys to yield a fully sorted
sequence.

## Beyond the requirement: level view

`printByLevel()` does a breadth-first dump with one tree level per line and each
node shown as `[k1 k2 ...]`. This makes the median promotions and the
same-depth-leaves property visible directly from the output.

## Build and run

```bash
# Direct (as specified)
g++ -std=c++17 btree.cpp -o btree
./btree

# Or with CMake
cmake -S . -B build && cmake --build build
./build/btree_lab6
```

The program is menu-driven:

```text
  1) Insert key
  2) Search key
  3) Show inorder traversal
  4) Show tree structure (by level)
  5) Exit
```

## Sample run

Inserting `50 30 70 10 20 60 80 5 40 90` (the order that forces two splits),
then traversing and searching:

```text
Inorder: 5, 10, 20, 30, 40, 50, 60, 70, 80, 90
Structure:
  L0: [30 60]
  L1: [5 10 20] [40 50] [70 80 90]
Found 40.
Not found 99.
```

Reading the result: the first split promoted `50`'s median when the root
filled, and subsequent inserts split again so the medians `30` and `60` rose
into the root. The traversal is fully sorted, both leaves sit on level 1 (equal
depth), and every node holds between 2 and 5 keys — exactly the B-tree
invariants.

## Files

| File | Purpose |
| --- | --- |
| [btree.cpp](btree.cpp) | Templated `BTree<T>` + menu-driven driver |
| [CMakeLists.txt](CMakeLists.txt) | CMake build (C++17, warnings on) |
| `README.md` | This write-up |
