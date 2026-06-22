# Lab 5 — Red-Black Tree (C++)

- **Role Number:** 24BCS10345
- **Name:** Ansh Mahajan
- **Date:** 2026-06-22

## Aim

Implement a Red-Black Tree in C++ and understand how a self-balancing binary
search tree keeps its height logarithmic through colouring and rotations.

## The four invariants

A red-black tree is a BST in which every node carries a colour and the
following properties hold at all times:

1. Every node is red or black.
2. The root is black.
3. A red node never has a red child (no two reds on any path).
4. Every path from a node down to a `NIL` leaf crosses the same number of
   black nodes (its **black height**).

Properties 3 and 4 together force the longest root-to-leaf path to be at most
twice the shortest, so the height stays `<= 2*log2(n+1)` and every operation is
`O(log n)`.

## Design notes

- **Single sentinel.** Instead of using `nullptr` for empty leaves, the tree
  keeps one shared black `nil_` node. Every leaf and the root's parent point at
  it. This removes the null checks from the rebalancing cases — an uncle that
  doesn't exist is simply the (black) sentinel.
- **New nodes are red.** Inserting red can only ever break property 3, never
  property 4, which keeps the fix-up to a small set of cases.
- **`insertFixup` pushes the violation upward.** Three cases (and their mirror
  images) handle a red node whose parent is also red:
  - *Case 1 — red uncle:* recolour parent, uncle, and grandparent, then
    continue from the grandparent (the only case that loops).
  - *Case 2 — black uncle, "triangle":* rotate the parent to straighten the
    triangle into a line, reducing to Case 3.
  - *Case 3 — black uncle, "line":* recolour and rotate the grandparent,
    after which the tree is valid and the loop ends.
  - Finally the root is painted black to guarantee property 2.
- **Rotations** (`rotateLeft` / `rotateRight`) are the standard pointer
  surgery that re-parents a node and its child while preserving in-order
  sequence.

## Beyond the requirement: self-checking

`validate()` re-derives all four properties from the finished tree (root is
black, no red node has a red child, and `blackHeight()` returns a single
consistent value for every path). The driver runs it after all inserts, so the
program *proves* the result is a valid red-black tree rather than asking the
reader to trust the fix-up logic.

## Build and run

```bash
# Direct (as specified)
g++ -std=c++17 red_black_tree.cpp -o rbt
./rbt

# Or with CMake
cmake -S . -B build && cmake --build build
./build/red_black_tree_lab5
```

## Sample run

Insert sequence: `41 38 31 12 19 8 27 50 45 5 33`

```text
Inorder (sorted, with R/B colour):
  5(R), 8(B), 12(R), 19(R), 27(R), 31(B), 33(R), 38(B), 41(R), 45(B), 50(R)

Tree structure (rotate 90 clockwise to read):
        50(R)
    45(B)
        41(R)
38(B)
            33(R)
        31(B)
            27(R)
    19(R)
            12(R)
        8(B)
            5(R)

Property check after all inserts:
  [OK] all four red-black properties hold
```

The in-order traversal is fully sorted (BST property), the root `38` is black,
no red node sits directly above another red node, and every root-to-`NIL` path
passes through the same number of black nodes — all four invariants verified
programmatically.

## Files

| File | Purpose |
| --- | --- |
| [red_black_tree.cpp](red_black_tree.cpp) | Templated `RedBlackTree<T>` + driver |
| [CMakeLists.txt](CMakeLists.txt) | CMake build (C++17, warnings on) |
| `README.md` | This write-up |
