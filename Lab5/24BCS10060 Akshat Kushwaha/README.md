# Lab 5 — Red-Black Tree

**Name:** Akshat Kushwaha
**Roll No:** 24BCS10060

## What this lab is about

A red-black tree is a binary search tree that keeps itself balanced, so it never
turns into a long thin chain. Databases use balanced trees like this for
in-memory ordered structures (C++'s `std::map` is a red-black tree underneath).
This lab is my implementation of one with insert, search, in-order print, and a
self-check that all the balancing rules still hold.

## Files

| File | What it does |
|---|---|
| `rb_tree.cpp` | `RBTree` class (insert / contains / sorted print / validity check) + demo |

## Build & run

```bash
g++ -std=c++17 -Wall -Wextra rb_tree.cpp -o rb_tree
./rb_tree
```

Output:

```
inserting: 50 30 70 20 40 60 80 10 25 35

in-order (should be sorted, R=red B=black):
10(R) 20(B) 25(R) 30(R) 35(R) 40(B) 50(B) 60(R) 70(B) 80(R)

searches:
  contains(40) = yes
  contains(99) = no
  contains(10) = yes
  contains(55) = no

all red-black rules hold? yes
```

## The four rules

Every node is coloured red or black, and the tree always keeps these true:

1. Every node is red or black.
2. The **root** is black.
3. A **red node cannot have a red child** (no two reds in a row).
4. Every path from a node down to a `NIL` leaf has the **same number of black
   nodes**.

Rules 3 and 4 together stop one side from getting much deeper than the other, so
the height stays about `2·log₂(n)`. That's what guarantees insert and search are
**O(log n)**.

## A design choice: the NIL sentinel

Instead of using `nullptr` for the leaves, I made one shared black node called
`nil_` and pointed every "empty" child and the parent-of-root at it. This is the
style from the CLRS textbook. The benefit is that the fix-up code never has to
write `if (node != nullptr)` everywhere — `nil_` is a real black node, so colour
checks on it just work.

## How insert keeps the tree balanced

1. Insert the key like a normal BST and colour the new node **red** (red is the
   "safe" colour because it doesn't change any path's black count).
2. The only rule a new red node can break is rule 3 (red parent + red child). The
   `fix_after_insert` function repairs it with three cases:
   - **Case 1 — the uncle is red:** recolour the parent and uncle black and the
     grandparent red, then move up and re-check.
   - **Case 2 — uncle black, the node is on the "inner" side:** rotate the parent
     so it becomes a straight line (turns into case 3).
   - **Case 3 — uncle black, straight line:** recolour and rotate the
     grandparent. Done.
3. Finally force the root black.

The rotations (`rotate_left` / `rotate_right`) are the local moves that re-shape
three nodes without breaking the sorted order.

## The validity checker

`valid()` walks the whole tree and confirms all four rules at once: the root is
black, no red node has a red child, and every subtree has equal black-height on
both sides. The demo prints `yes`, which is my proof the balancing actually
worked after all those inserts.

## Key takeaways

- A red-black tree stays balanced using just one colour bit per node plus
  rotations — no need to store heights.
- Inserting a node red and then fixing up is easier than trying to insert it with
  the right colour directly.
- The NIL sentinel removes a lot of null-pointer special cases.
- This is an *in-memory* balanced tree; for *on-disk* indexes databases prefer
  B-Trees (Lab 6) because those pack many keys per node and stay much shorter.
