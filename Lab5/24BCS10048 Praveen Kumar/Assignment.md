# Red-Black Tree (CLRS Chapter 13)

**Course:** Advanced DBMS (Scaler)
**Author:** Praveen Kumar 24bcs10048
**Date:** 2026-05-25

---

## 1. Objective

Implement a red-black tree in C++ following the CLRS algorithm (chapter 13). The tree stores `int -> string` key-value pairs and supports insert, erase, search, and in-order traversal. A built-in invariant checker verifies all five red-black properties after every mutation in the demo.

---

## 2. Build and Run

```bash
g++ -std=c++17 -O2 -o rb_tree rb_tree.cpp
./rb_tree
```

The program runs 6 phases: bulk insert, in-order traversal, search, erase, post-erase traversal, and key overwrite. Invariants are checked after every mutation.

---

## 3. Why Red-Black Trees Matter in a DBMS

A balanced BST provides O(log n) insert, delete, and search. Database engines use them in several places where a B-tree would be overkill (B-trees are for disk; red-black trees are for RAM):

- **PostgreSQL lock manager** (`src/backend/storage/lmgr/`) uses RB-trees to track waiting processes per lock.
- **C++ STL** `std::map` and `std::set` are red-black trees in every major implementation.
- **In-memory indexes** in analytical databases use RB-trees or AVL trees for sorted key access.
- **Query planner cost structures** use ordered containers to track candidate paths by estimated cost.

The core advantage over a plain BST: sorted input (e.g., inserting rows in primary key order) degrades a plain BST to O(n). A red-black tree guarantees O(log n) worst case regardless of insertion order.

---

## 4. The Five Red-Black Properties

| # | Property | Why it matters |
|---|----------|---------------|
| 1 | Every node is RED or BLACK | Defines the invariant space |
| 2 | The root is BLACK | Ensures the tree starts with a black anchor |
| 3 | Every NIL leaf is BLACK | NIL nodes count in black-height calculations |
| 4 | No RED node has a RED child | Prevents two consecutive reds shortening a path |
| 5 | Every root-to-NIL path has the same number of BLACK nodes | Ensures the tree is balanced |

Properties 4 and 5 together imply that the longest root-to-leaf path is at most twice the shortest. This gives the height bound of `2 log2(n+1)` and ensures O(log n) for all operations.

---

## 5. Data Structures

```cpp
enum Colour { RED, BLACK };

struct Node {
    int         key;
    std::string value;
    Colour      colour;
    Node       *left, *right, *parent;
};
```

A single NIL sentinel node (always BLACK) is shared by all leaves. This eliminates special-casing for null pointers in every rotation and fix-up.

---

## 6. Insert Fix-up (3 cases + mirrors)

New nodes are always inserted as RED to preserve property 5 immediately. The only invariant that can break is property 4 (two consecutive reds). `fix_insert` walks up the tree fixing violations:

| Case | Trigger | Action |
|------|---------|--------|
| 1 | Parent and uncle are both RED | Recolour both BLACK, grandparent RED, recurse on grandparent |
| 2 | Uncle BLACK, z is inner grandchild (zigzag) | Rotate around parent to convert to Case 3 |
| 3 | Uncle BLACK, z is outer grandchild | Recolour parent BLACK + grandparent RED, rotate around grandparent |

After the loop the root is forced BLACK (property 2).

### Case 1 diagram

```
       G (BLACK)                G (RED)          <- problem moved up
      / \          case 1      / \
     P   U (RED)   ----->     P   U
    (RED)                   (BLACK)(BLACK)
   /
  z (RED)
```

### Cases 2 and 3 diagram (z is left child of right parent)

```
        G                   G                   P
       / \    case 2       / \    case 3        / \
      P   U   ------->    z   U   ------->    z   G
       \                 /                         \
        z               P                          U
```

---

## 7. Delete Fix-up (4 cases + mirrors)

Deletion is more complex. The splice removes a node and may reduce the black-height on one path, creating a "double-black" deficit at the replacement slot. `fix_erase` removes this deficit:

| Case | Trigger | Action |
|------|---------|--------|
| 1 | Sibling is RED | Rotate to make sibling BLACK, reduce to cases 2-4 |
| 2 | Sibling BLACK with two BLACK children | Recolour sibling RED, push deficit to parent |
| 3 | Sibling BLACK, outer nephew BLACK, inner nephew RED | Rotate sibling to convert to Case 4 |
| 4 | Sibling BLACK, outer nephew RED | Recolour and rotate around parent. Done. |

The key insight: Case 4 terminates the loop. Cases 1-3 may cascade upward but at most O(log n) times.

### Delete overview

