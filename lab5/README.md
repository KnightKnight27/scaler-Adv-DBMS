# Lab 5 : Red-Black Tree Implementation

## Student Details
- **Name:** Romit Raj Sahu
- **Roll Number:** 24BCS10436

---

## Objective

Implement a Red-Black Tree from scratch in C++17 and verify that all four Red-Black invariants hold after every insertion and deletion. A Red-Black Tree is a self-balancing Binary Search Tree : it automatically restructures itself after modifications to guarantee O(log N) performance regardless of the order values are inserted.

---

## Files

| File | Purpose |
|------|---------|
| `RedBlackTree.h` | Node struct, Color enum, class declaration with public API and private helpers |
| `RedBlackTree.cpp` | Full implementation: insert + 4-case fix-up, delete + fix-up, rotations, traversals, validation |
| `main.cpp` | Demo driver exercising insert, search, and remove |
| `Makefile` | `make` to build, `make run` to build and run, `make clean` to remove binaries |

---

## Build and Run

```bash
make run
```

Or manually:
```bash
g++ -std=c++17 -Wall -Wextra -O2 RedBlackTree.cpp main.cpp -o red_black_tree
./red_black_tree
```

Compiles cleanly with zero warnings under `-Wall -Wextra`.

---

## Background

### The Problem with an Unbalanced BST

A regular Binary Search Tree is fast when balanced : searching takes O(log N) steps because you eliminate half the remaining nodes at each level. But if values are inserted in sorted order, the tree degenerates into a straight line:

```
Insert 1, 2, 3, 4, 5:

1
 \
  2
   \
    3
     \
      4
```

Searching this is O(N) : no better than a plain list. The Red-Black Tree solves this by enforcing balance automatically on every insert and delete.

---

## Red-Black Invariants

Every node holds a color (RED or BLACK). The following four rules are maintained at all times:

| Rule | Invariant |
|------|-----------|
| 1 | Every node is RED or BLACK |
| 2 | The root is always BLACK |
| 3 | A RED node cannot have a RED child (no two consecutive reds on any path) |
| 4 | Every path from the root to a null position passes through the same number of BLACK nodes (equal black-height) |

Rules 3 and 4 together guarantee that the longest possible path in the tree (alternating RED-BLACK) is at most twice the shortest path (all BLACK). This bounds the tree height to 2 × log₂(N+1).

---

## Rotations

Rotations are O(1) local restructuring operations that change the shape of the tree without breaking BST ordering (left < node < right). They are the building blocks used inside both fix-up routines.

**Left Rotation** : the right child takes the current node's place:
```
    x                  y
   / \                / \
  A   y     →        x   C
     / \            / \
    B   C          A   B
```

**Right Rotation** : the left child takes the current node's place:
```
      y              x
     / \            / \
    x   C   →      A   y
   / \                / \
  A   B              B   C
```

---

## Insertion

### Step 1 : Normal BST Insert
Walk the tree left/right using the BST rule and place the new node in the correct position. Color it RED.

### Step 2 : fixInsert (4 cases)

After inserting, Rule 3 may be broken if the new RED node's parent is also RED. The fix-up handles this by walking up the tree. Let **z** = the new node, **P** = its parent, **G** = its grandparent, **U** = its uncle (sibling of P).

---

**Case 1 : Uncle is RED**

Just recolor: P and U become BLACK, G becomes RED. Then move z up to G and repeat the check.

```
Before:              After:
      G(B)                 G(R)   ← check here next
     /    \               /    \
   P(R)  U(R)           P(B)  U(B)
   /                    /
  z(R)                 z(R)
```

---

**Case 2 : Uncle is BLACK, z is an "inner" child (triangle shape)**

Rotate P toward G to turn the triangle into a straight line (Case 3). No recoloring yet.

```
Before:          After rotation:
    G                  G
   /                  /
  P(R)              z(R)
   \                /
    z(R)          P(R)
```

---

**Case 3 : Uncle is BLACK, z is an "outer" child (straight line)**

Rotate G away from z, then swap colors of P and G.

```
Before:          After:
    G(R)             P(B)
   /                /    \
  P(B)            z(R)   G(R)
  /
z(R)
```

Cases 2 and 3 have mirror images for when P is the right child of G.

After all cases, Rule 2 is enforced by setting the root to BLACK.

---

## Deletion

Deletion uses the standard BST remove (using in-order successor when a node has two children), followed by `fixDelete` if the removed node was BLACK. Removing a BLACK node may create a black-height violation on some path, which `fixDelete` resolves using four symmetric cases involving the sibling of the replacement node.

---

## Sample Output

```
── Inserting values ──

Insert 10
Inorder : 10(B)
Valid : YES

Insert 30
Inorder : 10(R) 20(B) 30(R)
Valid : YES

...

── Final tree (level order) ──
  Level 0: 20(B)
  Level 1: 10(R) 30(B)
  Level 2: 5(B) 15(B) 25(R)
  Level 3: 1(R) 7(R) 12(R) 18(R)

── Search tests ──
  Search 10 : FOUND
  Search 7  : FOUND
  Search 99 : NOT FOUND

── Removing values: 10, 20, 5 ──

Remove 10
Inorder : 1(R) 5(B) 7(R) 12(R) 15(B) 18(R) 20(B) 25(R) 30(B)
Valid : YES

── Tree after removals (level order) ──
  Level 0: 25(B)
  Level 1: 12(R) 30(B)
  Level 2: 7(B) 15(B)
  Level 3: 1(R) 18(R)
```

The inorder output is always sorted. `Valid: YES` after every operation confirms all four invariants hold throughout.

---

## Complexity

| Operation | Time Complexity | Space Complexity |
|-----------|----------------|-----------------|
| Insert | O(log N) | O(1) extra |
| Delete | O(log N) | O(1) extra |
| Search | O(log N) | O(1) extra |
| Rotation | O(1) | O(1) extra |

The tree height is bounded at 2 × log₂(N+1), guaranteeing logarithmic operations regardless of insertion order.

---

## Connection to Real Systems

`std::map` in C++ and `TreeMap` in Java are both implemented using Red-Black Trees internally. This lab is also direct preparation for B-Trees : the index structure used by databases to store and look up rows on disk : which extend the same self-balancing principles to nodes that hold multiple keys per page.