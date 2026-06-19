# Lab 5 - B-Tree & Red-Black Tree

**Tanishq | 24BCS10303**

---

## What this is

Two self-balancing index structures implemented in C++: a B-Tree (the disk-oriented multi-way tree that SQLite and most databases actually use) and a Red-Black Tree (a self-balancing BST). Both come with an interactive CLI so you can insert values, search, and visualize the structure as it changes.

---

## How to build & run

```bash
# with cmake (if you have it)
cmake -S . -B build && cmake --build build
./build/trees

# or just directly
g++ -std=c++20 main.cpp btree.cpp rbt.cpp -o trees
./trees
```

Tested on macOS with Apple Clang. No external deps.

---

## File layout

```
Assignment-5/
├── btree.hpp      <- BTree / BTreeNode class declarations
├── btree.cpp      <- B-Tree implementation
├── rbt.hpp        <- RedBlackTree / RBTNode declarations
├── rbt.cpp        <- Red-Black Tree implementation
├── main.cpp       <- interactive CLI driver
├── CMakeLists.txt <- build config
└── README.md      <- this file
```

---

## B-Tree

### What it is

A B-Tree of minimum degree `t`. Each internal node holds between `t-1` and `2t-1` keys, and has between `t` and `2t` children. When a node fills up (hits `2t-1` keys), it gets split before a new key is inserted — the split happens on the way _down_ the tree, so we never have to backtrack.

The default `t=3` in main means nodes can hold 2 to 5 keys. You can pass any `t` when constructing the tree.

### Insertion

```
BTree bt(3);
bt.insert(10);
bt.insert(20);
// ...
```

Insert starts at the root. If the root is full, a new root is created and the old root is split. After that, we descend into whichever child subtree should receive the key, splitting any full nodes we pass through. When we hit a leaf, the key gets shifted into its sorted position.

The key part of the assignment — the **insertion slot search** — is in `insertNonFull`. For a leaf node, it scans right-to-left through the current keys, shifting each one right until it finds the right slot. For an internal node, it does the same scan to pick which child pointer to follow.

### Search

Standard B-Tree search: at each node scan left-to-right comparing keys, go into the right child subtree if the key isn't in the current node.

### Sample output (t=3, after inserting 10,20,5,6,12,30,7,17,3,25,40,50,22)

```
└── [17]
    ├── [25, 40]
    │   ├── [50]
    │   ├── [30]
    │   └── [22]
    └── [6, 10]
        ├── [12]
        ├── [7]
        ├── [5]
        └── [3]
```

---

## Red-Black Tree

### Rules

1. Every node is either red or black.
2. Root is always black.
3. No two red nodes appear consecutively on any root-to-leaf path.
4. Every path from root to a null leaf has the same number of black nodes (black height).

### Sentinel NIL node

I used a sentinel `NIL` node (always black) instead of actual null pointers. Every leaf's children point to `NIL`, and NIL's parent also points to itself. This makes rotation code cleaner since you never have to null-check before reading a node's color.

### Insertion fixup

After a standard BST insert (new node colored RED), we might have a red-red violation where the new node and its parent are both red. The fixup walks back up the tree fixing violations:

- **Case 1 — uncle is red**: recolor parent + uncle to BLACK, grandparent to RED, move `z` up to grandparent. Might push the problem higher.
- **Case 2 — uncle is black, z is an inner child**: rotate at parent to convert to Case 3.
- **Case 3 — uncle is black, z is outer child**: recolor + rotate at grandparent. Terminates.

Both cases have a left-side and right-side mirror version depending on whether the parent is a left or right child.

### Sample output (after inserting 10,20,30,15,25,5,1,35,8)

Nodes print with [R] for red and [B] for black:

```
└── 20 [B]
    ├── 30 [B]
    │   └── 35 [R]
    │   └── 25 [R]
    └── 10 [R]
        ├── 15 [B]
        └── 5 [B]
            ├── 8 [R]
            └── 1 [R]
```

### Balance checks

The stats menu (option 4) shows:
- **height**: total tree height
- **black-height**: count of black nodes on any root-to-leaf path
- **RB-balanced**: verifies no red-red violations and uniform black height
- **AVL-balanced**: checks if height difference between siblings is ≤1. RBTs don't guarantee this — it's expected to say "no" on larger trees.

---

## Why these two together

B-Trees and Red-Black Trees solve similar problems from different angles. A B-Tree is wide and short — designed for disk pages where loading a full node at once is cheap. An RBT is a binary tree — designed for in-memory indexing where pointer chasing is fine. Both guarantee O(log n) insert and search. PostgreSQL uses a variant of B-Tree for all its indexes. Many language runtimes (like Java's `TreeMap` and Linux's kernel process scheduling) use RBTs.

---

## Interactive menu

When you run `./trees`, both structures come pre-seeded with some values. You pick a structure, then get options to insert, search, print the tree visually, or check balance stats.