```
If z has 0 or 1 real child:
    Graft the one real child (or NIL) in z's place.

If z has 2 real children:
    Find z's in-order successor y (leftmost of z's right subtree).
    Graft y's right child in y's place.
    Put y where z was and give y z's colour.
    Fix-up starts at the slot y's right child now occupies.
```

---

## 8. Rotations

Only two operations restructure the tree. Both preserve the BST in-order property and only change six pointers.

```
rotate_left(x):                    rotate_right(y):

      x              y                   y              x
     / \            / \                 / \            / \
    a   y   -->    x   c              x   c  -->      a   y
       / \        / \                / \                  / \
      b   c      a   b              a   b                b   c
```

---

## 9. Invariant Checker

`check_invariants()` does a single O(n) recursive pass and checks:

- Property 2: root is BLACK
- Property 4: no RED node has a RED child (checked at every node)
- Property 5: all root-to-NIL paths accumulate the same black count (tracked and compared)

The demo calls this after every insert and erase. If any rotation or recolouring is wrong, the next call fails immediately with a description of the violated property and the key involved.

---

## 10. Complexity

| Operation | Time | Space (extra) |
|-----------|------|--------------|
| `insert` | O(log n) | O(1) -- one new node |
| `erase` | O(log n) | O(1) |
| `search` | O(log n) | O(1) |
| `in_order` | O(n) | O(log n) call stack |
| `check_invariants` | O(n) | O(log n) call stack |

The O(log n) bound holds because properties 4 and 5 together ensure tree height <= `2 log2(n+1)`. Insert fix-up does at most O(log n) recolourings and 2 rotations. Delete fix-up does at most O(log n) recolourings and 3 rotations.

---

## 11. Sample Output

```
============================================================
  Red-Black Tree (CLRS chapter 13)
============================================================

[PHASE 1] Insert
------------------------------------------------------------
  insert(41, "Database Internals")
  insert(38, "DDIA")
  insert(31, "OSTEP")
  insert(12, "CLRS")
  insert(19, "Clean Code")
  insert(8, "Linux Programming Interface")
  insert(55, "TCP/IP Illustrated")
  insert(45, "C Programming Language")
  insert(63, "Modern Operating Systems")
  insert(74, "Computer Networks")
  insert(25, "Pragmatic Programmer")
  insert(17, "Compilers (Dragon Book)")
  insert(3, "SICP")
  insert(50, "UNIX Network Programming")
  insert(60, "CS:APP")
  size = 15  [invariants OK]

[PHASE 2] In-order traversal (should be ascending)
------------------------------------------------------------
  3 -> SICP
  8 -> Linux Programming Interface
  12 -> CLRS
  17 -> Compilers (Dragon Book)
  19 -> Clean Code
  25 -> Pragmatic Programmer
  31 -> OSTEP
  38 -> DDIA
  41 -> Database Internals
  45 -> C Programming Language
  50 -> UNIX Network Programming
  55 -> TCP/IP Illustrated
  60 -> CS:APP
  63 -> Modern Operating Systems
  74 -> Computer Networks

[PHASE 3] Search
------------------------------------------------------------
  search(3)  -> found: "SICP"
  search(41) -> found: "Database Internals"
  search(74) -> found: "Computer Networks"
  search(99) -> not found

[PHASE 4] Erase
------------------------------------------------------------
  erase(12) -> removed
  erase(38) -> removed
  erase(55) -> removed
  erase(3)  -> removed
  erase(74) -> removed
  size = 10  [invariants OK after all erases]

[PHASE 5] In-order after erase
------------------------------------------------------------
  8 -> Linux Programming Interface
  17 -> Compilers (Dragon Book)
  19 -> Clean Code
  25 -> Pragmatic Programmer
  31 -> OSTEP
  41 -> Database Internals
  45 -> C Programming Language
  50 -> UNIX Network Programming
  60 -> CS:APP
  63 -> Modern Operating Systems

[PHASE 6] Overwrite
------------------------------------------------------------
  After overwrite(41): "Database Internals (2nd read)"
  size unchanged: YES

============================================================
  All checks passed.
============================================================
```

---

## 12. Files in this Submission

| File | Description |
|------|-------------|
| `rb_tree.cpp` | C++ implementation of the red-black tree |
| `Makefile` | Build instructions |
| `Assignment.md` | This document |
| `README.md` | Quick-start guide |

---

## 13. References

- Cormen, T.H. et al. *Introduction to Algorithms*, 3rd ed., Ch. 13 (Red-Black Trees)
- PostgreSQL source: `src/backend/storage/lmgr/` (lock manager RB-tree usage)
- cppreference.com: `std::map` implementation notes
