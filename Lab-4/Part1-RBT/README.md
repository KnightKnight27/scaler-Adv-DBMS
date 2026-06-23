# Lab 4 (Part 1) — Red-Black Tree

> Part 1 of Lab 4 (*Red-Black Tree & Full B-Tree*). The B-Tree is in
> `../Part2-BTree/`.

A CLRS-style red-black tree over `int` keys, split into a header,
implementation, and a small demo driver. Insertion and deletion both use
a single shared **NIL sentinel** so the fix-up routines can read
`x->parent`, `x->left`, and `x->color` without any nullptr guards.

## Layout

```
Lab-4/Part1-RBT/
├── RedBlackTree.h    # class declaration + Node struct + Color enum
├── RedBlackTree.cc   # insert / find / remove / print / fixups
├── main.cc           # demo driver and invariant checks
├── Makefile          # g++ -std=c++17 -Wall -Wextra -O2
└── README.md
```

## The five invariants

A red-black tree maintains a balanced search tree by colouring each node
RED or BLACK and enforcing:

1. **Every node is RED or BLACK.**
2. **The root is BLACK.**
3. **Every NIL leaf is BLACK.** (We use a single shared sentinel for this.)
4. **A RED node never has a RED child.** (No two reds in a row.)
5. **Every path from a node to any descendant NIL contains the same
   number of BLACK nodes.** This count is the node's *black-height*.

Together these guarantee that the longest path from root to leaf is at
most twice the shortest, so all operations run in **O(log n)**.

`RedBlackTree::checkInvariants()` walks the tree recursively, asserts
invariants 4 and 5 at every node, and returns the black-height. `main.cc`
calls it after every mutation as a self-check.

## Public API

```cpp
class RedBlackTree {
public:
    void insert(int key);
    bool find(int key) const;
    bool remove(int key);
    void print() const;          // level-order BFS, with colour tags
    int  checkInvariants() const;
};
```

- `insert` — standard BST insert that paints the new node RED, then calls
  `insertFixup` to restore invariant 4.
- `find` — plain BST search; returns whether the key is present.
- `remove` — CLRS deletion with `transplant` + `deleteFixup`.
- `print` — level-order (BFS) dump with `R`/`B` colour tags per node.
- `checkInvariants` — walks the tree and asserts the colour rules.

## Insert fixup — three cases

A freshly inserted RED node `z` violates invariant 4 only if its parent
is also RED. Let `u` be `z`'s uncle (the sibling of `z->parent`):

| Case | Trigger                              | Fix |
| ---- | ------------------------------------ | --- |
| 1    | `u` is RED                           | recolour parent + uncle BLACK, grandparent RED; restart at grandparent |
| 2    | `u` is BLACK and `z` is the *zigzag* child | rotate around `z->parent` to convert to case 3 |
| 3    | `u` is BLACK and `z` is the *straight-line* child | recolour and rotate around grandparent — done |

Each case has a mirror (left vs right child of grandparent). After the
loop the root is forced BLACK in case the final iteration coloured it
RED.

## Delete fixup — four cases

After splicing out a node, we propagate an "extra black" up from a node
`x`. Let `w` be `x`'s sibling:

| Case | Trigger                                       | Fix |
| ---- | --------------------------------------------- | --- |
| 1    | `w` is RED                                    | rotate `x->parent` so `w` becomes BLACK — falls into 2/3/4 |
| 2    | `w` is BLACK with two BLACK children          | recolour `w` RED, move `x` up; the extra black bubbles to the parent |
| 3    | `w` is BLACK, `w`'s *far* nephew is BLACK     | rotate `w` so the far nephew becomes RED — falls into case 4 |
| 4    | `w` is BLACK, `w`'s *far* nephew is RED       | recolour + rotate around `x->parent`; loop exits |

As with insert, each case has a left/right mirror.

## Complexity

| Operation | Worst case | Notes |
| --------- | ---------- | ----- |
| `find`    | O(log n)   | plain BST descent |
| `insert`  | O(log n)   | BST insert + at most O(log n) recolours, ≤ 2 rotations total |
| `remove`  | O(log n)   | transplant + at most O(log n) recolours, ≤ 3 rotations total |
| `print`   | O(n)       | BFS visits every node once |

Space is O(n) for the nodes plus a single shared NIL sentinel.

## Build & run

```bash
cd Lab-4/Part1-RBT
make           # compiles into ./rbtree
make run       # builds and runs the demo
make clean
```

If you don't have `make` (or `g++`), the equivalent direct invocation is:

```bash
g++ -std=c++17 -Wall -Wextra -O2 main.cc RedBlackTree.cc -o rbtree
./rbtree
```

## Expected demo output (shape)

```
== inserting 15 keys ==

== tree after inserts ==
L0:  25(B)
L1:  17(R)  47(R)
L2:  8(B)  22(B)  41(B)  53(B)
...
black-height = 3

== find ==
find(38) = true
find(100) = false
...

== tree after deletes ==
...
black-height = 3
```

The exact shape depends on the insertion order — what matters is that
every assertion in `checkInvariants` passes after every mutation.
